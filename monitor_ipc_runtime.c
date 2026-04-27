/*
 * monitor_ipc_runtime.c — CSNOBOL4 statically-linked sync-step monitor bridge.
 *
 * Wire protocol: see ../one4all/scripts/monitor/monitor_wire.h (copied below
 * inline so this file builds standalone in the csnobol4 tree).
 *
 * Design (SN-26-csn-bridge-a, 2026-04-27 session):
 *   - Statically linked into xsnobol4 (object listed in Makefile2 OBJS).
 *   - No SNOBOL4 LOAD() involvement.  XCALLC sites in v311.sil call the
 *     C entry points directly (monitor_emit_value / monitor_emit_call /
 *     monitor_emit_return).
 *   - Lazy init on first emit: reads MONITOR_READY_PIPE / MONITOR_GO_PIPE.
 *     If unset, emits become silent no-ops (returns 1 so XCALLC bool gate
 *     never branches FAIL when monitoring is disabled).
 *   - Auto-interns names into a growing in-memory table (no static
 *     MONITOR_NAMES_FILE input).  At process exit (atexit handler), the
 *     table is dumped to MONITOR_NAMES_OUT — matches the per-participant
 *     names sidecar architecture from session #22.
 *   - End record (MWK_END) emitted at exit before the names sidecar is
 *     written, so the controller sees a clean wire close.
 *
 * The CSNOBOL4 "register" args (WPTR, ZPTR, ATPTR, RTZPTR) are pointers to
 * struct descr.  We accept void* and cast — matches the GETPARM pattern in
 * include/macros.h (#define GETPARM(A) getparm((struct spec *)(A))).
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/uio.h>
#include <sys/stat.h>

/*============================================================================
 * Wire protocol — inline copy of monitor_wire.h to avoid dep on one4all tree.
 * If the upstream header changes, sync these constants by hand.
 *==========================================================================*/
#define MWK_VALUE       1u
#define MWK_CALL        2u
#define MWK_RETURN      3u
#define MWK_END         4u

#define MWT_NULL        0
#define MWT_STRING      1
#define MWT_INTEGER     2
#define MWT_REAL        3
#define MWT_NAME        4
#define MWT_PATTERN     5
#define MWT_EXPRESSION  6
#define MWT_ARRAY       7
#define MWT_TABLE       8
#define MWT_CODE        9
#define MWT_DATA       10
#define MWT_FILE       11
#define MWT_UNKNOWN   255

#define MW_HDR_BYTES    13
#define MW_NAME_ID_NONE 0xffffffffu

static inline void mw_pack_hdr(unsigned char hdr[MW_HDR_BYTES],
                               uint32_t kind, uint32_t name_id,
                               uint8_t  type, uint32_t value_len)
{
    hdr[0]  = (unsigned char)( kind        & 0xff);
    hdr[1]  = (unsigned char)((kind  >>  8)& 0xff);
    hdr[2]  = (unsigned char)((kind  >> 16)& 0xff);
    hdr[3]  = (unsigned char)((kind  >> 24)& 0xff);
    hdr[4]  = (unsigned char)( name_id      & 0xff);
    hdr[5]  = (unsigned char)((name_id>>  8)& 0xff);
    hdr[6]  = (unsigned char)((name_id>> 16)& 0xff);
    hdr[7]  = (unsigned char)((name_id>> 24)& 0xff);
    hdr[8]  = type;
    hdr[9]  = (unsigned char)( value_len      & 0xff);
    hdr[10] = (unsigned char)((value_len>>  8)& 0xff);
    hdr[11] = (unsigned char)((value_len>> 16)& 0xff);
    hdr[12] = (unsigned char)((value_len>> 24)& 0xff);
}

/*============================================================================
 * CSNOBOL4 ABI shape — matches struct descr / SCBLK layout.
 *
 * Same layout as monitor_ipc_bin_csn.c uses; we replicate the minimal subset
 * here so the file builds without csnobol4 internal headers (which pull in
 * res.h / globals.h / config.h dependencies we don't need).
 *==========================================================================*/
