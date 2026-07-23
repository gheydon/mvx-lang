/* MVX runtime — the ABI surface the compiler emits calls against.
 *
 * Everything in this header is part of the compiled-code contract.
 * See DECISIONS.md before changing anything: the mv_value layout and the
 * mvx_sub_* signature are permanent once subroutines ship in shared
 * libraries.
 */
#ifndef MVX_RUNTIME_H
#define MVX_RUNTIME_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    MV_UNASSIGNED = 0,
    MV_INT        = 1,
    MV_DBL        = 2,
    MV_STR        = 3,
    MV_FILE       = 4,      /* file variable; i holds the mvx_file* */
};

typedef struct mv_string {
    int64_t refs;
    int64_t len;
    char    data[];             /* NUL-terminated */
} mv_string;

typedef struct mv_value {
    int64_t    tag;
    int64_t    i;
    double     d;
    mv_string *s;
} mv_value;

typedef struct mv_array mv_array;
typedef struct mvx_ctx  mvx_ctx;

/* --- context ----------------------------------------------------------- */
mvx_ctx *mvx_ctx_create(void);
void     mvx_ctx_destroy(mvx_ctx *ctx);

/* --- value lifecycle --------------------------------------------------- */
void mv_init(mv_value *v);
void mv_clear(mv_value *v);                         /* release + unassigned */
void mv_set_int(mv_value *v, int64_t i);
void mv_set_dbl(mv_value *v, double d);
void mv_set_str(mv_value *v, const char *p, int64_t len);   /* copies */
void mv_copy(mv_value *dst, const mv_value *src);           /* retains str */

/* --- coercion ---------------------------------------------------------- */
int64_t mv_get_int(const mv_value *v);
double  mv_get_dbl(const mv_value *v);
int64_t mv_truth(const mv_value *v);                /* boolean context */

/* --- arithmetic / comparison (dst may alias operands) ------------------ */
void    mv_add(mv_value *dst, const mv_value *a, const mv_value *b);
void    mv_sub(mv_value *dst, const mv_value *a, const mv_value *b);
void    mv_mul(mv_value *dst, const mv_value *a, const mv_value *b);
void    mv_div(mv_value *dst, const mv_value *a, const mv_value *b);
void    mv_pow(mv_value *dst, const mv_value *a, const mv_value *b);
void    mv_neg(mv_value *dst, const mv_value *a);
void    mv_cat(mv_value *dst, const mv_value *a, const mv_value *b);
int64_t mv_compare(const mv_value *a, const mv_value *b);   /* <0, 0, >0 */

/* --- dimensioned arrays ------------------------------------------------ */
mv_array *mv_arr_create(int64_t d1, int64_t d2);    /* d2 == 0 → one-dim */
void      mv_arr_destroy(mv_array *a);
mv_value *mv_arr_elem(mv_array *a, int64_t i, int64_t j); /* 1-based, checked */
void      mv_arr_fill(mv_array *a, const mv_value *val);
void      mv_arr_copy(mv_array *dst, const mv_array *src); /* dims must match */

/* --- string intrinsics ------------------------------------------------- */
void    mv_char_fn(mv_value *dst, int64_t code);
int64_t mv_seq_fn(const mv_value *v);
void    mv_space_fn(mv_value *dst, int64_t n);
void    mv_str_fn(mv_value *dst, const mv_value *src, int64_t n);
void    mv_trim_fn(mv_value *dst, const mv_value *src);
void    mv_field_fn(mv_value *dst, const mv_value *src,
                    const mv_value *delim, int64_t n, int64_t cnt);
int64_t mv_index_fn(const mv_value *src, const mv_value *sub, int64_t occ);
int64_t mv_num_fn(const mv_value *v);
void    mv_substr(mv_value *dst, const mv_value *src, int64_t start,
                  int64_t len);

/* --- conversions and formatting ---------------------------------------- */
void    mv_oconv(mvx_ctx *ctx, mv_value *dst, const mv_value *src,
                 const mv_value *code);
void    mv_iconv(mvx_ctx *ctx, mv_value *dst, const mv_value *src,
                 const mv_value *code);
void    mv_fmt(mv_value *dst, const mv_value *src, const mv_value *mask);
int64_t mvx_status(mvx_ctx *ctx);

/* --- COMMON blocks ------------------------------------------------------ */
mv_value *mvx_common_scalar(mvx_ctx *ctx, const char *block, int64_t idx);
mv_array *mvx_common_arr(mvx_ctx *ctx, const char *block, int64_t idx,
                         int64_t d1, int64_t d2);

/* --- input / output ---------------------------------------------------- */
void mv_input(mvx_ctx *ctx, mv_value *dst);         /* read line from stdin */
void mv_sentence(mvx_ctx *ctx, mv_value *dst);      /* invoking TCL sentence */
void mv_env(mv_value *dst, const mv_value *name);   /* getenv, "" if unset */
void mv_print(mvx_ctx *ctx, const mv_value *v);     /* no newline */
void mv_print_nl(mvx_ctx *ctx);
void mv_print_tab(mvx_ctx *ctx);                    /* comma zones (18 cols) */

