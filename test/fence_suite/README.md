# FENCE 32-test suite

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