typedef long      int_t;
typedef double    real_t;

struct mir_descr {
    union { int_t i; real_t f; } a;
    char         f;
    unsigned int v;
};

#define DESCR_SZ   ((int)sizeof(struct mir_descr))
#define BCDFLD     (4 * DESCR_SZ)

/* CSNOBOL4 type tag codes (from equ.h, verified against existing IPC lib): */
#define CSN_T_STRING    1
#define CSN_T_PATTERN   3
#define CSN_T_ARRAY     4
#define CSN_T_TABLE     5
#define CSN_T_INTEGER   6
#define CSN_T_REAL      7
#define CSN_T_CODE      8
#define CSN_T_NAME      9
#define CSN_T_EXTERNAL 11

static inline void *_d_blk(const struct mir_descr *d) {
    return (void *)(d->a.i);
}
static inline int _d_slen(const struct mir_descr *d) {
    void *blk = _d_blk(d);
    if (!blk) return 0;
    return (int)((struct mir_descr *)blk)->v;
}
static inline const char *_d_sptr(const struct mir_descr *d) {
    void *blk = _d_blk(d);
    if (!blk) return NULL;
    return (const char *)blk + BCDFLD;
}

static uint8_t csn_tag_to_wire(unsigned int v) {
    switch (v) {
        case CSN_T_STRING:   return MWT_STRING;
        case CSN_T_INTEGER:  return MWT_INTEGER;
        case CSN_T_REAL:     return MWT_REAL;
        case CSN_T_PATTERN:  return MWT_PATTERN;
        case CSN_T_NAME:     return MWT_NAME;
        case CSN_T_ARRAY:    return MWT_ARRAY;
        case CSN_T_TABLE:    return MWT_TABLE;
        case CSN_T_CODE:     return MWT_CODE;
        case CSN_T_EXTERNAL: return MWT_DATA;
        case 0:              return MWT_NULL;
        default:             return MWT_UNKNOWN;
    }
}

/*============================================================================
 * Module state.
 *==========================================================================*/
static int   g_ready_fd       = -1;     /* write end of READY pipe */
static int   g_go_fd          = -1;     /* read end  of GO    pipe */
static int   g_init_attempted = 0;      /* tried to open FIFOs?    */
static int   g_init_ok        = 0;      /* FIFOs open & usable?    */
static int   g_atexit_done    = 0;      /* atexit handler ran?     */

/* Auto-interning name table — grows as new names are observed.
 * Linear-scan lookup; fine for tens-to-hundreds of names per run. */
static char    **g_names      = NULL;
static int     *g_name_lens   = NULL;
static int      g_n_names     = 0;
static int      g_names_cap   = 0;

static char    *g_names_out_path = NULL;  /* set in init from env */

/*============================================================================
 * atexit: emit MWK_END and dump names sidecar.
 *==========================================================================*/
static void emit_record_raw(uint32_t kind, uint32_t name_id, uint8_t type,
                            const void *value, uint32_t value_len);

static void monitor_atexit(void) {
    if (g_atexit_done) return;
    g_atexit_done = 1;

    if (g_init_ok && g_ready_fd >= 0) {
        emit_record_raw(MWK_END, MW_NAME_ID_NONE, MWT_NULL, NULL, 0);
    }
    if (g_names_out_path && g_names) {
        FILE *f = fopen(g_names_out_path, "w");
        if (f) {
            for (int i = 0; i < g_n_names; i++) {
                fwrite(g_names[i], 1, (size_t)g_name_lens[i], f);
                fputc('\n', f);
            }
            fclose(f);
        }
    }
    if (g_ready_fd >= 0) { close(g_ready_fd); g_ready_fd = -1; }
    if (g_go_fd    >= 0) { close(g_go_fd);    g_go_fd    = -1; }
}

