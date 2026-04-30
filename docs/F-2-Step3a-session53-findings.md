# F-2 Step 3a, session #53 findings

**Goal entering session:** continue toward GOAL-CSN-FENCE-FIX. Confirm
session #52's flpop patch, drive fence_suite past 27/3/2, ideally fix
beauty stmt-1074 crash.

**Goal at end of session:** the fix is *not* yet implemented, but the
crash is now diagnosed at the level of individual PDL slot writes/reads,
and a sharper architectural view of *what* needs to change has been
established. The naïve "rewind STARP2's PDLPTR" idea was tried and
**regressed** — that result is itself useful information.

## Summary of work done

### 1. Session #52 patch verified, two .ref files corrected

The session #52 patch (`docs/F-2-Step3a-session52-flpop-fix.patch`)
applies cleanly. After applying:

* `test/fence_function`: **10 / 0 / 0** (preserved)
* `test/fence_suite` baseline: 24 / 2 / 6
* `test/fence_suite` after also correcting bad .ref files for tests 127
  and 130: **27 / 3 / 2**

Tests 127 and 130 had wrong expected outputs — verified by running them
under SPITBOL (which is the oracle). The corrected .ref files match
SPITBOL exactly. SPITBOL itself now passes the suite **32 / 0 / 0**, so
the suite is correctly calibrated.

The corrected .ref files are present in the corpus working tree, **not
yet committed**:

```
 M crosscheck/patterns/127_pat_json_keyvalue.ref      "k=age s=\"age\":42 n=42 b="
 M crosscheck/patterns/130_pat_two_star_fence_concat_outer.ref     "fail"
```

### 2. SPITBOL `p_arb` / `p_aba` / `p_abc` / `p_abd` and snobol4dotnet read

* SPITBOL's ARBNO uses **pmhbs swapping**: `p_aba` saves the outer
  history-stack base and sets a fresh inner one for the iteration;
  `p_abc` (success) restores the outer base; `p_abd` (failure) restores
  the inner base then fails. The "stack base swap" is what gives
  isolation across iterations.

* snobol4dotnet implements FENCE as `Concat(p, SealPattern())`, exactly
  Gimpel's Fig. 11 shape. `SealPattern.Scan()` calls
  `scan.SealAlternates()`, which clears the **per-Match alternate
  stack** and pushes a sentinel. The .NET design avoids CSNOBOL4's
  PDL-leak class entirely because:
    - `*var` doesn't recursive-SCAN — `UnevaluatedPattern` *grafts* the
      evaluated AST into the running scanner and uses GOTO.
    - The alternate stack is per-Match-call, not global.
  Different shape, same semantic goal. Cannot be ported directly.

### 3. Crash root-cause traced to a single PDL slot

