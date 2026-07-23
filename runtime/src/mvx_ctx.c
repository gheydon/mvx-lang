#include "mvx_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

/* Session context.  Slice 1 carries only output state; the field exists
   in every ABI signature so that session identity, locks, and the
   privilege gate can land here later without breaking compiled code. */
struct mvx_ctx {
    int64_t print_col;      /* current output column, for comma zones */
};

mvx_ctx *mvx_ctx_create(void) {
    mvx_ctx *ctx = calloc(1, sizeof(mvx_ctx));
    if (!ctx) mvx_fatal("out of memory creating context");
    return ctx;
}

void mvx_ctx_destroy(mvx_ctx *ctx) {
    free(ctx);
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

void mv_system_fn(mv_value *dst, const mv_value *code) {
    int64_t s, ms;
    switch (mv_get_int(code)) {
    case 12:                       /* ms since midnight — later-MV extension;
                                      classic TIME() is whole seconds, too
                                      coarse for benchmarking */
        time_of_day(&s, &ms);
        mv_set_int(dst, ms);
        return;
    default:
        mvx_fatal("SYSTEM(%lld) not implemented in Slice 1",
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

double mvx_num_system(double code) {
    mv_value c, r;
    mv_init(&c); mv_init(&r);
    mv_set_int(&c, (int64_t)code);
    mv_system_fn(&r, &c);
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