/*============================================================================
 * Lazy init — read env vars on first emit.  Returns 1 on success, 0 if
 * monitoring is disabled (env vars unset or FIFOs unopenable).
 *==========================================================================*/
static int monitor_init(void) {
    if (g_init_attempted) return g_init_ok;
    g_init_attempted = 1;

    const char *ready_path = getenv("MONITOR_READY_PIPE");
    const char *go_path    = getenv("MONITOR_GO_PIPE");
    const char *names_path = getenv("MONITOR_NAMES_OUT");
    if (!ready_path || !*ready_path) return 0;
    if (!go_path    || !*go_path)    return 0;

    /* Open READY: write end, non-blocking first to avoid deadlock if the
     * controller hasn't yet opened the read end, then we proceed; FIFO
     * semantics will block writes if no reader, which is what we want. */
    int rfd = open(ready_path, O_WRONLY | O_NONBLOCK);
    if (rfd < 0) rfd = open(ready_path, O_WRONLY);
    if (rfd < 0) return 0;

    /* Clear non-blocking on the ready fd so writev() blocks if the pipe is
     * full — proper backpressure. */
    int fl = fcntl(rfd, F_GETFL);
    if (fl >= 0) fcntl(rfd, F_SETFL, fl & ~O_NONBLOCK);

    int gfd = open(go_path, O_RDONLY);
    if (gfd < 0) { close(rfd); return 0; }

    g_ready_fd = rfd;
    g_go_fd    = gfd;
    if (names_path && *names_path) {
        size_t n = strlen(names_path);
        g_names_out_path = (char *)malloc(n + 1);
        if (g_names_out_path) memcpy(g_names_out_path, names_path, n + 1);
    }
    g_init_ok = 1;
    atexit(monitor_atexit);
    return 1;
}

/*============================================================================
 * Name interning — linear scan, append on miss.  Returns name_id.
 *==========================================================================*/
static uint32_t intern_name(const char *p, int len) {
    if (!p || len < 0) return MW_NAME_ID_NONE;
    for (int i = 0; i < g_n_names; i++) {
        if (g_name_lens[i] == len &&
            (len == 0 || memcmp(g_names[i], p, (size_t)len) == 0))
            return (uint32_t)i;
    }
    if (g_n_names == g_names_cap) {
        int nc = g_names_cap ? g_names_cap * 2 : 64;
        char **nn = (char **)realloc(g_names,    nc * sizeof(char *));
        int   *nl = (int  *)realloc(g_name_lens, nc * sizeof(int));
        if (!nn || !nl) { free(nn); free(nl); return MW_NAME_ID_NONE; }
        g_names      = nn;
        g_name_lens  = nl;
        g_names_cap  = nc;
    }
    char *copy = (char *)malloc((size_t)len + 1);
    if (!copy) return MW_NAME_ID_NONE;
    if (len > 0) memcpy(copy, p, (size_t)len);
    copy[len] = '\0';
    g_names[g_n_names]     = copy;
    g_name_lens[g_n_names] = len;
    return (uint32_t)g_n_names++;
}

/*============================================================================
 * Wait for ack: read 1 byte from the GO pipe.  Returns 1 on 'G' (or any
 * non-'S'), 0 on 'S' (stop) / EOF / error.
 *==========================================================================*/
static int wait_ack(void) {
    if (g_go_fd < 0) return 0;
    char ack;
    ssize_t r = read(g_go_fd, &ack, 1);
    if (r != 1) return 0;
    return (ack != 'S');
}

/*============================================================================
 * Internal: emit a record (header + optional value bytes), then block on ack.
 *==========================================================================*/