A diagnostic trace was added (env-gated `FENCE_TRACE`, **already
reverted from working tree** — verified by `diff` against the
session-#52 patch). The trace produced 89 events on test 119; full
analysis below.

**Crash signature** (gdb):
```
SCIN1 at isnobol4.c:11456
PATICL = {a.i = 560 (0x230), v = 96}
ZCL = NULL  →  D(D_A(ZCL)) = D(0)  →  SEGV
```

**Causal chain:**
1. ft0020: SCIN3 push at PDL address `...a90`. PATBCL = `...d610` (the
   inner pattern: `outer = ARBNO(*cmd)`). slot[1] = 0x200 — a
   then-or PATICL offset valid only relative to PATBCL=`...d610`.
2. ft0023: STARP2 success, returning from `*outer`'s recursive SCIN.
   PDLPTR = `...a60`. PATBCL switches back to `...dab0` (the
   outermost pattern `s POS(0) *outer RPOS(0)`).
   **STARP2 does not rewind PDL.** The slot at `...a90` still holds
   `0x200` from ft0020.
3. ft0024–ft0085: outer pattern continues, much pushing and popping;
   PDLPTR descends as low as `...a30` and climbs back to `...a60`. **No
   write reaches `...a90`** in this window.
4. ft0086: outer pattern fails. SALT2 reads slot[1] at PDLPTR+DESCR =
   `...a90` → gets stale `0x200`. PATICL ← `{a.i=512}`. Non-zero,
   non-FNC → falls into L_SCIN3.
5. L_SCIN3 advances PATICL, reads pattern at `D(PATBCL+560)` —
   PATBCL is now `...dab0`, which is small. The read returns NULL
   (zeroed memory past pattern end).
6. SCIN1:11456 dereferences NULL → SEGV.

**This is the PATBCL-context-mismatch session #50 hypothesized**, now
witnessed at the level of individual PDL slot addresses. CSNOBOL4
stores then-or trap slot[1] = small integers that are PATICL offsets
relative to the current PATBCL at push time. SPITBOL stores slot[1] =
function pointers (pcode addresses) which are absolute and
self-dispatching. CSNOBOL4's slot[1] is contextually scoped; SPITBOL's
is global. **That is the architectural difference, exposed by FENCE.**

### 4. Naïve rewind in L_STARP2 — tried and FAILED

The intuitive fix: mirror FNCA's success-path PDLPTR-rewind in L_STARP2.

* L_STARP6: `PUSH(PDLPTR)` (between `PUSH(YCL)` and `D(MAXLEN) = D(NVAL)`)
* L_STARP2: `POP(PDLPTR); D_A(PDLPTR) -= 3*DESCR;` (before existing pops)
* L_STARP5: `POP(TMVAL)` (discard, since failure walker already
  rewound PDLPTR via the SCFLCL trap consumption)
* L_DSARP2: also push PDLPTR (cstack balance) — DSAR routes through
  STARP2 / STARP5

**Result:** fence_suite **regressed** from 27/3/2 to 24/2/6. Tests
109, 113, 114, 130 went from OK → CRASH.

**Why it regressed:** for `*var → FENCE` directly (test 109), the inner
SCIN exits with FNCDCL on top of PDL. STARP2's rewind discards
FNCDCL — the very seal that makes session #52 work.

**Lesson:** STARP2 cannot blindly rewind. FNCDCLs (legitimate, placed
explicitly by FNCA) must survive; only **then-or leaks** under inner
PATBCL must be cleaned.

## The architectural picture (sharpened)

The shared PDL across recursive SCIN levels is the design flaw:

* **SPITBOL:** xs (history stack) is shared across all SCIN levels, but
  trap slot[1] is a `pcode` (function pointer). Dispatch is via
  function-pointer call — PATBCL doesn't matter. So when xs entries
  outlive their pushing scope, dispatch still works.
* **CSNOBOL4:** PDL is shared. Trap slot[1] is `XCL = D(PATBCL+PATICL)`
  — i.e. a *byte-copy* of the dispatch tag from the pattern node. For
  most primitives (BREAK, ANY, …) the descriptor's `f` byte has the
  FNC flag set and `a.i` is a function pointer — these are
  context-independent. **For then-or / alternation traps, slot[1] is
  a small integer (PATICL offset) with `f=0`** — and *that* is
  context-dependent. So the leak class is specifically: then-or traps
  pushed under inner PATBCL, surviving past their SCIN's exit.

`*var → FENCE` directly works (test 109 with session-#52 patch) because
the seal is FNCDCL — function descriptor with FNC flag — globally
dispatchable. The seal mechanism does *not* rely on PATBCL context.

`*var → ARBNO(*var → FENCE)` (test 119) breaks because the inner
recursive SCIN evaluating the ARBNO pattern leaves then-or traps for
ARBN's loop-control, and those *do* rely on PATBCL context. They leak
across STAR's success boundary and get re-walked under outer PATBCL.

## What the fix needs to do

The right shape:

> **On STAR's recursive-SCIN success, walk PDL from STARP6-entry up to
> the current PDLPTR. For each trap entry whose slot[1] does NOT have
> the FNC flag set, zero out slot[1] (or replace with SCFLCL or some
> neutral fail-marker) so SALT2 takes a clean failure path on it.
> Trap entries with FNC flag (FNCDCL, SCFLCL, BAL primitives, etc.)
> are PATBCL-independent and may safely remain.**

This is essentially the goal-file's Candidate A from a different
angle. Not blind rewind — *targeted* zeroing of the
context-dependent slots.

Open design questions for whoever picks this up:

1. Is "zero slot[1]" the right neutral value? `SCFLCL` (= FAIL when
   dispatched) might be safer — it terminates the failure walk cleanly
   rather than risking SALT3 misbehaving on a zero slot.
2. How does the loop bound STARP6-entry-PDLPTR? It needs to be saved
   somewhere — either on cstack (PUSH(PDLPTR) at STARP6 entry, then
   walk, then POP at STARP2/STARP5), or via a dedicated register.
   PDLPTR-on-cstack is the obvious choice; that part of the regressed
   patch was correct, only the action at STARP2 was wrong.
3. Does L_DSARP2 (Deferred STAR) have the same leak class? Logically
   yes: it calls SCIN1 which pushes traps under inner PATBCL. But its
   mechanism differs (no SCFLCL push of its own). The traces showed
   DSARP2 firing in test 119 at multiple points. Worth implementing
   the same zeroing pass for DSAR's success path.
4. Is L_STARP5 (failure path) correct as-is? Per the trace, the
   failure walker has *already* rewound PDLPTR by consuming SCFLCL,
   so STARP5 sees a clean PDL. Yes — leave it alone except to
   pop-and-discard the cstack-saved PDLPTR for balance.

## Suggested next steps

1. Add cstack `PUSH(PDLPTR)` at L_STARP6 entry (after the existing 5
   pushes) and at L_DSARP2 (similarly).
2. Add a small helper at L_STARP2 / L_DSARP2 success path: read saved
   PDLPTR from cstack into a temp, then walk from `temp+3*DESCR`
   (skipping STARP6's SCFLCL slot) up to current PDLPTR in 3*DESCR
   strides. For each step, if `D_F(D_A(PDLPTR_saved+i)+DESCR) & FNC == 0`,
   set `D_A(D_A(PDLPTR_saved+i)+DESCR) = 0` so SALT2 takes the SALT3
   path on it.
   Note that L_DSARP2 has no SCFLCL of its own, so no `+3*DESCR`
   skip there.
3. At L_STARP5 / DSAR-failure path, just `POP(TMVAL)` to discard.
4. Run regression cycle:
   - fence_function must stay 10/10
   - fence_suite must stay ≥ 27/3/2 (target ≥ 30/30, ideal 32/32)
   - beauty self-host must stay ≥ 33 lines (target: ≥ 500)
5. If fence_suite hits 32/32 and beauty stays alive past 500 lines,
   commit and hand off. If not, the trace evidence in this document
   is rich enough to localize the next bug without restarting from
   scratch.

## Reproducibility

Minimal repro for the crash class (test 119 stripped):

```snobol
        cmd = FENCE('a' | 'ab')
        outer = ARBNO(*cmd)
        s = 'aab'
        s POS(0) *outer RPOS(0)                               :S(BAD)F(GOOD)
BAD     OUTPUT = 'unexpected match'                           :(END)
GOOD    OUTPUT = 'beauty-class FENCE seal held'
END
```

Expected output (per SPITBOL): `beauty-class FENCE seal held`
Current csnobol4 output: `Segmentation fault`

To reproduce the trace, the diagnostic FENCE_TRACE instrumentation
described in this document can be re-applied. The instrumentation
points are:

* L_SALT2 entry: print PDLPTR, PATBCL, slot[1], slot[2]
* L_STARP6 entry: print PDLPTR, PATBCL
* L_STARP2 entry: print PDLPTR
* L_STARP5 entry: print PDLPTR
* L_DSARP2 entry: print PDLPTR, PATBCL
* L_SCIN3 entry (first line of the L_SCIN3 block): print PDLPTR,
  PATBCL, PATICL_in
* L_FNCA entry: print PDLPTR, PATBCL, PATICL, PDLHED
* FNCA success exit (after slot[3] = LENFCL write, before goto L_SCOK):
  print PDLPTR, inner_base
* L_FNCBX entry: print PDLPTR
* L_FNCD entry: print PDLPTR, YCL, PATBCL
* L_FNCD exit (after `PDLPTR -= 3*DESCR` and `D(NAMICL)=D(NHEDCL)`):
  print PDLPTR

All FTRACE calls must include `fflush(stderr)` for clean output at
SEGV. The `__ftrace_seq` counter helps cross-reference events.

## Tree state at handoff

* `csnobol4/isnobol4.c`: session #52 patch applied (verified by `diff`
  against the patch file — exact match). No instrumentation residue.
* `csnobol4/snobol4_debug`: untracked binary, leftover from earlier
  debug build; safe to delete.
* `corpus/crosscheck/patterns/127_pat_json_keyvalue.ref`: corrected
  to match SPITBOL output.
* `corpus/crosscheck/patterns/130_pat_two_star_fence_concat_outer.ref`:
  corrected to match SPITBOL output.

Test counts at handoff:
* fence_function: 10 / 0 / 0
* fence_suite: 27 / 3 / 2 (with corrected .refs)
* beauty: 33-line crash at stmt 1074 (unchanged)

## Files of interest

* `/home/claude/csnobol4/isnobol4.c` lines 12174–12270: STAR / DSAR /
  STARP2 / STARP5 — the proposed-fix-site
* `/home/claude/csnobol4/isnobol4.c` lines 12258–12390: FNCA / FNCBX /
  FNCD — already correct after session #52
* `/home/claude/csnobol4/isnobol4.c` lines 11447–11500: SCIN3 / SALT2 —
  the push and walk sites
* `/home/claude/x64/sbl.min` lines 11610–11750: SPITBOL p_aba / p_abc /
  p_abd / p_arb — the analog architectures
* `/home/claude/snobol4dotnet/Snobol4.Common/Runtime/Pattern/SealPattern.cs`:
  the .NET FENCE implementation as a reference shape
* `Gimpel 1973` (uploaded PDF), Fig. 9 (ARBNO LFT) and Fig. 11
  (STAR/RESTAR): the canonical theoretical compounds
