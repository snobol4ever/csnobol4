# Session #60 Final Handoff — Read This First in Session #61

## Where things stand

```
Last commit: ddc223e  (pushed to origin/main)
fence_suite: 44 OK, 4 FAIL, 0 CRASH (of 48)
beauty self-host: 42 lines
fence_function: 10/10
```

This is a real, committed milestone. All 6 CRASHes are eliminated. The
remaining 4 FAILs (119, 124, 127, 129) are wrong-answer (`unexpected match`),
not crashes. Build is clean.

## What landed this session

The session #58 STREXCCL sentinel patch was manually ported to HEAD (the
patch wouldn't apply because of file divergence). All sentinel machinery is
in `isnobol4.c`:

- STREXCCL globals (top of file, after `#include "parms.h"`)
- `case 41: goto L_STREXC` in PATBRA dispatch
- STARP6/STARP2/STARP5 — `PUSH(PDLPTR); PUSH(YPTR)` and sentinel install/discard
- DSARP2 — symmetric SCFLCL push + STREX_entrypdl/innerpat saves
- FNCA seal slot[2] = entry-PDLPTR (s52 form)
- FNCD — flpop (subtract 3*DESCR after rewind)
- L_STREXC handler — `D(PATBCL) = D(YCL); goto L_SALT3`

This eliminated all 6 CRASHes. The remaining 4 FAILs are a separate bug.

## What did NOT work this session (do not repeat)

Three fix attempts for the 4 remaining FAILs were all tried and all failed.
They have been backed out — current `isnobol4.c` is clean s58 state.

### Attempt 1: `goto L_TSALF` in FNCD instead of `BRANCH(FAIL)`
SALT2 trace showed walker still descended into garbage memory below the PDL.
The `L_TSALF` is just `LENFCL=0; goto SALT2` — same loop, doesn't change exit.

### Attempt 2: Deferred-seal via `fnca_seal_pending` flag
Added globals `fnca_seal_pending` and `fnca_seal_lenfcl`. FNCA success sets
flag instead of pushing seal. STARP2 checks flag and pushes seal at PDL top
(above STREXCCL sentinels). With trace, confirmed: STARP2 DID install seal
correctly — but FNCD never fired. Wrong-answer persisted.

### Attempt 3: In-place overwrite of SCIN3-FENCEPT trap at P0
After `POP(PDLPTR); D_A(PDLPTR) -= 3*DESCR` lands at P0 (= entry-PDLPTR =
SCIN3-FENCEPT trap position). Overwrite slot[1] with FNCDCL function. With
trace, FNCD STILL never fired. Wrong-answer persisted.

**Conclusion:** the alternation trap that produces the wrong match is at a
PDL position the seal cannot reach via any of these mechanisms. Where it
lives is unknown without a fresh full-PDL trace.

## What session #61 must do FIRST: full PDL dump

Before attempting any fix, instrument `isnobol4.c` to dump the entire PDL
contents at three checkpoints:

1. Right after FNCA success (before `goto L_SCOK`)
2. Right after STARP2 (before `goto L_SCOK`)
3. Inside SALT2, every iteration

For each PDL slot show: address, slot[1].A_addr, slot[1].A_flags, slot[2].A_addr.
Identify each slot[1] as one of: SCFLCL, FNCDCL, STREXCCL, FNCAFN, BAL_function,
ALT_continuation, etc.

Cross-reference: known function addresses can be printed at INIT (search
isnobol4.c for `D_A(SCFLCL)`, `D_A(FNCDCL)`, etc., pull their value at runtime
and print `legend: SCFLCL=0x... FNCDCL=0x... ...`).

Run on test 119 (smallest repro: `s='aab', cmd=FENCE('a'|'ab'), outer=ARBNO(*cmd)`,
match `*outer RPOS(0)`). The trace will show:

- Where the alternation continuation trap (`'a'|'ab'`) actually lives in the PDL
- Whether it's pushed by the OUTER SCIN3 (processing `*cmd` in the outer pattern)
  or by the INNER SCIN (processing `'a'|'ab'` for FNCA)
- Whether SCNR's `D(PDLPTR) = D(PDLHED)` resets the PDL between iterations

Once the alternation trap's location is known, the fix is one of:
- (a) FNCA at success path overwrites THAT slot (not P0) with FNCDCL
- (b) STARP2's STREXCCL top sentinel position needs adjustment to wrap it
- (c) A different mechanism entirely (e.g., SCFLCL needs a flag bit)

