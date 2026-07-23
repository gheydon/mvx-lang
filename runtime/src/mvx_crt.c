/* Program entry for compiled MVX main programs.  Kept out of libmvxrt.a
 * so that shared-library subroutine builds never pick up a main().
 * The driver links this object explicitly when producing an executable.
 */
#include "mvx_runtime.h"

int main(void) {
    mvx_ctx *ctx = mvx_ctx_create();
    mvx_main(ctx);
    mvx_ctx_destroy(ctx);
    return 0;
}
