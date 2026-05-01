# F-2 Step 3a session #65 findings — test suite verification + Tier G additions

**Date:** 2026-05-01
**Repos touched:** csnobol4 (Makefile, README, this file), corpus (test files).
**Code touched:** none (no runtime source changes).

## What this session did

Session #65 was scoped to **verify the test suite against the SPITBOL oracle
and fill its actual coverage gaps**, before any further attempt to write the
runtime fix outlined in session #64's plan (FNCA-success leaked-alt zeroing).

The motivation: 20 sessions on F-2 Step 3a, six successive "land a fix, hit
deeper bug" cycles, two prior sessions where the proposed fix was implemented
and refuted by gates.  Before another such cycle, audit the gates themselves.

## What the audit found

### 1. SPITBOL was not 47/1 of 48 — it was 46/2/0

The goal file reported SPITBOL passes 47 of the 48 tests with test 127 known
bad-`.ref`.  Direct measurement on this session's HEAD shows SPITBOL produces
**46 OK / 2 FAIL / 0 CRASH** of the original 48:

- Test 127's `.ref` (`k=age s="age":42 n=42 b=`) was authored under SPITBOL
  `-b` (case-fold ON); the Makefile invokes SPITBOL with `-bf` (case-fold OFF),
  and `-bf` output is `k=age s= n=42 b=`.  This was a corpus error, not a
  SPITBOL bug — fixed by updating the `.ref`.
- Tests 140 and 141 used label names `shift`/`Shift` and `grab`/`Grab` that
  collide under SPITBOL's case-fold (default).  This is a portability issue,
  not a bug — fixed by renaming labels (`inner`/`outer`, `grab`/`catch`).

After both corrections SPITBOL is **53 OK / 0 FAIL / 0 CRASH** on the new
53-test suite.  This is the first time the oracle gate is actually clean.

### 2. Test 118 was structurally degenerate

Test 118's source assigns `outer = ARBNO(*cmd)` *after* the match statement
that uses `*outer`.  In SNOBOL4 statements execute in source order, so
`*outer` dereferences an unassigned variable at match time and the FENCE
machinery never runs.  Both csnobol4 and SPITBOL produce the no-match
output ("FENCE sealed") because the match fails immediately, not because
the seal worked.

The test's `.ref` happened to align with that outcome, hiding the
degeneracy.  Test 118 was listed in the goal file's session #51 strategy
section as a target the fix must hit; it never actually exercised the bug.

Replaced 118 with comment-only documentation of the degeneracy and added
test 149 — same shape, with `outer` correctly assigned before the match.
Test 149 fails on csnobol4 with the genuine bug-class signature.

### 3. The bug class is sharper than session #62 stated

Session #62 reported that the bug requires the conjunction of:
1. `*var` outer indirection, AND
2. ARBNO inside that var, AND
3. `*var` inside the ARBNO body, AND
4. FENCE dispatched from that inner var.

Tier G's additions tighten this:

- Test 150 (`outer = (*cmd | *cmd *cmd | *cmd *cmd *cmd)` with no ARBNO)
  PASSES on csnobol4.  This proves the bug requires **ARBNO specifically**,
  not generic-iteration.  The bug is in ARBNO's redo-trap mechanism, not
  in any iterating outer construct.
- Test 151 (ARBNO of *inline* FENCE with backtrack-needed input) PASSES on
  csnobol4.  This proves the bug requires **`*var` indirection** of the
  FENCE pattern, not inline FENCE.

The refined bug-class conjunction is:
1. **`*var` indirection** of a FENCE-containing pattern, AND
2. **ARBNO** as the outer iterator (not other iteration shapes), AND
3. Tail-anchor failure that backtracks across the FENCE seal.

### 4. Test 127's failure was partly a case-fold issue, not pure FENCE bug

Test 127 used `S` as a capture variable (`BREAK('"') . S`) and `s` as the
input string.  Under SPITBOL `-b` (case-fold), these collide; under `-bf`
they don't.  csnobol4 always treats them as distinct.  The `.ref` was
generated under `-b` semantics that made it impossible to tell what 127
was actually measuring.

Test 152 (added) is 127 with capture variables renamed to `SVAL`, `NVAL`,
`KVAL`, `BVAL`.  Same FENCE structure, no case-fold ambiguity.  csnobol4
still fails 152 — that's the real bug 2 (conditional-assign inside FENCE
not committed on success), which 127's case-fold collision had been
masking.

## What the corrected suite enables