## Critical traps to dump

```c
fprintf(stderr, "PDL legend: SCFLCL=%lx FNCDCL=%lx STREXCCL=%lx FNCAFN=%lx FNCBFN=%lx\n",
    (long)D_A(SCFLCL), (long)D_A(FNCDCL), (long)D_A(STREXCCL),
    (long)D_A(FNCAFN), (long)D_A(FNCBFN));
```

Then in SALT2:
```c
fprintf(stderr, "SALT2@%p: slot1.A=%lx slot1.F=%x slot2.A=%lx",
    (void*)D_A(PDLPTR), (long)D_A(XCL), (int)D_F(XCL), (long)D_A(YCL));
if (D_A(XCL) == D_A(SCFLCL)) fprintf(stderr, " [SCFLCL]");
else if (D_A(XCL) == D_A(FNCDCL)) fprintf(stderr, " [FNCDCL]");
else if (D_A(XCL) == D_A(STREXCCL)) fprintf(stderr, " [STREXCCL]");
else if (D_F(XCL) & FNC) fprintf(stderr, " [FNC dispatch]");
else if (D_A(XCL) != 0) fprintf(stderr, " [ALT cont? offset=%ld]", (long)D_A(XCL));
fprintf(stderr, "\n");
```

## Build commands

```bash
cd /home/claude/csnobol4
touch isnobol4.c   # force rebuild after edit
make -f Makefile2 xsnobol4 OPT="-O0 -g" 2>&1 | grep -E "error:|Error" | head -3
cp xsnobol4 snobol4

# Single test
timeout 5 ./snobol4 -bf /home/claude/corpus/crosscheck/patterns/119_pat_arbno_of_fence_via_var_via_outer.sno

# fence_suite gate
cd test/fence_suite && make csnobol4 SNOBOL4=/home/claude/csnobol4/snobol4

# beauty self-host
cd /home/claude/corpus/programs/snobol4/demo/beauty
SNO_LIB=. /home/claude/csnobol4/snobol4 -bf -P64k -S64k beauty.sno < beauty.sno | wc -l
```

## Repository state

- `snobol4ever/csnobol4` HEAD: ddc223e
- `snobol4ever/.github` HEAD: unchanged
- `snobol4ever/corpus` HEAD: unchanged
- `philbudne/csnobol4` is now wired as a remote (added this session) — vanilla
  upstream 2.3.4, has no FENCE(P) work; useful for tracking Phil's changes

## Honest assessment

**Sessions #44–#60 progress (16 sessions):**
- CRASHes: 6 → 0 ✓ (real progress)
- fence_suite: 24/2/6 → 44/4/0 (+20 tests pass)
- beauty: 35 → 42 lines (+7 lines)
- Bug class shifted: memory corruption → wrong-answer semantics

**Sessions #44–#60 NOT progress:**
- Beauty done-when is ≥500 lines. We are at 42. That gap is huge.
- 16 sessions of diagnosis without closing the FENCE(P) bug
- The architectural mismatch (CSNOBOL4 PDL-relative offsets vs SPITBOL pointers)
  may not be solvable by patching FNCA/FNCD alone — it may require a deeper
  redesign of how SCIN3 and SALT2 interact when leaked entries exist

**Strategic question worth raising with the user:** is closing this bug worth
more sessions, or should the goal pivot? Possible pivots:
- Remove FENCE(P) from beauty.sno entirely (Plan B route)
- Use SPITBOL exclusively for beauty self-host
- Accept 44/4/0 and document the remaining bug as a known limitation

The traced data is concrete and the fix direction is clear (in-place SCIN3
overwrite at the right slot). One more session of focused PDL-dumping work
should close it. But the user has been patient through 16 sessions — this
deserves an explicit checkpoint conversation.

## Files updated this session

- `isnobol4.c`: STREXCCL sentinels + s52 flpop (committed in 9cfb8fa)
- `docs/F-2-Step3a-session60-findings.md` (committed in 9cfb8fa)
- `docs/F-2-Step3a-session61-handoff.md` — the original handoff (committed in a883e77)
  then updated with "in-place SCIN3 overwrite" finding (committed in ddc223e)
- This file: final wrap-up handoff
