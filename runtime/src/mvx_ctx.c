#include "mvx_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

/* Session context.  The parameter exists in every ABI signature so that
   session identity, locks, and the privilege gate can land here later
   without breaking compiled code.  It currently carries output state,
   the STATUS() flag, and COMMON block storage. */

typedef struct common_slot {
    mv_value v;
    mv_array *arr;
} common_slot;

/* Slots are handed out by address and held for the life of compiled
   code, so storage must never move: slots live in fixed-size chunks
   allocated on demand, never realloc'd. */
#define COMMON_CHUNK 64
#define COMMON_MAX_CHUNKS 1024

typedef struct common_block {
    char *name;
    common_slot *chunks[COMMON_MAX_CHUNKS];
    struct common_block *next;
} common_block;

struct mvx_ctx {
    int64_t print_col;      /* current output column, for comma zones */
    int64_t status;         /* STATUS() value, set by conversions */
    common_block *commons;
    void *store;            /* storage state, owned by mvx_store.c */
};

void *mvx_ctx_store_get(mvx_ctx *ctx) { return ctx->store; }
void  mvx_ctx_store_set(mvx_ctx *ctx, void *p) { ctx->store = p; }

mvx_ctx *mvx_ctx_create(void) {
    mvx_ctx *ctx = calloc(1, sizeof(mvx_ctx));
    if (!ctx) mvx_fatal("out of memory creating context");
    return ctx;
}

void mvx_ctx_destroy(mvx_ctx *ctx) {
    mvx_store_shutdown(ctx);
    common_block *b = ctx->commons;
    while (b) {
        common_block *next = b->next;
        for (int c = 0; c < COMMON_MAX_CHUNKS; c++) {
            if (!b->chunks[c]) continue;
            for (int i = 0; i < COMMON_CHUNK; i++) {
                mv_clear(&b->chunks[c][i].v);
                mv_arr_destroy(b->chunks[c][i].arr);
            }
            free(b->chunks[c]);
        }
        free(b->name);
        free(b);
        b = next;
    }
    free(ctx);
}

void mvx_ctx_set_status(mvx_ctx *ctx, int64_t s) { ctx->status = s; }

int64_t mvx_status(mvx_ctx *ctx) { return ctx->status; }

/* ------------------------------------------------------- COMMON blocks */

static common_block *common_get(mvx_ctx *ctx, const char *name) {
    for (common_block *b = ctx->commons; b; b = b->next)
        if (strcmp(b->name, name) == 0) return b;
    common_block *b = calloc(1, sizeof(common_block));
    if (!b) mvx_fatal("out of memory creating COMMON block");
    b->name = strdup(name);
    b->next = ctx->commons;
    ctx->commons = b;
    return b;
}

static common_slot *common_slot_at(mvx_ctx *ctx, const char *name,
                                   int64_t idx) {
    common_block *b = common_get(ctx, name);
    if (idx < 0 || idx >= (int64_t)COMMON_CHUNK * COMMON_MAX_CHUNKS)
        mvx_fatal("COMMON index %lld out of range", (long long)idx);
    int64_t c = idx / COMMON_CHUNK;
    if (!b->chunks[c]) {
        b->chunks[c] = calloc(COMMON_CHUNK, sizeof(common_slot));
        if (!b->chunks[c]) mvx_fatal("out of memory extending COMMON block");
    }
    return &b->chunks[c][idx % COMMON_CHUNK];
}

mv_value *mvx_common_scalar(mvx_ctx *ctx, const char *block, int64_t idx) {
    return &common_slot_at(ctx, block, idx)->v;
}

mv_array *mvx_common_arr(mvx_ctx *ctx, const char *block, int64_t idx,
                         int64_t d1, int64_t d2) {
    common_slot *s = common_slot_at(ctx, block, idx);
    if (!s->arr) s->arr = mv_arr_create(d1, d2);
    return s->arr;
}

/* --------------------------------------------------------------- output */

static int64_t num_len_probe(const char *p) { return (int64_t)strlen(p); }

void mv_print(mvx_ctx *ctx, const mv_value *v) {
    char buf[40];
    switch (v->tag) {
    case MV_STR:
        fwrite(v->s->data, 1, (size_t)v->s->len, stdout);
        for (int64_t k = 0; k < v->s->len; k++)
            ctx->print_col = (v->s->data[k] == '\n') ? 0 : ctx->print_col + 1;
        return;
    case MV_INT:
        snprintf(buf, sizeof buf, "%lld", (long long)v->i);
        break;
    case MV_DBL: {
        double d = v->d;
        if (d == (double)(int64_t)d && d >= -1e15 && d <= 1e15)
            snprintf(buf, sizeof buf, "%lld", (long long)d);
        else {
            int n = snprintf(buf, sizeof buf, "%.4f", d);
            while (n > 0 && buf[n - 1] == '0') buf[--n] = '\0';
            if (n > 0 && buf[n - 1] == '.') buf[--n] = '\0';
        }
        break;
    }
    default:
        buf[0] = '\0';
        break;
    }
    fputs(buf, stdout);
    ctx->print_col += num_len_probe(buf);
}

void mv_print_nl(mvx_ctx *ctx) {
    fputc('\n', stdout);
    ctx->print_col = 0;
}

