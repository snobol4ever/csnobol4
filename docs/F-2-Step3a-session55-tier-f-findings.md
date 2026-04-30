# F-2 Step 3a — session #55 Tier F findings

GOAL-CSN-FENCE-FIX F-2 Step 3a (session #55, 2026-04-30).

## Action this session — Tier F enhancement to fence_suite

Added 16 depth-recursion stress tests to `fence_suite/` (numbered 132–147)
that exercise FENCE under recursion shapes the existing 32 tests don't cover:

- Canonical Gimpel `FENCE(*P | epsilon)` at depths 3 / 30 / 100
- Balanced delimiters `()` `([{<>}])` via mutual recursion
- Calculator with parenthesized recursion
- The `EVAL("pat . *f()")` double-function trick (beauty Shift/Reduce idiom)
- ARBNO + FENCE + `*var` combinations
- Regex Kleene-star with FENCE-locked character classes
- Nested-choice grammars with backtrack across FENCE seals
- Left-fold via `ARBNO(FENCE(op) operand)`
- FENCE longest-alt selection with `.` capture
- FENCE through `*var` inside concat

Each test is self-contained (no `-I` includes), 5–10 lines, with `.ref`
authored from SPITBOL output.

## Empirical result — all 16 Tier F tests PASS on csnobol4 baseline

| Suite | Baseline (HEAD `6d08540`) |
|-------|---------------------------|
| fence_function | 10/10 PASS |
| fence_suite Tier A–E (32 tests) | 24 OK / 2 FAIL / 6 CRASH |
| fence_suite Tier F (16 tests) | **16/16 PASS** |
| fence_suite total (48 tests) | 40 OK / 2 FAIL / 6 CRASH |
| SPITBOL on full suite | 47 OK / 1 FAIL (test 127 `.ref` known-bad) |

## What this empirically rules out

The bugs causing tests 119/129/130 are **NOT**:

- Depth recursion alone — 134 (depth 100) and 136 (parens depth 40) pass
- Mutual recursion via `*var` — 135–137 all pass
- Calculator-style `*var` nesting — 138, 139 pass
- Double-function `EVAL("pat . *f()")` dispatch — 140, 141 pass
- Generic ARBNO + FENCE — 142 passes
- Regex Kleene-star with FENCE — 143 passes
- Nested array grammars with FENCE — 144 passes
- Left-fold via ARBNO + FENCE — 145 passes
- FENCE longest-alt with `.` capture — 146 passes
- FENCE inside `*var` inside concat (no outer ARBNO) — 147 passes

## What it confirms

The 119/129/130 bug class is **narrow and specific**. It requires the
combination of:

1. `*var` indirection of a FENCE-containing pattern, **AND**
2. An outer ARBNO/iteration that pushes its own redo traps, **AND**
3. A tail-anchor failure that walks past the FENCE seal AND past the
   outer ARBNO's redo trap into leaked then-or alternation traps from
   the inner pattern's matching.

Test 117 (`ARBNO of *var FENCE`) and 142 (`ARBNO of LEN(1) FENCE`) both
pass because they don't have the third leg — there's no leaked then-or
trap whose `slot[1]` is an offset relative to the inner-pattern PATBCL.

This validates session #54's STREXCCL design as the **right shape** of
fix: a sentinel pushed at STARP6/DSARP2 success that, on dispatch,
restores PATBCL = inner-PATBCL before the failure walker enters the
leaked region. The fix is targeted at the exact mechanism Tier F
demonstrates is broken (and confines to where the bug actually lives).

## Regression-prevention floor

Any fix to the leak class **must** keep all 16 Tier F tests green plus
the 5-line `guard5` (`cmd=(LEN(1)|LEN(2)); outer=(*cmd 'X'); s='ABX'`).
This rules out:

- The naïve PDLPTR rewind (session #53) — would discard FNCDCL seals
  that 142, 143 depend on
- Targeted slot-zeroing (session #54) — kills inner-backtrack which
  guard5 specifically tests
- Saving-more-state in the seal trap (session #41 / D3 layouts) —
  doesn't address the *PATBCL routing* problem, just the
  *PDLPTR rewind* problem

STREXCCL doesn't share these failure modes:

- It doesn't discard seals (they sit below the sentinel)
- It doesn't zero anything (leaked traps remain dispatchable)
- It doesn't save bigger trap entries (just one extra trap conditionally
  pushed)

## Files changed this session (no runtime changes)

| Repo | File | Change |
|------|------|--------|
| corpus | `crosscheck/patterns/132_*.sno` … `147_*.sno` | New (16 .sno files) |
| corpus | `crosscheck/patterns/132_*.ref` … `147_*.ref` | New (16 .ref files, derived from SPITBOL) |
| csnobol4 | `test/fence_suite/Makefile` | Added TIER_F block, updated counts 32→48 |
| csnobol4 | `test/fence_suite/Makefile` | Added `-bf` to spitbol target (was missing) |
| csnobol4 | `test/fence_suite/README.md` | Added Tier F documentation + empirical-result section |
| csnobol4 | `docs/F-2-Step3a-session55-tier-f-findings.md` | This file |

**Working tree of csnobol4 has NO source-code changes** — only test
infrastructure additions. The runtime is bit-identical to HEAD
`6d08540`. STREXCCL implementation is left for next session, now with
a 16-test regression-prevention floor in place.

## Next session's mandate

Session #56 (or later) should implement STREXCCL exactly as session
#54's findings prescribe, and validate against the **48-test** fence_suite
plus the 5-line guard5. Specifically the gates are:

| Gate | Target |
|------|--------|
| guard5 (5-line inner-backtrack) | `inner backtrack worked` |
| fence_function | 10/10 PASS |
| fence_suite Tier A–E (32) | ≥30 OK incl. 119/129 |
| fence_suite Tier F (16) | 16/16 PASS (regression floor) |
| fence_suite total | ≥46/48 |
| Beauty self-host | ≥500 lines |

If Tier F regresses below 16/16, the proposed fix is wrong regardless
of how many other tests improve.

## Honest circularity check

This session's contribution is **pure test infrastructure** — no runtime
code changes. The new tests reveal that the bug class is narrower than
session #51's "4 bug classes mapped" framing suggested. The 4 bug
classes are still real, but they all share one underlying mechanism
(PATBCL context mismatch on dispatched leaked traps), and Tier F
provides empirical isolation of that mechanism by demonstrating which
shapes do NOT trigger it.

This is the kind of session-to-session value that doesn't move the
beauty self-host counter but materially advances the diagnosis. Future
sessions can use Tier F as an oracle to disqualify wrong-fix candidates
before running them on beauty.