static void emit_record_raw(uint32_t kind, uint32_t name_id, uint8_t type,
                            const void *value, uint32_t value_len)
{
    if (g_ready_fd < 0) return;

    unsigned char hdr[MW_HDR_BYTES];
    mw_pack_hdr(hdr, kind, name_id, type, value_len);

    struct iovec iov[2];
    int niov = 1;
    iov[0].iov_base = hdr;
    iov[0].iov_len  = MW_HDR_BYTES;
    if (value_len > 0 && value) {
        iov[1].iov_base = (void *)value;
        iov[1].iov_len  = (size_t)value_len;
        niov = 2;
    }

    ssize_t total = (ssize_t)MW_HDR_BYTES + (ssize_t)value_len;
    ssize_t got   = writev(g_ready_fd, iov, niov);
    if (got != total) return;
    /* Don't wait for ack on END — controller closes its side first. */
    if (kind != MWK_END) (void)wait_ack();
}

/*============================================================================
 * Inspect a value descriptor and emit type+bytes.  Modeled on emit_value()
 * in monitor_ipc_bin_csn.c.
 *==========================================================================*/
static void emit_descr_value(uint32_t kind, uint32_t name_id,
                             const struct mir_descr *d)
{
    uint8_t type = csn_tag_to_wire(d->v);

    const void *vp   = NULL;
    uint32_t    vlen = 0;
    int_t  i_buf;
    real_t r_buf;

    switch (type) {
        case MWT_STRING:
        case MWT_NAME:
            vp   = _d_sptr(d);
            vlen = (uint32_t)_d_slen(d);
            if (vlen == 0) vp = NULL;
            break;
        case MWT_INTEGER: {
            int_t iv = d->a.i;
            unsigned char *p = (unsigned char *)&i_buf;
            for (int k = 0; k < 8; k++) p[k] = (unsigned char)((iv >> (k*8)) & 0xff);
            vp = &i_buf; vlen = 8;
            break;
        }
        case MWT_REAL: {
            real_t rv = d->a.f;
            memcpy(&r_buf, &rv, sizeof(r_buf));
            vp = &r_buf; vlen = 8;
            break;
        }
        default:
            /* PATTERN, ARRAY, TABLE, CODE, EXTERNAL, NULL, UNKNOWN → empty. */
            break;
    }
    emit_record_raw(kind, name_id, type, vp, vlen);
}

/*============================================================================
 * Public C entry points — called from v311.sil via XCALLC.
 *
 * Each accepts void* args (cast from CSNOBOL4 ptr_t = char*).  The args
 * are pointers to struct descr (the "registers" WPTR, ZPTR, etc.).
 *
 * Returns 1 always — XCALLC's boolean branching needs a stable result.
 * The actual "should we continue or stop" decision is made by the
 * controller via the ack byte ('G' vs 'S'), which wait_ack() reads.
 * If 'S' or any error, the FIFO machinery falls into a quiet-noop state
 * but we still return 1 so SIL flow doesn't take a SIL-level FAIL branch.
 *==========================================================================*/

/*============================================================================
 * lvalue name extraction — robust against NAME descriptors that point into
 * the middle of array element storage or table slots (where there is no
 * SCBLK header and no name string at the +BCDFLD offset).
 *
 * For SNOBOL4 lvalue forms:
 *   X = ...        → STRING/NAME descr at named vrblk; name string valid.
 *   PAT . X        → same as above (NAME of named variable).
 *   PAT $ X        → same.
 *   @X             → same.
 *   A<i,j> = ...   → NAME descr from ITEM proc, points into array storage.
 *                    No name string at +BCDFLD; reading there gives garbage.
 *   T<'k'> = ...   → same shape as array element.
 *
 * Heuristic: validate the candidate name characters against the length
 * implied by the descriptor.  A real SNOBOL4 identifier consists of
 * printable ASCII (letters, digits, underscore, dot — typically).  If
 * the bytes at +BCDFLD don't look like a valid identifier of that
 * length, treat as anonymous lvalue and intern the sentinel "<lval>"
 * so the wire stream stays well-formed and both runtimes converge on
 * the same record shape for these stores.
 *
 * Returns name_id (never MW_NAME_ID_NONE for a successfully initialized
 * monitor).
 *==========================================================================*/
