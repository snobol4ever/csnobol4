# F-2 Step 3a session #65 (continued) — L_FNCD bug isolated via attribute-grid analysis

**Date:** 2026-05-01 (continued from session #65 first commit `5fbf2ce`)
**Status:** Attempt saved as patch artifact, NOT committed (regression in error class).

## What was found

After session #65's suite expansion to 53 tests (commit `5fbf2ce`),
attribute-grid analysis of the 7 csnobol4 FAILs vs. 46 OKs surfaced a
single discriminating shape that ALL 7 failures share and ZERO of the
46 passes have:

> At the moment FNCDCL fires, the PDL contains alternative-traps both
> BELOW the seal-base (legitimate outer alts that must be tried) AND
> ABOVE-but-physically-still-present (leaked inner FENCE alts that must
> NOT be tried).

The 7 FAILs partition into two clusters with opposite symptoms:

- **Cluster A** (119, 129, 148, 149): walker incorrectly *matches* —
  dispatches a leaked inner-FENCE alt as if it were legitimate.
- **Cluster B** (124, 127, 152): walker incorrectly *fails* — exits the
  scan instead of falling through to a legitimate outer alt.

Both symptoms originate from the same line: `L_FNCD: BRANCH(FAIL)` at
`isnobol4.c:12437`.

## SPITBOL comparison

`p_fnd` at `sbl.min:12044`:
```
p_fnd  ent  bl_p0            p0blk
       mov  xs,wb            pop stack to fence() history base
       brn  flpop            pop base entry and fail
```

`flpop` at `sbl.min:16242`:
```
flpop  rtn
       add  xs,*num02        pop two entries off stack
       ejc                   (drops into failp)
```

`failp` at `sbl.min:16256`:
```
failp  rtn
       mov  xr,(xs)+         load alternative node pointer
       mov  wb,(xs)+         restore old cursor
       mov  xl,(xr)          load pcode entry pointer
       bri  xl               jump to execute code for node
```

So SPITBOL's seal handler `p_fnd` does NOT exit the scan — it pops
entries to the inner base, then `failp` POPS THE NEXT ALT FROM THE
STACK AND DISPATCHES IT.  Outer alts that were on the stack BEFORE
FENCE was entered remain dispatchable; FENCE-internal alts are
discarded by the `mov xs,wb` truncation.

csnobol4's `BRANCH(FAIL)` is `return 1` from SCIN — it exits the
entire scan and skips ALL remaining alts, both inner and outer.  This
is the bug behind cluster B.

## Minimal repro

Smallest demonstration of the cluster-B half of the bug:

```snobol
        s = 'iffoo'
        s POS(0) (FENCE('if') | SPAN('abcdefghijklmnopqrstuvwxyz')) RPOS(0)  :S(Y)F(N)
Y       OUTPUT = 'matched'                                    :(END)
N       OUTPUT = 'failed'
END
```

SPITBOL: `matched` (FENCE matches `'if'`, RPOS(0) fails, walker falls
through the `|` alt to SPAN, SPAN matches `'iffoo'`, RPOS(0) succeeds).

csnobol4 baseline: `failed` (FENCE matches, RPOS(0) fails, walker hits
FNCDCL, BRANCH(FAIL) exits the entire scan).

## Attempted fix

Replace `BRANCH(FAIL)` with `goto L_TSALT` in `L_FNCD`.  L_TSALT is the
csnobol4 analog of SPITBOL's `failp`: it pops the next PDL entry and
dispatches via PATBRA.  The existing `D_A(PDLPTR) -= 3*DESCR` step is
the analog of `flpop`'s pop-2-entries-then-failp.

Saved as `docs/F-2-Step3a-session65-L_FNCD-attempt.diff` (one-line
change, applies cleanly to `isnobol4.c` at HEAD `5fbf2ce`).

## Why the attempt was rejected

Result with the change applied:

| Test | Baseline | s65-L_FNCD attempt |
|------|----------|-------------------|
| 124 (cluster B) | FAIL | **OK** ✓ |
| 119, 129, 148, 149 (cluster A) | FAIL | FAIL (unchanged) |
| 127, 152 (cluster B variants) | FAIL | FAIL (unchanged) |
| **150 (negative discriminator)** | **OK** | **FAIL** ✗ |
| guard5 | OK | OK |
| fence_function | 10/10 | 10/10 |

Test 150 regressed from OK to FAIL — that's a regression in error class
(was a passing negative discriminator, now matches incorrectly).  Per
RULES.md, this is not safe to commit.

The reason: removing `BRANCH(FAIL)` lets the walker reach LEAKED inner
alts that were physically still on PDL above PDLPTR-after-rewind.
`BRANCH(FAIL)` was over-correcting (blocked all alts including the
legitimate outer ones), but its over-correction was masking the
underlying leak issue.  Cluster A's failures and 150's regression both
come from leaked alts being dispatched.

## What this means for the fix

The `L_FNCD: BRANCH(FAIL) -> goto L_TSALT` change IS architecturally
correct and IS necessary.  But it must be COMPOSED with one of:

- **(a) Physical leak removal at FNCA-success.** Walk PDL from inner-base
  +3*DESCR to old_PDLPTR (top of leaks); zero slot[1] of any non-FNC
  entry.  Session #64 proposed this; it was the next runtime-fix attempt
  before session #65's audit.
- **(b) STREXCCL bottom-sentinel that stays in place across iterations.**
  Session #58 implemented top-and-bottom STREXCCL; that was insufficient
  because session #57 identified the multi-iteration ARBNO leak class.
  A persistent bottom sentinel might compose with the L_FNCD fix.

Either (a) or (b) prevents the walker from reaching leaked alts; the
L_FNCD fix lets it reach legitimate ones.  Together they should resolve
cluster A AND cluster B AND keep 150 passing.

## Plan for session #66

1. Apply `docs/F-2-Step3a-session65-L_FNCD-attempt.diff`.
2. Implement option (a): leaked-alt zeroing at FNCA-success.  Pseudocode
   given in goal-file session #64 update.
3. Run full gate stack:
   - guard5: must produce `inner backtrack worked`
   - fence_function: 10/10
   - fence_suite SPITBOL: 53/0/0 (clean)
   - fence_suite csnobol4: target ≥51/53 (only 127/152 left as bug 2)
   - Tier F: 16/16
   - beauty self-host: ≥500 lines
4. If 150 passes AND 119/124/129/148/149 all flip OK: commit.
5. If 150 regresses: try option (b) as alternative composition.
6. If neither composition works without regressing 150: document and
   redirect to investigate why (a)/(b) fail to remove the dispatchable
   leaked alts.

## Verification commands for session #66

```bash
# Apply the L_FNCD fix
cd /home/claude/csnobol4
git apply docs/F-2-Step3a-session65-L_FNCD-attempt.diff

# Verify it applied
grep -A2 "^L_FNCD:" isnobol4.c | head -8
# Should show: goto L_TSALT; instead of BRANCH(FAIL)

# Then implement (a) or (b) and rebuild
make -f Makefile2 xsnobol4 && cp xsnobol4 snobol4

# Gates
cd test/fence_suite && make csnobol4 | tail -2
cd test/fence_function && make | tail -1
```

## Files this session-continuation

- `csnobol4/docs/F-2-Step3a-session65-L_FNCD-attempt.diff` (1-line patch)
- `csnobol4/docs/F-2-Step3a-session65-L_FNCD-findings.md` (this file)
- No source changes committed.  Working tree clean except for these two
  docs files.
