/* Terminal primitives for full-screen applications.
 *
 * KEYIN() delivers one DECODED keystroke: printable characters as
 * themselves, specials as logical names (UP, F3, ENTER, ...), never
 * raw escape bytes.  Raw terminal mode is entered and restored per
 * call, so a program cannot leave the terminal wedged.  The decoder
 * reads plain bytes too, so piped input (and tests) decode the same
 * way a terminal does.
 *
 * @(col,row) yields ANSI positioning strings for PRINT, with the
 * classic negative codes; ECHO ON/OFF drives the tty echo flag used
 * by INPUT.
 */
#include "mvx_runtime.h"

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

/* One byte of pushback: a lone ESC followed by an unrelated byte must
   not swallow that byte. */
static int g_pushed = -1;

static int read_byte(int64_t timeout_ms, unsigned char *c) {
    if (g_pushed >= 0) {
        *c = (unsigned char)g_pushed;
        g_pushed = -1;
        return 1;
    }
    if (timeout_ms >= 0) {
        struct pollfd p = {0, POLLIN, 0};
        int r = poll(&p, 1, (int)timeout_ms);
        if (r <= 0) return 0;
    }
    return read(0, c, 1) == 1;
}

static int term_raw(struct termios *saved) {
    if (!isatty(0)) return 0;
    if (tcgetattr(0, saved) != 0) return 0;
    struct termios t = *saved;
    t.c_lflag &= ~(ICANON | ECHO);      /* keep ISIG: ^C still breaks */
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &t);
    return 1;
}

static void term_restore(int active, const struct termios *saved) {
    if (active) tcsetattr(0, TCSANOW, saved);
}

static void set_name(mv_value *dst, const char *n) {
    mv_set_str(dst, n, (int64_t)strlen(n));
}

/* Decode a CSI / SS3 sequence after ESC. */
static void decode_escape(mv_value *dst) {
    unsigned char c;
    if (!read_byte(30, &c)) {           /* lone ESC */
        set_name(dst, "ESC");
        return;
    }
    if (c == 'O') {                     /* SS3: F1-F4, and arrows when
                                           the terminal runs application
                                           cursor mode (Warp does) */
        if (!read_byte(30, &c)) { set_name(dst, "ESC"); return; }
        switch (c) {
        case 'P': set_name(dst, "F1"); return;
        case 'Q': set_name(dst, "F2"); return;
        case 'R': set_name(dst, "F3"); return;
        case 'S': set_name(dst, "F4"); return;
        case 'A': set_name(dst, "UP"); return;
        case 'B': set_name(dst, "DOWN"); return;
        case 'C': set_name(dst, "RIGHT"); return;
        case 'D': set_name(dst, "LEFT"); return;
        case 'H': set_name(dst, "HOME"); return;
        case 'F': set_name(dst, "END"); return;
        default:  set_name(dst, "ESC"); g_pushed = c; return;
        }
    }
    if (c != '[') {                     /* ESC then something else */
        set_name(dst, "ESC");
        g_pushed = c;
        return;
    }
    char params[16];
    size_t np = 0;
    for (;;) {
        if (!read_byte(30, &c)) { set_name(dst, "ESC"); return; }
        if (c >= 0x40 && c <= 0x7E) break;      /* final byte */
        if (np < sizeof params - 1) params[np++] = (char)c;
    }
    params[np] = '\0';
    switch (c) {
    case 'A': set_name(dst, "UP"); return;
    case 'B': set_name(dst, "DOWN"); return;
    case 'C': set_name(dst, "RIGHT"); return;
    case 'D': set_name(dst, "LEFT"); return;
    case 'H': set_name(dst, "HOME"); return;
    case 'F': set_name(dst, "END"); return;
    case 'Z': set_name(dst, "BTAB"); return;
    case '~': {
        int n = atoi(params);
        switch (n) {
        case 1: case 7:  set_name(dst, "HOME"); return;
        case 4: case 8:  set_name(dst, "END"); return;
        case 2:  set_name(dst, "INS"); return;
        case 3:  set_name(dst, "DEL"); return;
        case 5:  set_name(dst, "PGUP"); return;
        case 6:  set_name(dst, "PGDN"); return;
        case 11: set_name(dst, "F1"); return;
        case 12: set_name(dst, "F2"); return;
        case 13: set_name(dst, "F3"); return;
        case 14: set_name(dst, "F4"); return;
        case 15: set_name(dst, "F5"); return;
        case 17: set_name(dst, "F6"); return;
        case 18: set_name(dst, "F7"); return;
        case 19: set_name(dst, "F8"); return;
        case 20: set_name(dst, "F9"); return;
        case 21: set_name(dst, "F10"); return;
        case 23: set_name(dst, "F11"); return;
        case 24: set_name(dst, "F12"); return;
        }
        set_name(dst, "ESC");
        return;
    }
    default:
        set_name(dst, "ESC");
        return;
    }
}

