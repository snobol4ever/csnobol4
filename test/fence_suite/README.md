# FENCE 53-test suite

**Updated session #65, 2026-05-01:** suite extended to 53 tests with Tier G
(148-152) — a bug-class regression target plus negative discriminators
verified against the SPITBOL oracle.  See "Tier G" section at end of file.

A second FENCE regression suite, complementary to `test/fence_function/`.
Where `fence_function/` covers 10 toy cases of inline FENCE, this suite covers
the **realistic patterns** that real SNOBOL4 grammars use — `*var` indirection,
ARBNO loops, calculator/regex/JSON parsers, and the deeply-nested shapes that
beauty.sno hits.

Tests live in `corpus/crosscheck/patterns/100..131`.  Run with:

```bash
cd csnobol4/test/fence_suite && make csnobol4   # against csnobol4
cd csnobol4/test/fence_suite && make spitbol    # against SPITBOL oracle
```

## Tier organization

| Tier | Range | Theme |
|------|-------|-------|
| A | 100–107 | Baseline FENCE (no `*var`, no ARBNO) — sanity floor |
| B | 108–115 | `*var` indirection of FENCE — beauty's first bug class |
| C | 116–119 | ARBNO + FENCE combinations |
| D | 120–127 | Real grammars: calculator (3), regex (2), JSON (3) |
| E | 128–131 | Beauty-class deeply-nested grammars |
| F | 132–147 | **Depth-recursion stress (session #55)** — answers "does FENCE corrupt state under deep recursion?" |

## csnobol4 baseline (HEAD `80454aa`, session #50 end)

**26 OK · 4 FAIL · 2 CRASH** of 32.

The 6 non-passing tests reveal **at least 3 distinct csnobol4 FENCE bugs**, all
absent in `fence_function/`'s 10/10 PASS suite:

### Bug 1 — outer alt fall-through after FENCE-via-`*var` continuation fails

Tests **114, 124** (FAIL).

**Shape:** `(*cmd 'X' | *cmd 'Y' | LEN(0))` where `cmd = FENCE(...)`.
First alt's `*cmd` matches and seals; suffix `'X'` fails; outer alternation
should fall through to second alt `*cmd 'Y'`. csnobol4 fails outright instead.

This is **not** the same as test 109 (which already passes). 109 has FENCE
directly under the `s POS(0) ... RPOS(0)` outer scan; the seal correctly fails
the whole match. 114/124 have FENCE under an *outer alternation* that has
more alternatives to try; the seal must NOT propagate failure that high.

### Bug 2 — conditional assignment (`.`) inside FENCE not committed on success

Test **127** (FAIL).

**Shape:** `FENCE(num)` where `num = digits . N` against `'42'`. Match
succeeds, but `N` is empty. Bare `num` works correctly; the FENCE wrapper
loses the `.` commit. Probably an artifact of D6's recursive-SCIN call frame:
the conditional-assignment commit happens in the inner SCIN's success path
but isn't propagated to the outer cscope.

### Bug 3 — SEGV on `ARBNO(*cmd)` indirection of FENCE

Tests **119, 129** (CRASH).

**Shape:** `*outer` where `outer = ARBNO(*cmd)` where `cmd = FENCE(alts)`,
plus a tail anchor (`RPOS(0)`) that must fail. Triple `*var` indirection.
This is the bug class blocking beauty.sno's stmt 1074 (`*snoParse *snoSpace
RPOS(0)`).

### Bug 4 — concat of two `*var`-FENCE with mismatched alternation length

Test **130** (FAIL, no crash).

**Shape:** `*outer` where `outer = *cmdA *cmdB`, cmdA = FENCE-containing,
cmdB = SPAN/epsilon. Subject `'xy'`. csnobol4 fails the whole match where
SPITBOL succeeds. Probably related to bug 1 — the FENCE seal interferes
with the outer alternation's continuation across the second `*var`.

## Strategy for fixing FENCE

These bugs likely share a single underlying cause: **the recursive-SCAN
(D6) implementation of FENCE doesn't correctly hand back state to the
outer scan when the seal fires across a `*var` boundary.**

Bug 1 (114/124) and bug 4 (130) probably share root cause with bug 3
(119/129). Bug 2 (127) is a separate axis (conditional-assign commit
through recursive-SCIN return).

The right test gate to drive fixes:

1. **Tier A (8/8) must always stay green.** Sanity baseline.
2. **Tier B (currently 7/8) → 8/8** would close bug 1.
3. **Tier C (currently 3/4) → 4/4** would close bug 3 (CRASH).
4. **Tier D (currently 6/8) → 8/8** would close bugs 1 + 2.
5. **Tier E (currently 2/4) → 4/4** would close bugs 3 + 4.

The minimal repro for bug 3 is **test 119 / 129** — five lines of SNOBOL4.
Any fix attempt should run those first.

## When SPITBOL output is available

The `.ref` files were authored to match **SPITBOL's correct semantics**
(SNOBOL4-doc-conforming behavior). When run against `make spitbol`, all 32
should PASS. If any don't, the .ref needs correction — not a SPITBOL bug.

The fence_function/ suite is also reference-quality. This new suite
extends coverage; together they form the FENCE contract csnobol4 must meet.

## Tier F (session #55): the empirical bug-class boundary

Tier F adds 16 depth-recursion stress tests. **All 16 PASS on csnobol4
baseline at HEAD `6d08540`.** This is a useful negative result — it
constrains what the existing 119/129/130 bug class actually IS:

The bugs causing tests 119/129/130 to crash/fail are **NOT**:

- Depth recursion alone (134 = depth 100, 136 = parens depth 40, both pass)
- Mutual recursion via `*var` (135-137 all pass)
- Calculator-style nesting with `*var` (138, 139 pass)
- The double-function trick `EVAL("pat . *f()")` (140, 141 pass)
- ARBNO containing inline FENCE (142 passes)
- Regex with FENCE-locked alternation (143 passes)
- Nested array grammars with FENCE (144 passes)
- Left-fold via ARBNO+FENCE (145 passes)
- Capture into FENCE alts (146 passes)
- FENCE through `*var` inside concat (147 passes)

The bugs **specifically require** all three of:

1. `*var` indirection of a FENCE-containing pattern, **AND**
2. Being matched under an outer ARBNO/iteration, **AND**
3. A tail-anchor failure that triggers the failure walker to traverse
   leaked traps from an iteration past the seal.

That is the precise signature of test 119/129. The Tier F results
demarcate this bug class from the broader "FENCE under recursion"
space — the runtime handles deep recursion, mutual recursion, and
double-function dispatch correctly. The leak class is narrow and
specific, which validates session #54's STREXCCL design (a sentinel
that reroutes PATBCL specifically when STARP6/DSARP2 succeed with
inner-pushed entries) as the targeted fix.

Tier F also provides a **regression-prevention floor**: any fix to
the FENCE leak class must keep all 16 Tier F tests green. The 5-line
guard `cmd=(LEN(1)|LEN(2)); outer=(*cmd 'X'); s='ABX'` from session
#54 is one element of this floor; Tier F is the broader version.

### Tier F details

| ID | Name | What it stresses |
|----|------|-------------------|
| 132 | `pat_fence_eps_recur_shallow` | Canonical Gimpel `FENCE(*P\|eps)`, depth 3 |
| 133 | `pat_fence_eps_recur_deep` | Same, depth 30 |
| 134 | `pat_fence_eps_recur_stress` | Same, depth 100 |
| 135 | `pat_balanced_parens_shallow` | Balanced parens via FENCE, depth 3 |
| 136 | `pat_balanced_parens_deep` | Same, depth 40 |
| 137 | `pat_balanced_mixed` | `([{<>}])` heterogeneous brackets |
| 138 | `pat_calc_paren_expr` | `((1+2)*3)` calculator with parens |
| 139 | `pat_calc_paren_deep` | `(((((1)))))` all-parens depth 5 |
| 140 | `pat_eval_double_fn_trick` | Beauty Shift/Reduce idiom isolated |
| 141 | `pat_eval_double_fn_arbno` | Double-fn trick under ARBNO loop |
| 142 | `pat_arbno_fence_arbno` | ARBNO of `LEN(1) FENCE(LEN(1)\|eps)` |
| 143 | `pat_regex_quantified_class` | `ARBNO(*LP)` where `LP=FENCE(...)` |
| 144 | `pat_json_nested_array` | Nested-choice with backtrack across FENCEs |
| 145 | `pat_left_assoc_via_arbno_fence` | `num ARBNO(FENCE('+') num)` left-fold |
| 146 | `pat_fence_alt_with_capture` | FENCE longest-match with `.` capture |
| 147 | `pat_fence_through_unevaluated` | Minimal FENCE-in-`*var`-in-concat |

## When SPITBOL output is available

The `.ref` files were authored to match **SPITBOL's correct semantics**
(SNOBOL4-doc-conforming behavior). When run against `make spitbol`, all 48
should PASS — except test 127, whose `.ref` is incorrect (documented in
goal-file session #52 findings; correction is pending). Currently:
**SPITBOL: 47/48 PASS · csnobol4: 40/48 PASS** (HEAD `6d08540`).

The fence_function/ suite is also reference-quality. This new suite
extends coverage; together they form the FENCE contract csnobol4 must meet.

## Tier G (session #65, 2026-05-01): bug-class regression target + negative discriminators

Tier G adds 5 tests (148-152) verified against the SPITBOL oracle.  Their
purpose is to make the regression target **specific** rather than just larger.

| ID  | Name                                       | Status  | Role |
|-----|--------------------------------------------|---------|------|
| 148 | `pat_arbno_star_var_fence_short`           | FAIL    | Bug-class POSITIVE — variant of 119 with shorter input string `'ab'` |
| 149 | `pat_arbno_star_var_fence_outer_pre_match` | FAIL    | Bug-class POSITIVE — corrected version of structurally-degenerate test 118 |
| 150 | `pat_star_var_fence_alts_no_arbno`         | OK      | NEGATIVE discriminator — same conjunction as 119 but explicit alternation iterator instead of ARBNO |
| 151 | `pat_arbno_inline_fence_backtrack`         | OK      | NEGATIVE discriminator — ARBNO of *inline* FENCE with backtrack-needed input (not `*var`-dispatched) |
| 152 | `pat_json_keyvalue_renamed`                | FAIL    | Bug-class POSITIVE — 127 with capture vars renamed to avoid case-fold collision |

**Findings from the Tier G additions:**

1. **Test 118 was structurally degenerate.** Its source assigns `outer` *after*
   the match statement, so `*outer` dereferences an unassigned variable at
   match time and the FENCE machinery never runs.  Both csnobol4 and SPITBOL
   agree on the no-match outcome, but only because no one ran the seal-test
   logic.  Test 149 is what 118 was meant to be.

2. **Test 150 (no-ARBNO version) PASSES on csnobol4.**  This sharpens the
   bug-class characterization: it requires *ARBNO specifically*, not just any
   outer iteration.  The bug is in ARBNO's redo-trap mechanism interaction
   with FENCE leaks.

3. **Test 151 (inline-FENCE-no-`*var`) PASSES on csnobol4.**  This further
   sharpens: the bug requires *`*var` indirection* of the FENCE pattern, not
   inline FENCE.  Inline FENCE inside ARBNO with backtrack-required input is
   handled correctly.

4. **Test 152 exposes a cleaner case for bug 2.**  Test 127 used `S` as a
   capture variable, which under SPITBOL's default case-folding collides with
   the input variable `s` — this masked what 127 was measuring.  Renamed
   capture vars (`SVAL`, `NVAL`, etc.) make the FENCE conditional-assign-not-
   committed bug visible cleanly.

5. **Tests 140 and 141 had label collisions** under SPITBOL's case-fold
   default.  `shift`/`Shift` and `grab`/`Grab` collide when SPITBOL folds
   case internally.  Renamed to `inner`/`outer` and `grab`/`catch` for
   cross-dialect portability.

6. **Test 127's `.ref` was generated under SPITBOL `-b` (case-fold ON);
   the Makefile runs `-bf` (case-fold OFF).**  Updated `.ref` to match
   `-bf` output (`k=age s= n=42 b=`).

**Suite totals after Tier G additions:**

| Implementation | Total | OK | FAIL | CRASH |
|----------------|-------|----|------|-------|
| SPITBOL (-bf)  | 53    | **53** | 0 | 0 |
| csnobol4       | 53    | 46 | 7    | 0 |

**The 7 csnobol4 FAILs** (119, 124, 127, 129, 148, 149, 152) form the
**bug-class regression target**.  Any fix to the FENCE `*var`+ARBNO
backtrack bug must:

- Promote all 7 to OK
- Keep tests 150 and 151 passing (negative discriminators)
- Keep Tier F's 16 tests passing (depth-recursion floor)
- Keep `fence_function/` 10/10 (toy regression suite)
- Keep `guard5` (`cmd=(LEN(1)|LEN(2)); outer=(*cmd 'X'); s='ABX'`) producing
  `inner backtrack worked` (inner-backtrack regression guard from session #54)
