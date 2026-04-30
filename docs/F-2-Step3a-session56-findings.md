# F-2 Step 3a — session #56 findings (STREXCCL implemented; insufficient alone)

GOAL-CSN-FENCE-FIX F-2 Step 3a continuation (session #56, 2026-04-30).

## TL;DR

Session #56 implemented the STREXCCL sentinel proposed by session #54
(SPITBOL `=ndexc` analog).  The implementation is correct in shape:
fence_function preserved 10/10, Tier F preserved 16/16, guard5
preserved.  But fence_suite did **not** improve — same 43 OK / 3 FAIL
/ 2 CRASH (of 48) as the s52 baseline alone.

Trace on test 114 shows the sentinel **does fire** but PATBCL is
already `= inner-PATBCL` when the walker reaches it.  Some upstream
handler is already setting PATBCL=inner before the walker hits the
sentinel.  This refutes the session #54 hypothesis that "leaked
inner traps are dispatched under wrong (outer) PATBCL."  The actual
mechanism by which 114/119/124/129 break is something else.

Per RULES.md (no improvement = nothing to commit), the implementation
is reverted from working tree and saved as
`docs/F-2-Step3a-session56-strexccl-attempt.diff` so session #57 can
re-apply it as the starting point.

## What session #56 did

1. Sanity-check baseline at HEAD `1b2e28a` matches session #55's
   reported numbers exactly:
   - fence_function: 10/10
   - fence_suite: 40 OK / 2 FAIL / 6 CRASH (of 48), Tier F all 16
   - guard5: `inner backtrack worked`

2. Apply `docs/F-2-Step3a-session52-flpop-fix.patch`:
   - fence_suite improves to 43 OK / 3 FAIL / 2 CRASH
   - 119/129 still CRASH, 114/124/127 FAIL
   - guard5 still passes, Tier F still 16/16

3. Implement STREXCCL in `isnobol4.c` (C-only, no SIL/data_init/
   res.h touches — file-local static descriptors):
   - `XSTREX = 41` constant
   - Static `STREXFN_d`/`STREXCCL_d` descriptors lazily initialized
     via `STREXC_INIT()` macro on first STARP6/DSARP2 entry
   - New dispatch `case 41: goto L_STREXC;` in PATBRA switch
   - New `L_STREXC:` handler: `D(PATBCL) = D(YCL); goto L_SALT3;`
   - STARP6 and DSARP2 now `PUSH(PDLPTR); PUSH(YPTR);` after the
     existing 5 saves (entry-PDLPTR snapshot + inner-PATBCL = YPTR)
   - STARP2 success path pops the two extras and conditionally
     installs the STREXCCL trap with `slot[2] = inner_PATBCL`
     when `PDLPTR > entry_PDLPTR` (mirrors SPITBOL `p_nth` 's
     `beq xt,xs,pnth1` optimization)
   - STARP5 fail path pops and discards

4. Gates after STREXCCL:
   - guard5: `inner backtrack worked` ✓
   - Tier F: 16/16 ✓
   - fence_function: 10/10 ✓
   - fence_suite: **43 OK / 3 FAIL / 2 CRASH (of 48)** — IDENTICAL
     to s52-only baseline; no improvement on 119/129/114/124

5. Trace investigation on test 114:
   - Added env-gated `STREXC_TRACE`, `STREXC_TRACE2` instrumentation
     at install site, FIRE site, and SALT2 entry
   - Single trace event `STREXC: install at PDLPTR=...9b0
     entry=...950 innerpat=7f667bc0b9d0` followed by
     `STREXC: FIRE — restoring PATBCL=7f667bc0b9d0 (was 7f667bc0b9d0)`
   - **PATBCL was already = inner (cmd)** when STREXCCL fired
   - Trap installed at offset +12*8 = +96 bytes above entry-PDLPTR
     (4 descriptors of leaked content above the SCFLCL marker)

## Why STREXCCL alone is insufficient

The session #54 hypothesis was: "leaked inner traps are dispatched
under outer PATBCL because no DSAR redo trap routes the walker
through L_UNSC first; install a sentinel that switches PATBCL to
inner before the walker reaches them."

Trace evidence refutes this for test 114.  At the moment STREXCCL
fires (walker reaches the sentinel position), PATBCL is **already**
inner — meaning some prior handler set PATBCL=inner during the
walker's traversal of what should have been outer territory.

Candidate mechanisms that may set PATBCL=inner during walker
traversal (not yet bisected):

1. **L_FNCBX / L_STARP5 cstack POPs.**  Session #54 findings claimed
   these "automatically" restore outer PATBCL when the walker
   descends past the inner region.  But these are reached only via
   inner SCIN1's failure return (return code 1) — they're not on
   the walker's traversal path through PDL.  If they ARE somehow
   reached during outer SCIN1's failure walk, they'd pop the WRONG
   cstack frame (cstack is a separate stack from PDL, and the inner
   SCIN1's cstack frame was already balanced before its return).

2. **A leaked FNC-flagged trap (e.g. FNCBCL, FNCCCL stub) that
   re-routes through some L_UNSC-like path.**  CSNOBOL4 has FNCBCL
   and FNCCCL (cases 38, 39) which are D6-dead stubs that
   `BRANCH(FAIL)` — but if compilation still produces them and the
   walker dispatches them, behavior is undefined.

3. **The DSAR redo trap mechanism.**  When `*var` is dispatched
   first time via L_STAR, no redo trap exists.  But during walker
   traversal, an outer SCIN3 push for the `*cmd` token would have
   pushed a then-or descriptor pointing to L_DSAR.  When walker
   reaches that, it dispatches L_DSAR — which calls `D_A(PATICL) +=
   DESCR; D(YPTR) = D(D_A(PATBCL) + D_A(PATICL));` — reads pattern
   data using current PATBCL (still outer).  That's correct.  But
   then DSARP2 is reached, sets `D_A(UNSCCL) = 1`, calls SCIN1.
   The recursive SCIN1 sets `D(PATBCL) = D(YPTR)` = inner.  When
   THAT SCIN1 returns (success or failure), control returns to
   STARP2/STARP5 in the OUTER frame, which pops cstack restoring
   outer PATBCL.  So this path also restores correctly.

4. **A CHR/SCIN3 dispatch where PATICL/PATBCL relationship is
   tangled.**  Worth investigating with PATBCL-write logger.

The right next-session diagnostic is **a PATBCL-write logger**:
trap every C site that writes `D(PATBCL) = ...` or `D_A(PATBCL) =
...` (look at `L_UNSC`, `L_SCIN1A`, `L_STARP2/5` POPs, all the
cstack pops in pattern primitives).  Run test 114 with the logger.
Identify the write that sets PATBCL=cmd at the wrong moment.  That
write is the bug site.

## What was added/preserved this session

- **`docs/F-2-Step3a-session56-strexccl-attempt.diff`** — clean
  STREXCCL implementation (without trace instrumentation).
  `git apply` clean on top of the s52 patch on HEAD `1b2e28a`.
  Session #57 should apply both s52 and this diff as the starting
  point, then continue with the PATBCL-write logger.

- **`docs/F-2-Step3a-session56-findings.md`** — this file.

- **No runtime source changes committed.** Working tree clean.

## Key code excerpts (for future sessions)

The STREXCCL design lands as five edits to `isnobol4.c`:

### 1. Static descriptors (top of file, after `# include "parms.h"`)

```c
#define XSTREX (41)
static struct descr STREXFN_d[1];
static struct descr STREXCCL_d[1];
#define STREXFN  ((ptr_t)STREXFN_d)
#define STREXCCL ((ptr_t)STREXCCL_d)
static struct descr STREX_innerpat_d[1];
static struct descr STREX_entrypdl_d[1];
#define STREX_innerpat ((ptr_t)STREX_innerpat_d)
#define STREX_entrypdl ((ptr_t)STREX_entrypdl_d)
static int strexc_inited = 0;
#define STREXC_INIT() do { if (!strexc_inited) { \
    D_A(STREXFN) = (int_t)XSTREX;  D_F(STREXFN) = 0;   D_V(STREXFN) = (int_t)2; \
    D_A(STREXCCL) = (int_t)STREXFN; D_F(STREXCCL) = FNC; D_V(STREXCCL) = (int_t)2; \
    strexc_inited = 1; } } while (0)
```

### 2. Dispatch case 41 (in PATBRA switch)

```c
    case 40:
        goto L_FNCD;
    case 41:
        goto L_STREXC;
    }
```

### 3. L_STREXC handler (after L_FNCD)

```c
L_STREXC:
    D(PATBCL) = D(YCL);
    goto L_SALT3;
```

### 4. STARP6 — extra PUSH after existing 5

```c
    PUSH(YCL);
    STREXC_INIT();
    PUSH(PDLPTR);   /* entry-PDLPTR snapshot */
    PUSH(YPTR);     /* inner-PATBCL */
    D(MAXLEN) = D(NVAL);
    SAVSTK();
    switch (SCIN(NORET)) { ... }
```

### 5. STARP2 success path — POP and conditional install

```c
L_STARP2:
    POP(STREX_innerpat);
    POP(STREX_entrypdl);
    POP(YCL); POP(XCL); POP(PATICL); POP(PATBCL); POP(MAXLEN);
    if (D_A(PDLPTR) > D_A(STREX_entrypdl)) {
        D_A(PDLPTR) += 3*DESCR;
        if (D_A(PDLPTR) > D_A(PDLEND)) BRANCH(INTR31)
        D(D_A(PDLPTR) + DESCR) = D(STREXCCL);
        D(D_A(PDLPTR) + 2*DESCR) = D(STREX_innerpat);
        D(D_A(PDLPTR) + 3*DESCR) = D(LENFCL);
    }
    goto L_SCOK;
```

STARP5 fail path: same two POPs (discard), then existing 5 POPs.
DSARP2: same insertion of `PUSH(PDLPTR); PUSH(YPTR);` after
existing 5, before `D(MAXLEN) = D(NVAL); D_A(UNSCCL) = 1; SAVSTK();
switch (SCIN1(NORET))`.

## Gate-stack matrix as of session #56 end

| Gate | Baseline (`1b2e28a`) | + s52 patch | + STREXCCL |
|------|----------------------|-------------|------------|
| guard5 | ✓ inner backtrack worked | ✓ | ✓ |
| Tier F (16) | 16/16 | 16/16 | 16/16 |
| fence_function (10) | 10/10 | 10/10 | 10/10 |
| fence_suite (48) | 40/2/6 | 43/3/2 | 43/3/2 |
| beauty self-host | 35 lines (Parse Error) | 33 lines (SEGV) | not tested |

The s52 patch + STREXCCL combination preserves every regression-
prevention floor but does NOT advance fence_suite past the s52
baseline.  119/129 still CRASH with the same `PATICL=0xc0` signature
as session #54.  114/124 still FAIL.  127 FAIL is the known-bad-`.ref`
pre-existing pseudo-failure.

## Recommended session #57 plan

1. **Apply the combined patch as the starting point:**
   ```bash
   cd /home/claude/csnobol4
   git apply docs/F-2-Step3a-session56-strexccl-attempt.diff
   ```
   (The session #56 diff is self-contained — it includes both the s52
   flpop fix AND the STREXCCL implementation as a single 90-line patch
   against HEAD `1b2e28a`.  Verified to produce gates: guard5 ✓,
   fence_function 10/10, Tier F 16/16, fence_suite 43/3/2.)

2. **Add a PATBCL-write logger** — env-gated `_check_patbcl(site_id)`
   helper that prints every C site that writes `D(PATBCL)` or
   `D_A(PATBCL)`.  Find sites with:
   ```bash
   grep -nE 'D_A\(PATBCL\)\s*=|D\(PATBCL\)\s*=' /home/claude/csnobol4/isnobol4.c
   ```

3. **Run test 114 with the logger.**  Identify which write sets
   PATBCL=cmd at the moment STREXCCL fires.  That write is the
   problem — either it's a legitimate write that should not have
   happened, or it's a leaked save/restore that's mis-firing.

4. **Decide between:**
   - (a) **Fix the upstream write site** (if it's a bug) — STREXCCL
     may then become unnecessary or only fix the leftover few cases.
   - (b) **Augment STREXCCL** with a paired BOTTOM-of-region sentinel
     pushed BEFORE the inner SCIN call (between the existing 5 PUSHes
     and the STREXCCL pair) that switches PATBCL=outer when walker
     descends past the inner region.  This is the symmetric piece
     session #54 claimed was "automatic" — it isn't.

5. **Test gate stack (in order, stop at first regression):**
   - guard5 must produce `inner backtrack worked`
   - Tier F all 16 must remain PASS
   - fence_function 10/10
   - fence_suite total ≥45/48 (target 47/48 minus test 127 known-bad-ref)
   - Beauty self-host ≥500 lines

6. **Commit only if all gates pass.** Per RULES.md, regression in
   error class is unsafe to commit.  Save findings + diff if not
   committable.

## Honest circularity check

Session #56's genuine new contributions:

1. **First implemented STREXCCL.** Sessions #54/#55 designed it but
   neither implemented.  The implementation is clean (5 edits to one
   C file, ~50 lines total) and preserves every gate.  Saved as a
   reusable diff for session #57.

2. **Empirical refutation of session #54's hypothesis.** The trace
   evidence shows STREXCCL doesn't catch the bug class because
   PATBCL is already inner at the moment the walker reaches the
   sentinel.  Session #54's "wrong PATBCL on leaked traps" framing
   is incorrect — at least for test 114.  Some upstream write is
   setting PATBCL=inner during outer's failure walk.

3. **Concrete next-session diagnostic.** A PATBCL-write logger on
   ~10 known sites in isnobol4.c is a 1-hour task that should
   produce a specific named bug site.

What session #56 did NOT do:

- Did not advance beauty self-host.  Did not run beauty in this
  session (still at 35 lines from baseline).
- Did not implement the BOTTOM-of-region paired sentinel.  Worth
  trying next session if the PATBCL-write logger doesn't reveal a
  cleaner fix point.
- Did not port s52 or STREXCCL to `v311.sil`/`snobol4.c` — Step 3b.

## Pattern continues

Sessions #44–#56 = 13 sessions on F-2 Step 3a.  fence_function
preserved 10/10 throughout.  Tier F preserved 16/16 since session
#55.  fence_suite has graduated from 24/2/6 (s51 baseline) through
to 43/3/2 (s52, repeated by s56).  Beauty self-host stuck at 33–35
lines.

Each session has eliminated a wrong-fix candidate and/or sharpened
diagnosis.  Session #56's elimination: STREXCCL alone (the session
#54 design) is insufficient because the bug is upstream of where
the sentinel sits.

## Files added this session

- `csnobol4/docs/F-2-Step3a-session56-strexccl-attempt.diff` — the
  clean STREXCCL implementation as a reusable patch.
- `csnobol4/docs/F-2-Step3a-session56-findings.md` — this file.
- No runtime source changes committed.  No SIL changes.