static int looks_like_identifier(const char *p, int n) {
    if (n <= 0 || n > 256) return 0;
    if (!p) return 0;
    for (int i = 0; i < n; i++) {
        unsigned char c = (unsigned char)p[i];
        if (c < 0x20 || c >= 0x7f) return 0;   /* non-printable → reject */
    }
    return 1;
}

static uint32_t lvalue_name_id(const struct mir_descr *nd) {
    const char *np = _d_sptr(nd);
    int         nl = _d_slen(nd);
    if (looks_like_identifier(np, nl)) {
        return intern_name(np, nl);
    }
    /* Anonymous lvalue (array element, table slot, etc.).  Use a stable
     * sentinel so the wire stream is well-formed and both runtimes
     * converge on the same record. */
    static const char sentinel[] = "<lval>";
    return intern_name(sentinel, (int)(sizeof(sentinel) - 1));
}

/* monitor_emit_value(name_descr, value_descr) — VALUE event.
 * name_descr is a STRING descriptor giving the variable name.
 * value_descr is the new value being assigned. */
int monitor_emit_value(void *name_d_void, void *value_d_void) {
    if (!monitor_init()) return 1;
    const struct mir_descr *nd = (const struct mir_descr *)name_d_void;
    const struct mir_descr *vd = (const struct mir_descr *)value_d_void;
    if (!nd || !vd) return 1;

    uint32_t name_id = lvalue_name_id(nd);
    if (name_id == MW_NAME_ID_NONE) return 1;

    emit_descr_value(MWK_VALUE, name_id, vd);
    return 1;
}

/* monitor_emit_call(name_descr) — CALL event.
 * name_descr is a STRING/NAME descriptor giving the function name. */
int monitor_emit_call(void *name_d_void) {
    if (!monitor_init()) return 1;
    const struct mir_descr *nd = (const struct mir_descr *)name_d_void;
    if (!nd) return 1;

    const char *np = _d_sptr(nd);
    int         nl = _d_slen(nd);
    if (!np && nl == 0) np = "";
    if (!np) return 1;

    uint32_t name_id = intern_name(np, nl);
    if (name_id == MW_NAME_ID_NONE) return 1;

    emit_record_raw(MWK_CALL, name_id, MWT_NULL, NULL, 0);
    return 1;
}

/* monitor_emit_return(name_descr, rtntype_descr) — RETURN event.
 * name_descr names the function.
 * rtntype_descr carries SIL's &RTNTYPE — a NAME descriptor pointing to one
 * of RETURN / NRETURN / FRETURN — i.e. *how* the function exited, not the
 * function's result value.  The function's actual return value, if any,
 * is delivered separately via a preceding monitor_emit_value(fn_name, ...)
 * event because in SIL the function-name variable is where the result is
 * stored (e.g. `SQR = N * N` inside SQR's body).
 *
 * Wire-format consumer note: a RETURN record carrying STRING("RETURN")
 * means "function returned normally"; STRING("FRETURN") means "function
 * failed"; STRING("NRETURN") means "name-return".  The preceding VALUE
 * record names the result. */
int monitor_emit_return(void *name_d_void, void *rtntype_d_void) {
    if (!monitor_init()) return 1;
    const struct mir_descr *nd = (const struct mir_descr *)name_d_void;
    const struct mir_descr *rd = (const struct mir_descr *)rtntype_d_void;
    if (!nd || !rd) return 1;

    const char *np = _d_sptr(nd);
    int         nl = _d_slen(nd);
    if (!np && nl == 0) np = "";
    if (!np) return 1;

    uint32_t name_id = intern_name(np, nl);
    if (name_id == MW_NAME_ID_NONE) return 1;

    emit_descr_value(MWK_RETURN, name_id, rd);
    return 1;
}

/* end of monitor_ipc_runtime.c */