void mv_keyin(mvx_ctx *ctx, mv_value *dst, int64_t timeout_ms) {
    (void)ctx;
    fflush(stdout);
    struct termios saved;
    int raw = term_raw(&saved);

    unsigned char c;
    if (!read_byte(timeout_ms, &c)) {   /* timeout (or EOF with pushback) */
        term_restore(raw, &saved);
        mv_set_str(dst, "", 0);
        return;
    }

    if (c == 27) {
        decode_escape(dst);
    } else if (c == '\r' || c == '\n') {
        set_name(dst, "ENTER");
    } else if (c == '\t') {
        set_name(dst, "TAB");
    } else if (c == 8 || c == 127) {
        set_name(dst, "BS");
    } else if (c < 32) {
        char n[3] = {'^', (char)(c + 64), 0};
        set_name(dst, n);
    } else if (c < 0x80) {
        char ch = (char)c;
        mv_set_str(dst, &ch, 1);
    } else {
        /* UTF-8: deliver the whole character */
        char buf[8];
        size_t len = 1;
        buf[0] = (char)c;
        int cont = (c & 0xE0) == 0xC0   ? 1
                   : (c & 0xF0) == 0xE0 ? 2
                   : (c & 0xF8) == 0xF0 ? 3
                                        : 0;
        for (int i = 0; i < cont; i++) {
            unsigned char cc;
            if (!read_byte(30, &cc)) break;
            buf[len++] = (char)cc;
        }
        mv_set_str(dst, buf, (int64_t)len);
    }
    term_restore(raw, &saved);
}

/* @(col,row) and the classic screen codes. */
void mv_at_fn(mv_value *dst, int64_t a, int64_t b, int64_t has_b) {
    char buf[40];
    int n;
    if (has_b) {
        n = snprintf(buf, sizeof buf, "\033[%lld;%lldH",
                     (long long)b + 1, (long long)a + 1);
    } else if (a >= 0) {
        n = snprintf(buf, sizeof buf, "\033[%lldG", (long long)a + 1);
    } else {
        switch (a) {
        case -1: n = snprintf(buf, sizeof buf, "\033[2J\033[H"); break;
        case -2: n = snprintf(buf, sizeof buf, "\033[H"); break;
        case -3: n = snprintf(buf, sizeof buf, "\033[J"); break;
        case -4: n = snprintf(buf, sizeof buf, "\033[K"); break;
        /* MVX extensions for full-screen work: the alternate screen
           buffer is what makes the geometry from SYSTEM(2)/(3) true in
           terminals that decorate the main screen (Warp blocks, VSCode
           prompt rows) — and it restores the scrollback on leave. */
        case -5: n = snprintf(buf, sizeof buf,
                              "\033[?1049h\033[2J\033[H"); break;
        case -6: n = snprintf(buf, sizeof buf, "\033[?1049l"); break;
        case -7: n = snprintf(buf, sizeof buf, "\033[?25l"); break;
        case -8: n = snprintf(buf, sizeof buf, "\033[?25h"); break;
        default: n = 0; break;
        }
    }
    mv_set_str(dst, buf, n);
}

void mv_echo(mvx_ctx *ctx, int64_t on) {
    (void)ctx;
    if (!isatty(0)) return;
    struct termios t;
    if (tcgetattr(0, &t) != 0) return;
    if (on) t.c_lflag |= ECHO;
    else t.c_lflag &= ~ECHO;
    tcsetattr(0, TCSANOW, &t);
}