After session #65:

| Implementation | Tests | OK | FAIL | CRASH |
|----------------|-------|----|------|-------|
| SPITBOL `-bf`  | 53    | **53** | 0 | 0 |
| csnobol4 baseline | 53 | 46 | 7    | 0 |

The 7 csnobol4 FAILs (119, 124, 127, 129, 148, 149, 152) form the
**bug-class regression target**.  Any fix must:

- Promote all 7 to OK
- Keep tests 150 and 151 passing (negative discriminators that disprove
  past sessions' wider hypotheses)
- Keep Tier F's 16 tests passing (depth-recursion regression floor)
- Keep `fence_function/` 10/10 PASS
- Keep `guard5` producing `inner backtrack worked`

## Why this matters for the next runtime-fix session

Session #64 proposed FNCA-success leaked-alt zeroing as the next attempt.
That plan is consistent with the new Tier G evidence:

- Inline FENCE inside ARBNO works correctly (test 151) — so FNCA itself
  is not where the leak forms; the leak is in how `*var`-dispatched FENCE
  hands state back through STAR/DSAR.
- Bug requires ARBNO redo specifically — consistent with session #57's
  multi-iteration leak diagnosis.

**However**, session #62's PDL-dump diagnostic (no SALT2 events between
post-STARP2 dump and wrong-match output) suggests the wrong match goes
through the SUCCESS path, not the failure walker.  If true, zeroing
slot[1] of leaked traps doesn't help — the walker never reads them.

Session #62 and session #64 are in tension on this point.  Before
implementing the zeroing fix, session #66 should rerun session #62's
PDL-dump diagnostic on the now-larger test set, focusing on test 148
(simpler than 119 — input `'ab'` not `'aab'`, only one ARBNO iteration
of the FENCE-sealed `'a'` succeeds before tail-anchor failure).  The
trace will either:

- Show SALT2 events on the wrong-match path → session #64's framing
  holds → zeroing is the right fix → implement it.
- Show no SALT2 events → session #62's framing holds → zeroing won't
  help → redirect investigation to the success path (likely STARP2's
  redo dispatch).

Test 148 is significantly easier to trace than 119 because it has fewer
ARBNO iterations and a shorter input string.  The diagnostic state-space
is smaller.

## Files this session

- `corpus/crosscheck/patterns/118_*.sno` — comment-only correction
  (acknowledges degeneracy)
- `corpus/crosscheck/patterns/127_*.ref` — fixed: was generated under `-b`,
  Makefile uses `-bf`
- `corpus/crosscheck/patterns/140_*.sno` — renamed labels for portability
- `corpus/crosscheck/patterns/141_*.sno` — renamed labels for portability
- `corpus/crosscheck/patterns/148_*.sno`, `.ref` — Tier G new
- `corpus/crosscheck/patterns/149_*.sno`, `.ref` — Tier G new
- `corpus/crosscheck/patterns/150_*.sno`, `.ref` — Tier G new (negative)
- `corpus/crosscheck/patterns/151_*.sno`, `.ref` — Tier G new (negative)
- `corpus/crosscheck/patterns/152_*.sno`, `.ref` — Tier G new
- `csnobol4/test/fence_suite/Makefile` — Tier G integration, header rewritten
- `csnobol4/test/fence_suite/README.md` — Tier G section added
- `csnobol4/docs/F-2-Step3a-session65-findings.md` — this file

## Files NOT touched

- No runtime source changes (`isnobol4.c`, `snobol4.c`, `v311.sil`, `lib/pat.c`
  all unchanged).
- No `.github/PLAN.md` or goal-file step-update yet — that's the handoff
  step, deferred to user direction.

## Honest checkpoint

Sessions #44–#65 = 21 sessions on F-2 Step 3a.  Session #65's contribution
is non-runtime: it sharpens the gate.  This is similar to session #55's
Tier F contribution.  Beauty self-host and fence_function counts unchanged
because no runtime code changed.  csnobol4 working tree clean before this
session's docs/Makefile changes; corpus working tree previously had 32
untracked Tier F files (sessions #51-55) plus this session's 5 new + 4
modified files.

The 7-FAIL bug-class target is more specific than the prior 4-FAIL target
in two ways: (a) it includes tests 148 and 149 which are harder for any
"accidentally passes" fix to satisfy than 119/129 alone (different input
strings, different output messages), and (b) the negative discriminators
150/151 reject overly-broad fixes that the prior gate would have allowed.