/* --- intrinsics -------------------------------------------------------- */
void mv_time(mv_value *dst);                        /* secs since midnight */
void mv_system_fn(mvx_ctx *ctx, mv_value *dst, const mv_value *code);
void mv_int_fn(mv_value *dst, const mv_value *a);
void mv_sqrt_fn(mv_value *dst, const mv_value *a);
void mv_abs_fn(mv_value *dst, const mv_value *a);
void mv_mod_fn(mv_value *dst, const mv_value *a, const mv_value *b);

/* --- dynamic arrays ----------------------------------------------------
   Marks: attribute 0xFE, value 0xFD, subvalue 0xFC.  Subscript 0 means
   "not specified"; negative means append at that level.  dst may alias
   src.                                                                  */
void mv_extract_fn(mv_value *dst, const mv_value *src, int64_t a,
                   int64_t v, int64_t s);
void mv_replace_fn(mv_value *dst, const mv_value *src, int64_t a,
                   int64_t v, int64_t s, const mv_value *val);
void mv_insert_fn(mv_value *dst, const mv_value *src, int64_t a,
                  int64_t v, int64_t s, const mv_value *val);
void mv_delete_fn(mv_value *dst, const mv_value *src, int64_t a,
                  int64_t v, int64_t s);
int64_t mv_locate_fn(const mv_value *item, const mv_value *src, int64_t a,
                     int64_t v, const mv_value *order, int64_t *pos);
int64_t mv_len_fn(const mv_value *v);
int64_t mv_count_fn(const mv_value *src, const mv_value *what);
int64_t mv_dcount_fn(const mv_value *src, const mv_value *delim);

/* --- numeric-specialised support (compiler fast path) ------------------ */
void   *mvx_buf_create(int64_t nbytes);             /* zeroed */
void    mvx_buf_destroy(void *p);
void    mvx_narr_fail(int64_t i, int64_t j, int64_t d1, int64_t d2)
            __attribute__((noreturn));
double  mvx_num_time(void);
double  mvx_num_system(mvx_ctx *ctx, double code);
int64_t mvx_list_active(mvx_ctx *ctx);
double  mvx_num_mod(double a, double b);
int64_t mvx_num_imod(int64_t a, int64_t b);

/* --- storage (Slice 2) --------------------------------------------------
   File variables carry MV_FILE tag.  All boolean results are int64:
   1 = success/found, 0 = failure/not-found (drives THEN/ELSE).         */
int64_t mvx_open(mvx_ctx *ctx, const mv_value *dict, const mv_value *spec,
                 mv_value *fvar);
int64_t mvx_read(mvx_ctx *ctx, mv_value *rec, const mv_value *fvar,
                 const mv_value *id, int64_t lock);
void    mvx_write(mvx_ctx *ctx, const mv_value *rec, const mv_value *fvar,
                  const mv_value *id, int64_t keep_lock);
int64_t mvx_delete_rec(mvx_ctx *ctx, const mv_value *fvar,
                       const mv_value *id);
void    mvx_release(mvx_ctx *ctx, const mv_value *fvar, const mv_value *id);
void    mvx_select(mvx_ctx *ctx, const mv_value *fvar);
void    mvx_formlist(mvx_ctx *ctx, const mv_value *ids); /* AM-separated */
int64_t mvx_readnext(mvx_ctx *ctx, mv_value *id);
int64_t mvx_createfile(mvx_ctx *ctx, const mv_value *spec,
                       const mv_value *type);       /* type NULL/"DIR" */
int64_t mvx_deletefile(mvx_ctx *ctx, const mv_value *spec);
void    mvx_store_shutdown(mvx_ctx *ctx);           /* ctx destroy hook */

/* --- runtime CALL resolution (jBASE catalog model) --------------------- */
void mvx_call(mvx_ctx *ctx, const char *name, int32_t argc,
              mv_value **argv);
void mvx_call_var(mvx_ctx *ctx, const mv_value *name, int32_t argc,
                  mv_value **argv);

/* --- spawning, behind the privilege gate (see mvx_exec.c) -------------- */
int64_t mvx_unix_cmd(mvx_ctx *ctx, const char *cmd);    /* unrestricted */
int64_t mvx_compile(mvx_ctx *ctx, const mv_value *mode,
                    const mv_value *src, const mv_value *out); /* developer */
int64_t mvx_execute(mvx_ctx *ctx, const mv_value *sentence,
                    mv_value *capture, mv_value *rc);   /* any tier */
void   *mvx_ctx_store_get(mvx_ctx *ctx);
void    mvx_ctx_store_set(mvx_ctx *ctx, void *p);

/* Borrow a char view of a value (numeric tags render into numbuf). */
int64_t mv_val_chars(const mv_value *v, char *numbuf, size_t cap,
                     const char **out);

/* --- errors / ABI support ---------------------------------------------- */
void mvx_fatal(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));
void mvx_stop(void) __attribute__((noreturn));      /* STOP: end the program */
void mvx_arity_check(const char *name, int32_t expected, int32_t got);

/* Compiled main programs export this; the runtime crt calls it. */
void mvx_main(mvx_ctx *ctx);

#ifdef __cplusplus
}
#endif
#endif /* MVX_RUNTIME_H */
