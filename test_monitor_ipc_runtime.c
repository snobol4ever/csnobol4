/*
 * test_monitor_ipc_runtime.c — standalone smoke test for the CSN bridge.
 *
 * Builds against monitor_ipc_runtime.c and exercises the three public
 * entry points with hand-crafted struct mir_descr / SCBLK shapes.
 * Verifies records appear on MONITOR_READY_PIPE and a names sidecar
 * is written at exit.
 *
 * Build:
 *   gcc -Wall -O2 -o /tmp/mir_smoke \
 *       /home/claude/csnobol4/test_monitor_ipc_runtime.c \
 *       /home/claude/csnobol4/monitor_ipc_runtime.c
 *
 * Run (controller side first creates FIFOs and reads the wire):
 *   mkfifo /tmp/r.fifo /tmp/g.fifo
 *   MONITOR_READY_PIPE=/tmp/r.fifo MONITOR_GO_PIPE=/tmp/g.fifo \
 *   MONITOR_NAMES_OUT=/tmp/names.out /tmp/mir_smoke
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* Replicate the descriptor shape used inside monitor_ipc_runtime.c. */
typedef long      int_t;
typedef double    real_t;

struct mir_descr {
    union { int_t i; real_t f; } a;
    char         f;
    unsigned int v;
};

#define DESCR_SZ ((int)sizeof(struct mir_descr))
#define BCDFLD   (4 * DESCR_SZ)

#define CSN_T_STRING    1
#define CSN_T_INTEGER   6
#define CSN_T_REAL      7

extern int monitor_emit_value (void *, void *);
extern int monitor_emit_call  (void *);
extern int monitor_emit_return(void *, void *);

/* Build a fake SCBLK in a static buffer for STRING/NAME descriptors. */
static unsigned char scblk_buf[256][512];
static int next_scblk = 0;

static void make_str_descr(struct mir_descr *d, const char *s) {
    int len = (int)strlen(s);
    int slot = next_scblk++;
    /* SCBLK: header is 4 descrs (BCDFLD bytes); v of first descr holds len. */
    memset(scblk_buf[slot], 0, BCDFLD);
    struct mir_descr *hdr = (struct mir_descr *)scblk_buf[slot];
    hdr->v = (unsigned int)len;
    memcpy(scblk_buf[slot] + BCDFLD, s, (size_t)len);

    d->a.i = (int_t)(intptr_t)scblk_buf[slot];
    d->f   = 0;
    d->v   = CSN_T_STRING;
}

static void make_int_descr(struct mir_descr *d, int_t value) {
    d->a.i = value;
    d->f   = 0;
    d->v   = CSN_T_INTEGER;
}

static void make_real_descr(struct mir_descr *d, real_t value) {
    d->a.f = value;
    d->f   = 0;
    d->v   = CSN_T_REAL;
}

int main(void) {
    struct mir_descr name1, val1;
    struct mir_descr name2, val2;
    struct mir_descr name3, val3;
    struct mir_descr fname, fret;

    make_str_descr(&name1, "x");
    make_str_descr(&val1,  "hello");

    make_str_descr(&name2, "y");
    make_int_descr(&val2,  42);

    make_str_descr(&name3, "z");
    make_real_descr(&val3, 3.14);

    make_str_descr(&fname, "SQR");
    make_int_descr(&fret,  49);

    monitor_emit_value(&name1, &val1);   /* VALUE x = STRING "hello" */
    monitor_emit_value(&name2, &val2);   /* VALUE y = INTEGER 42 */
    monitor_emit_value(&name3, &val3);   /* VALUE z = REAL 3.14 */
    monitor_emit_call(&fname);           /* CALL SQR */
    monitor_emit_return(&fname, &fret);  /* RETURN SQR = 49 */

    fprintf(stderr, "test harness emitted 5 records, exiting\n");
    return 0;
}
