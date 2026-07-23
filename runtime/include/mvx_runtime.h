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

/* --- output ------------------------------------------------------------ */
void mv_print(mvx_ctx *ctx, const mv_value *v);     /* no newline */
void mv_print_nl(mvx_ctx *ctx);
void mv_print_tab(mvx_ctx *ctx);                    /* comma zones (18 cols) */

/* --- intrinsics -------------------------------------------------------- */
void mv_time(mv_value *dst);                        /* secs since midnight */
void mv_system_fn(mv_value *dst, const mv_value *code);
void mv_int_fn(mv_value *dst, const mv_value *a);
void mv_sqrt_fn(mv_value *dst, const mv_value *a);
void mv_abs_fn(mv_value *dst, const mv_value *a);
void mv_mod_fn(mv_value *dst, const mv_value *a, const mv_value *b);

/* --- numeric-specialised support (compiler fast path) ------------------ */
void   *mvx_buf_create(int64_t nbytes);             /* zeroed */
void    mvx_buf_destroy(void *p);
void    mvx_narr_fail(int64_t i, int64_t j, int64_t d1, int64_t d2)
            __attribute__((noreturn));
double  mvx_num_time(void);
double  mvx_num_system(double code);
double  mvx_num_mod(double a, double b);
int64_t mvx_num_imod(int64_t a, int64_t b);

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
