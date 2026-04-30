# F-2 Step 3a — session #58 findings (paired top+bottom STREXCCL sentinels)

GOAL-CSN-FENCE-FIX F-2 Step 3a continuation (session #58, 2026-04-30).

## TL;DR

Session #58 read SPITBOL `p_exa`/`p_nth`/`p_exb`/`p_exc` and `p_aba`/`p_abb`/
`p_abc`/`p_abd` (sbl.min:11600-11700, 11920-11990, 12200-12270) carefully,
identified the architectural mismatch between SPITBOL's `pmhbs` per-level
history-stack-base mechanism and CSNOBOL4's failure walker (which has the
analog `PDLHED` declared but does NOT bound SALT2/SALT3 against it — only
BAL's walker does, at v311.sil:3975).

Implemented session #57's candidate (b refined): paired top + bottom
STREXCCL sentinels at every STARP6/DSARP2 leak region.

**Result: ALL CRASHES ELIMINATED in fence_suite.** Bug class shifted from
memory corruption (CRASH) to semantic incorrectness (FAIL).  Per RULES.md
"regression in error class is unsafe to commit" — but here every error
class moves toward CORRECTNESS:

| | Baseline | s56 alone | **s58** |
|---|---|---|---|
| fence_function | 10/10 | 10/10 | **10/10** |
| Tier F | 16/16 | 16/16 | **16/16** |
| guard5 | OK | OK | **OK** |
| fence_suite | 40/2/6 | 43/3/2 | **44/4/0** |
| beauty self-host | 35 lines | 35 lines | **42 lines** |

The 4 FAIL in fence_suite are 119, 124, 127, 129.  Of these:
- 119, 129 — went from **CRASH to FAIL** (improvement; still wrong-answer)
- 124, 127 — pre-existing baseline FAILs (unchanged)

Patch is **NOT committed** because 119/129 give wrong semantic answer
(`unexpected match` instead of `triple-indirect FENCE sealed`).  Saved as
`docs/F-2-Step3a-session58-paired-strexc-attempt.diff` (195 lines,
self-contained against HEAD `8ebab64`, verified to reproduce all gates).

## What session #58 did

### 1. SPITBOL reading

Read four code regions:
- **`p_exa`/`p_exb`/`p_exc`** at sbl.min:11920-12000 — the `*expr`
  expression-pattern entry/walker/sentinel triple
- **`p_nth`** at sbl.min:12213 — end-of-inner-pattern-match success exit
- **`p_aba`/`p_abb`/`p_abc`/`p_abd`** at sbl.min:11610-11665 — ARBNO
  4-routine compound mechanism
- **`flpop`** at sbl.min:3144 + 16234 — fail-and-pop

Key insights:

1. SPITBOL maintains `pmhbs` (pattern-match-history-base-stack) **as a
   pointer to a stack base**, not a sentinel entry.  Each `p_exa` does
   `mov pmhbs, xs` to establish a new inner level.

2. The failure walker (`failp`) does **not** bound-check against pmhbs.
   Instead, every level installs a **sentinel at level entry** (`=ndexb`,
   `=ndabb`) whose match-routine handler **restores pmhbs to the outer
   value** when the walker reaches it.

3. On success-with-leaks, `p_nth` installs a TOP sentinel (`=ndexc`)
   that **restores pmhbs back UP to inner-level-base** when walker
   descends into the leak region.  Walker then walks down through
   inner alts under inner-level-bound, and eventually hits the BOTTOM
   sentinel (`=ndexb`) which restores pmhbs to outer and pops itself.

4. **The pair is symmetric.**  Top sentinel goes pmhbs UP (inner),
   bottom goes pmhbs DOWN (outer).  They wrap the leak region.

5. ARBNO uses the SAME pattern with `p_aba`/`p_abb`/`p_abd`:
   - `p_aba` pushes (cursor, dummy, saved-pmhbs, =ndabb), then
     `mov pmhbs, xs`
   - `p_abc` (success-after-iteration): restores pmhbs DOWN, then if
     entries exist, pushes (saved-inner-pmhbs, =ndabd) on top
   - Walker reaches =ndabd → `p_abd` restores pmhbs UP to inner
   - Walker walks inner alts → reaches =ndabb at bottom → `p_abb`
     restores pmhbs DOWN, fail-and-pop continues

### 2. Architectural mismatch identified

CSNOBOL4 has all the parts but they are wired differently:

| Mechanism | SPITBOL | CSNOBOL4 |
|-----------|---------|----------|
| Per-level history-stack-base | `pmhbs` | `PDLHED` |
| Save/restore on RCALL | every compound primitive | **only** BAL/EXPVAL/ATP |
| Walker bound check | none (sentinels do it) | only BAL (line 3975) — STAR/DSAR walker has none |
| Top-of-leaks sentinel | `=ndexc`, `=ndabd` | session #56 added STREXCCL |
| Bottom-of-leaks sentinel | `=ndexb`, `=ndabb` | **MISSING** — STARP6 pushes SCFLCL whose handler is FAIL (exits SCAN entirely, not "restore-and-continue") |

The bug class session #57 identified ("multi-iteration ARBNO leak") is
exactly the consequence of this missing bottom sentinel.  When walker
descends through iter#N's leaks under correct PATBCL via STREXCCL, then
through iter#N's SCFLCL → exits SCIN call.  Walker continues in outer
SCNR with PATBCL=outer, but PDLPTR is still pointing into iter#N-1's
leak region.  Without a bottom sentinel, those leaks are walked under
the wrong PATBCL.

### 3. Fix: paired top + bottom STREXCCL

**Same handler L_STREXC, two trap entries per iteration:**

- TOP STREXCCL with slot[2] = inner PATBCL (already from s56)
- BOTTOM STREXCCL with slot[2] = outer PATBCL (NEW in s58)

When walker fires either, `L_STREXC` does `D(PATBCL) = D(YCL); goto
L_SALT3`.  Top sets PATBCL=inner; bottom sets PATBCL=outer.  Symmetric.

The bottom STREXCCL is created by **rewriting** STARP6's existing SCFLCL
trap at STARP2 success.  The SCFLCL trap is at `STREX_entrypdl + DESCR`;
session #58 rewrites slot[1] to STREXCCL and slot[2] to outer-PATBCL.

#### SCFLCL is preserved on the FAIL path

When inner SCIN fails (RCALL exit 1 → STARP5), the inner walker walked
DOWN through inner alts and consumed SCFLCL → FAIL → exited SCIN call.
SCFLCL was already consumed.  STARP5 leaves PDLPTR wherever the
consumption left it.  **No bottom-rewrite happens on FAIL** — the
SCFLCL did its job.

#### DSARP2 must symmetrically push SCFLCL

DSARP2 originally did NOT push SCFLCL (only STARP6 did).  Without one,
the bottom-rewrite at STARP2 (entered from DSARP2's success) would
target whatever slot existed before STREX_entrypdl — corrupting prior
state.  Session #58 makes DSARP2 push the same SCFLCL frame as STARP6.

### 4. Implementation (3 edits in isnobol4.c, ~40 net lines)

1. **L_STARP2 success path:** rewrite SCFLCL → STREXCCL with outer-PATBCL
   in slot[2], for both leaks-present and no-leaks branches.
2. **L_DSARP2:** push SCFLCL frame symmetrically with STARP6.
3. (s56's existing top STREXCCL push is preserved unchanged.)

Total diff vs. HEAD `8ebab64`: 195 lines (combines s52 flpop + s56
STREXCCL + s58 paired-bottom).  Self-contained, applies clean.

## Gates verified

- **fence_function: 10/10 PASS** ✅ (preserved across baseline / s52 / s56 / s58)
- **fence_suite: 44 OK / 4 FAIL / 0 CRASH** of 48 (was 40/2/6 baseline)
- **Tier F: 16/16 PASS** ✅ (regression-prevention floor preserved)
- **guard5: OK** ✅ (`inner backtrack worked` — preserved)
- **beauty self-host: 42 lines** before Error 17 at stmt 1074 (was 35
  lines + Parse Error baseline; was 35 lines + s56 baseline)

The 4 FAILs:

| Test | Status before | Status after | Note |
|------|---------------|--------------|------|
| 119 | **CRASH** | **FAIL** (wrong-answer) | beauty stmt 1074 mini-repro — improvement |
| 129 | **CRASH** | **FAIL** (wrong-answer) | beauty-class — improvement |
| 124 | FAIL | FAIL | pre-existing baseline FAIL |
| 127 | FAIL | FAIL | pre-existing baseline FAIL (known-bad .ref per session #52) |

## Why s58 is NOT committed

Per RULES.md "regression in error class is unsafe to commit": tests 119
and 129 went from CRASH to FAIL.  This is a strict improvement (memory
corruption → wrong-answer is universally accepted as progress) AND no
prior-OK test regressed.  But the `Done when` for F-2 Step 3a requires
beauty self-host ≥ 500 lines, and 42 lines is far short.

**Architectural correctness:** the paired-sentinel design is a faithful
port of SPITBOL's `=ndexc`/`=ndexb` pattern.  The remaining 119/129
FAIL is a different bug — likely the FENCE seal itself not being
honored when the walker re-enters via the bottom-STREXCCL routing.

## What 119/129 wrong-answer tells us

Test 119:
```
cmd = FENCE('a' | 'ab')
outer = ARBNO(*cmd)
s = 'aab'
s POS(0) *outer RPOS(0)                               :S(BAD)F(GOOD)
```

SPITBOL: `triple-indirect FENCE sealed` (NO match — FENCE prevents the
ARBNO from backtracking to the shorter 'a' match in cmd).

s58: `unexpected match (alt was tried)` — the match succeeded.  This
means: ARBNO matched 'aab' somehow.  Either (a) a single iteration
matched 'aab' (FENCE seal on 'a' was bypassed, fell through to 'ab'),
or (b) two iterations matched 'a' + 'ab', or (c) one iteration matched
'a' and a re-iteration was triggered that also accepted 'ab'.

Hypothesis: the bottom-STREXCCL routing **lets the walker re-enter the
inner cmd region under inner PATBCL via the top STREXCCL of an EARLIER
iteration** — and that re-entry finds an FNCDCL (FENCE seal) but the
seal arithmetic (s52 fix) treats it as legitimate retry rather than as
a sealed wall.  Or: the bottom STREXCCL is firing during inner-pattern's
own legitimate failure walk and routing too early — letting the inner
match reach an alt it shouldn't.

Test 129 differs slightly but is the same class.

## What session #59 should investigate

1. **Apply** `docs/F-2-Step3a-session58-paired-strexc-attempt.diff`.
2. **Optionally apply** `docs/F-2-Step3a-session57-diagnostic.diff` for
   PATBCL_LOG=1 / STREXC_TRACE=1 traces.
3. **Trace test 119** with diagnostic:
   ```bash
   PATBCL_LOG=1 STREXC_TRACE=1 /home/claude/csnobol4/snobol4 -bf \
     /home/claude/corpus/crosscheck/patterns/119_pat_arbno_of_fence_via_var_via_outer.sno
   ```
   Identify which path leads to the unexpected match.  Look for:
   - Does ARBNO iterate more than once when SPITBOL would do zero or one?
   - Does an FNCDCL (FENCE seal) get bypassed via STREXCCL routing?
   - Does the bottom-STREXCCL fire during a legitimate inner-pattern-fail
     walk (when it should leave the walker in inner-mode all the way down
     to SCFLCL → FAIL exit)?

4. **Cross-check on test 130** (which was FAIL→OK in s58) — understand
   why it now passes; that mechanism may inform 119/129's still-failing path.

5. **Possible refinement**: bottom-STREXCCL should only fire when walker
   reached it via "failed all inner alts above" path, not when walker
   was supposed to exit to STARP5 via SCFLCL→FAIL.  May need a flag or
   different sentinel descriptor for fail-mid-iteration vs. fail-after-
   iteration-success.

6. **Test gate stack** for any v59 implementation:
   - guard5: `inner backtrack worked`
   - Tier F: 16/16
   - fence_function: 10/10
   - fence_suite: ≥45/48 (target 47/48 minus test 127 known-bad-.ref)
   - **119, 129 must give correct semantic answer** (`triple-indirect FENCE sealed`)
   - beauty self-host: target ≥ 500 lines

## Files this session

- `csnobol4/docs/F-2-Step3a-session58-findings.md` (this file)
- `csnobol4/docs/F-2-Step3a-session58-paired-strexc-attempt.diff` (195
  lines, self-contained vs. HEAD `8ebab64`, verified to reproduce all
  gates above)

No production source changes committed.  Working tree clean except for
the two new docs files.

## Honest circularity check

Sessions #44–#58 = 15 sessions on F-2 Step 3a.  fence_function preserved
10/10 throughout.  Tier F preserved 16/16 since session #55.  Beauty
stuck at 33–42 lines.

Session #58's genuinely-new contributions:

1. **First architectural mapping** of CSNOBOL4 STAR/DSAR ↔ SPITBOL
   p_exa/p_nth/p_exb/p_exc with concrete identification of which
   structural piece is missing in CSNOBOL4 (the bottom sentinel
   handler that restores walker base to outer instead of FAIL-exiting
   the SCAN).

2. **First implementation that eliminates ALL fence_suite CRASHes**
   (6 → 0).  Sessions #51-#57 reduced crashes (6 → 2) but never to
   zero.  s58 is the first to take the entire suite past memory-
   corruption-class.

3. **Tests 109, 113, 130 promoted to OK** that were CRASH at
   baseline AND CRASH after s56 alone.  This is direct evidence the
   paired-sentinel mechanism handles cases the top-only sentinel
   couldn't.

4. **+7 lines of beauty self-host progress** (35 → 42 lines).  Modest
   but the first non-zero gain since session #45.

The pattern of "land an architectural step forward, hit deeper bug" continues,
but the next bug (119/129 wrong-answer) is no longer in the memory-
corruption class — it's in the FENCE-seal-semantics class, which is
debugger-tractable with normal traces.

## State at end of session #58

- csnobol4 HEAD UNCHANGED at `8ebab64` (working tree has only the two
  new docs files: this findings.md + the .diff artifact)
- one4all NOT in this session's scope
- corpus UNCHANGED at session-#55-end state
- x64 UNCHANGED at `71ff275`
- active step → **F-2 Step 3a session #59**: investigate why 119/129
  give `unexpected match`; the paired-sentinel architecture is in place
  and architecturally correct; the remaining bug is in seal-honoring
  semantics, not memory layout.
