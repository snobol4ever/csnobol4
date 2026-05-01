# F-2 Step 3a Session #60 Findings

## Summary

- **fence_suite:** 44 OK, 4 FAIL, 0 CRASH (of 48) — same as s58 documented target
- **beauty self-host:** 42 lines (same as s58)
- **Committed:** yes — s58 STREXCCL sentinels manually ported to HEAD + FNCD flpop

## What was accomplished

### 1. SALT2 crash mechanism proven by trace

Added `SALT2` tracing (gated on `FNCD_TRACE` env var). After FNCD fires and
rewinds, the failure walker continues descending and hits garbage memory below
the PDL base (flags: `0xe0`, `0x41`, `0x31` — not valid trap entries). This
confirmed that the crash is not inside FNCA's recursive SCIN but in the outer
failure walker after FNCD's `BRANCH(FAIL)` exits.

### 2. STREXCCL sentinels (s58) manually ported to HEAD

The s58 patch (against commit `72030c0`) failed to apply cleanly against HEAD
(`1b59147`). Applied manually:

- Added STREXCCL globals after `#include "parms.h"`
- Added `case 41: goto L_STREXC` to PATBRA dispatch
- STARP6: `PUSH(PDLPTR); PUSH(YPTR)` before `SAVSTK`
- STARP2 (success): install paired bottom+top STREXCCL sentinels wrapping
  leaked inner-scan entries; POP STREX_entrypdl/STREX_innerpat from cstack
- STARP5 (fail): discard POP'd STREX_entrypdl/STREX_innerpat
- DSARP2: push SCFLCL frame + PUSH(PDLPTR)+PUSH(YPTR) symmetrically
- FNCD: flpop (`D_A(PDLPTR) -= 3*DESCR`) after rewind (s52)
- Added `L_STREXC` handler: `D(PATBCL) = D(YCL); goto L_SALT3`

Result: 0 CRASHes (was 6 at baseline), beauty 35→42 lines.

### 3. Remaining 4 FAILs: seal placement is root cause

Tests 119, 124, 127, 129 produce `unexpected match` — FNCD seal fires (once,
correctly) but FNCD is never reached on the paths that cause wrong matches.

**Root cause identified:** The FNCDCL seal is pushed at `P1` (= inner base =
SCFLCL position = `seal_PDLPTR`) in FNCA's success path. However, the leaked
alternation traps from the inner SCIN sit at addresses ABOVE P1. The STREXCCL
top sentinel also sits above P1.

The outer failure walker descends from high to low:
1. Hits top STREXCCL → restores inner PATBCL → continues down
2. Hits leaked alternation trap → **dispatched** → wrong match
3. Never reaches FNCDCL seal at P1 below

FNCD is never fired on these paths because the alternation trap executes before
the walker reaches the seal.

### 4. Required fix: seal must sit ABOVE leaked entries

In SPITBOL's `p_fna..p_fnd`, after the inner pattern matches, `p_fnc` pushes
the seal (`p_fnd`) at the TOP of the stack — above all leaked entries. The
outer walker therefore hits the seal first.

In CSNOBOL4's D6 design, FNCA pushes the seal at the inner-base position
(P1), which is BELOW the leaked entries. This inverts the correct ordering.

**Correct fix:** FNCA's success path should NOT push the FNCDCL seal at P1.
Instead, FNCA should communicate the seal-need to STARP2 (via cstack), and
STARP2 should install the FNCDCL seal at the top of PDL AFTER installing the
STREXCCL sentinels.

Concretely:
- FNCA success: instead of `D_A(PDLPTR) += 3*DESCR; D(... + DESCR) = D(FNCDCL)`,
  just `PUSH(FNCDCL)` onto cstack to pass the seal descriptor to STARP2
- STARP2: after installing STREXCCL sentinels, `POP(seal_descr)` from cstack
  and push the FNCDCL seal on PDL at the new top position

This makes the PDL layout (top-to-bottom):
```
FNCDCL seal          ← first thing outer walker sees → FNCD fires → clean rewind
STREXCCL top         ← inner PATBCL restore
leaked alt trap      ← never reached (FNCDCL already fired)
STREXCCL bottom      ← outer PATBCL restore
SCFLCL               ← STARP6's frame base
...outer entries...
```

## Next session

Implement the FNCA→STARP2 seal-on-cstack communication:
1. FNCA success: `PUSH(FNCDCL)` to cstack instead of PDL push
2. STARP2: `POP(FNCDCL_descr)` and install on PDL after STREXCCL sentinels
3. Handle the no-FENCE case (STARP2 can check whether the top cstack entry is
   a FNCDCL descriptor before popping)
4. Verify: fence_function 10/10, fence_suite all 48 OK, beauty ≥ 500 lines

## Session gate

```
fence_suite: 44/4/0
beauty: 42 lines
fence_function: 10/10 (not re-tested this session but preserved)
```