void mv_print_tab(mvx_ctx *ctx) {
    /* Classic 18-column print zones. */
    int64_t next = ((ctx->print_col / 18) + 1) * 18;
    while (ctx->print_col < next) {
        fputc(' ', stdout);
        ctx->print_col++;
    }
}

/* The TCL command line that invoked this program, set by the shell. */
void mv_sentence(mvx_ctx *ctx, mv_value *dst) {
    (void)ctx;
    const char *s = getenv("MVX_SENTENCE");
    if (!s) s = "";
    mv_set_str(dst, s, (int64_t)strlen(s));
}

/* ---------------------------------------------------------------- input */

void mv_input(mvx_ctx *ctx, mv_value *dst) {
    (void)ctx;
    char *line = NULL;
    size_t cap = 0;
    ssize_t n = getline(&line, &cap, stdin);
    if (n < 0) {
        mv_set_str(dst, "", 0);
        free(line);
        return;
    }
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) n--;
    mv_set_str(dst, line, n);
    free(line);
}

/* ------------------------------------------------------------ intrinsics */

static void time_of_day(int64_t *secs, int64_t *msecs) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int64_t since_midnight = tv.tv_sec % 86400;
    *secs  = since_midnight;
    *msecs = since_midnight * 1000 + tv.tv_usec / 1000;
}

void mv_time(mv_value *dst) {
    int64_t s, ms;
    time_of_day(&s, &ms);
    mv_set_int(dst, s);
}

void mv_system_fn(mvx_ctx *ctx, mv_value *dst, const mv_value *code) {
    int64_t s, ms;
    switch (mv_get_int(code)) {
    case 11:                       /* select list active? */
        mv_set_int(dst, mvx_list_active(ctx));
        return;
    case 12:                       /* ms since midnight — later-MV extension;
                                      classic TIME() is whole seconds, too
                                      coarse for benchmarking */
        time_of_day(&s, &ms);
        mv_set_int(dst, ms);
        return;
    default:
        mvx_fatal("SYSTEM(%lld) not implemented",
                  (long long)mv_get_int(code));
    }
}

void mv_int_fn(mv_value *dst, const mv_value *a) {
    mv_set_int(dst, mv_get_int(a));
}

void mv_sqrt_fn(mv_value *dst, const mv_value *a) {
    double d = mv_get_dbl(a);
    if (d < 0) mvx_fatal("SQRT of negative number");
    mv_set_dbl(dst, __builtin_sqrt(d));
}

void mv_abs_fn(mv_value *dst, const mv_value *a) {
    if (a->tag == MV_INT) mv_set_int(dst, a->i < 0 ? -a->i : a->i);
    else {
        double d = mv_get_dbl(a);
        mv_set_dbl(dst, d < 0 ? -d : d);
    }
}

/* ---------------------------------------- numeric-specialised support */

void *mvx_buf_create(int64_t nbytes) {
    void *p = calloc(1, (size_t)nbytes);
    if (!p) mvx_fatal("out of memory in DIM of %lld bytes", (long long)nbytes);
    return p;
}

void mvx_buf_destroy(void *p) { free(p); }

void mvx_narr_fail(int64_t i, int64_t j, int64_t d1, int64_t d2) {
    if (d2)
        mvx_fatal("array subscript (%lld,%lld) out of range (1..%lld,1..%lld)",
                  (long long)i, (long long)j, (long long)d1, (long long)d2);
    mvx_fatal("array subscript (%lld) out of range 1..%lld",
              (long long)i, (long long)d1);
}

double mvx_num_time(void) {
    mv_value v;
    mv_init(&v);
    mv_time(&v);
    return (double)v.i;
}

double mvx_num_system(mvx_ctx *ctx, double code) {
    mv_value c, r;
    mv_init(&c); mv_init(&r);
    mv_set_int(&c, (int64_t)code);
    mv_system_fn(ctx, &r, &c);
    return mv_get_dbl(&r);
}

double mvx_num_mod(double a, double b) {
    if (b == 0.0) return a;
    double r = __builtin_fmod(a, b);
    if (r != 0.0 && ((r < 0) != (b < 0))) r += b;
    return r;
}

int64_t mvx_num_imod(int64_t a, int64_t b) {
    if (b == 0) return a;
    int64_t r = a % b;
    if (r != 0 && ((r < 0) != (b < 0))) r += b;
    return r;
}

void mv_mod_fn(mv_value *dst, const mv_value *a, const mv_value *b) {
    if (a->tag == MV_INT && b->tag == MV_INT) {
        if (b->i == 0) { mv_set_int(dst, a->i); return; }  /* MV: MOD(x,0)=x */
        int64_t r = a->i % b->i;
        if (r != 0 && ((r < 0) != (b->i < 0))) r += b->i;
        mv_set_int(dst, r);
        return;
    }
    double x = mv_get_dbl(a), y = mv_get_dbl(b);
    if (y == 0.0) { mv_set_dbl(dst, x); return; }
    double r = __builtin_fmod(x, y);
    if (r != 0.0 && ((r < 0) != (y < 0))) r += y;
    mv_set_dbl(dst, r);
}
