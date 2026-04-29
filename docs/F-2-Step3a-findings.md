# F-2 Step 3a investigation — *cmd-FENCE-RPOS bug class

GOAL-CSN-FENCE-FIX F-2 Step 3a continuation (session #49).

## What this session found

### Concrete minimal reproducer

```snobol
        cmd = FENCE('a' | 'ab')
        s = 'ab'
        s  POS(0) *cmd RPOS(0)  :s(ok)f(fail)
ok      OUTPUT = 'ok'  :(done)
fail    OUTPUT = 'fail'
done
END
```

- **csnobol4 (current HEAD `0129932`):** SIGSEGV at `isnobol4.c:11437`
  (SCIN1 reads garbage PATICL = `stdio_ops+16` shaped pointer, then crashes
  on `D(D_A(PATBCL) + D_A(PATICL))`).
- **SPITBOL x64:** prints `fail` (correct — FENCE seals against backtrack
  into the alternative).

This is the simplest possible expression of beauty.sno's failure mode.
Beauty uses `*snoStmt`, `*snoCommand`, `*snoParse` — all unevaluated
references to FENCE-containing patterns — under `RPOS(0)` tail anchors.

### Note on the existing fence_function/ suite

Test 061 (`pat_fence_fn_seal`) already covers `FENCE(LEN(1)|LEN(2)) RPOS(0)`
on `'AB'` — but FENCE is **inline** there, not via variable-indirection (`*var`).
The inline path goes through PATBRA dispatch directly; the indirect path goes
through STAR (PATBRA case 32) → EXPVAL → SCIN, which is structurally
different and has its own setup of PDLHED.  All 10/10 fence_function tests
pass; none of them exercises the variable-indirection path.

A new test should be added to fence_function/ that mirrors the failing
shape (`X = FENCE(...); s POS(0) *X RPOS(0)`).

### Diagnosis (verified via gdb)

When `*cmd` matches the FENCE pattern, control flow is:

1. Outer SCIN walks the outer pattern.  Hits the STAR (`*`) pattern
   node → L_STAR → L_STAR2 → calls EXPVAL(YPTR) which evaluates `cmd`
   to the FENCE pattern.
2. L_STARP6 pushes SCFLCL sentinel at PDLPTR+DESCR (call this position
   `STAR_S+DESCR`), then SAVSTK(); SCIN(NORET) — recursive scan of the
   FENCE pattern.
3. Inner SCIN walks the FENCEPT node → SCIN3 dispatches FNCAFN via
   PATBRA → L_FNCA.
4. L_FNCA pushes its own SCFLCL trap on PDL, saves 9 things on cstack
   (including PDLHED), sets `PDLHED = PDLPTR` (FENCE's inner pmhbs
   snapshot), recursive SCIN call to match P (the inner pattern).
5. Inner P succeeds (matches `'a'`).  FNCA's success path pops cstack,
   rewinds PDLPTR to `STAR_S` (the pre-SCFLCL position the SCIN3
   trap-around-FENCEPT was at), then re-pushes a 3-slot trap region
   with FNCDCL seal at slot[1].
6. **Bug:** FNCA writes seal slot[2] (the rewind target for L_FNCD) as
   `D(PDLHED)`.  At this point PDLHED has been popped from cstack and
   equals the OUTER-SCAN's PDLHED — NOT a meaningful PDL position
   inside the current matching context.
7. STAR's outer SCIN succeeds, control returns to outer scan.  Outer
   scan walks past the `*cmd` node, hits RPOS(0), which fails (we're
   at position 1 of `'ab'`, not position 2).  Failure walker fires.
8. Walker traverses traps backward until hitting the FNCDCL seal.
   L_FNCD does `PDLPTR = YCL` (= seal slot[2] = outer-scan PDLHED)
   then BRANCH(FAIL).
9. Walker's next SALT2 reads slot[1] at `outer-scan-PDLHED + DESCR` —
   stale memory from earlier I/O setup, often `&stdio_ops + 16`
   shaped.  PATICL gets that pointer-shaped value, walker mistakenly
   takes the non-FNC branch (`goto L_SCIN3`), forward-scan crashes
   on the wild pointer.

### Proposed fix (verified working for the minimal case)

In `L_FNCA` success path, replace the seal slot[2] write:

```c
/* OLD (broken) */
D(D_A(PDLPTR) + 2*DESCR) = D(PDLHED);
```

with:

```c
/* NEW: synthesize a PTR descriptor pointing to the rewind target S,
   which is FNCA's outer-PDLPTR-at-entry value (= current PDLPTR -
   3*DESCR after the success-path arithmetic). */
D_A(TMVAL) = D_A(PDLPTR) - 3*DESCR;
D_F(TMVAL) = D_V(TMVAL) = 0;
D(D_A(PDLPTR) + 2*DESCR) = D(TMVAL);
```

When the seal fires, `PDLPTR` is then set to `S`, where `S+DESCR`
contains the SCIN3-around-FENCEPT trap entry whose slot[1] is the
FENCE node's then-or alternative (= zero descriptor for our top-level
FENCE).  Walker reads this NULL sentinel via SALT2 → falls to L_SALT3
→ fails the enclosing match cleanly.

### Result of applying the proposed fix

- ✅ `min_crash.sno`: SEGV → `fail` (matches SPITBOL).
- ✅ Nested variant `cmd2 = FENCE(*cmd1 'c' | epsilon)`: works.
- ✅ fence_function/ regression: 10/10 PASS preserved.
- ❌ **Beauty self-host: NEW SEGV at stmt 1074.**  The clean "Parse Error"
  baseline (35 lines) becomes a SIGSEGV (33 lines).  The crash signature
  is the OLD F-0 signature — `PATICL = 0xc0` (192 = 12*DESCR sentinel)
  — i.e. a cstack-overwrite bug class apparently distinct from the one
  this fix addresses.

The fix is therefore correct in isolation but UNCOMMITTED because of the
beauty regression.  See "Why this is unsafe to commit alone" below.

### Why this is unsafe to commit alone

Beauty's regression isn't a pre-existing bug being newly exposed by a
correct fix.  Both before and after this fix, beauty fails to produce
≥500 lines of self-host output.  But before the fix, beauty failed
GRACEFULLY (parse error message); with the fix, it CRASHES (signal 11).
A SEGV is strictly worse than a controlled program error.  Per RULES.md
("a broken push is better than no push" only applies when the alternative
is no work-tree-clean state) — committing a fix that turns clean failure
into crash is a regression in error class.

A correct fix needs to also handle the deep-nesting case beauty triggers.
That's a separate investigation.

### Hypothesis for the deep-nesting bug

At deep FENCE nesting (FENCE inside FENCE inside *var inside ARBNO inside
*var ...), the cstack PUSH/POP arithmetic for FNCA's outer state save
becomes unbalanced under FULLSCAN backtracking that traverses multiple
SCIN frames.  Specifically:

- Inner FENCE's success returns to outer FENCE's SCIN.
- Outer FENCE's success returns to STAR's SCIN.
- STAR's success returns to outer scan.
- Outer scan tries RPOS(0) which fails.
- Failure walker fires the seal of the OUTERMOST FENCE.
- That seal rewinds PDLPTR to outer FENCE's entry-PDLPTR.
- Walker then needs to fail past inner FENCE's seal too — but inner
  FENCE's cstack saves were already popped on inner FENCE's success.
  The walker re-encounters the inner FENCEDCL trap on its way back,
  fires L_FNCD, which now reads the (correct, my-fix) rewind target.
- But there's a SECOND issue: the walker continues past inner-FENCE's
  rewind point, reaches some EARLIER trap that has stale slot[1] data
  because the earlier success-path POP didn't clear the PDL slot it
  abandoned.

The inner-FENCE seal's rewind target points to a PDL position whose
slot[1] should be a NULL sentinel (SCFLCL), but at that position there's
actually a stale descriptor left over from an earlier SCIN3 push that
was abandoned when an inner pattern matched and walked forward.

The right fix for this would be: in FNCA's success path, after rewinding
PDLPTR to S+3*DESCR for the seal write, ALSO clear (zero) any PDL slots
in the abandoned inner-FENCE region that the walker might revisit.  But
this needs careful analysis of which slots could be revisited.

### Files of interest for next session

| File | Role |
|------|------|
| `csnobol4/isnobol4.c:12258-12370` | L_FNCA / L_FNCBX / L_FNCD — primary edit target |
| `csnobol4/snobol4.c` (same range) | sister generated file, must match isnobol4.c |
| `csnobol4/v311.sil:4093-4156` | SIL source — needs same edits per RULES.md "SIL/C consistency" |
| `csnobol4/lib/pat.c` cpypat | already correct (v=4 path) |
| `csnobol4/test/fence_function/` | needs a new test for `*cmd RPOS(0)` shape |
| `corpus/programs/snobol4/demo/beauty/beauty.sno` | self-host gate target |

### Recommended next-session steps

1. **Add a fence_function/ test** that mirrors the `*cmd FENCE RPOS(0)`
   shape.  This should fail on baseline and pass after the partial fix.
   Even if we can't ship the partial fix yet, having the test recorded
   prevents future regressions on this case.

2. **Build a minimal repro of the deep-nesting cstack bug** that does
   NOT require beauty.  Beauty is a 700-line program — too big to use
   as a unit-level oracle.  Once we have a small repro, the partial fix
   plus the deep-nesting fix can land together, with both new tests in
   fence_function/.

3. **Investigate PDL slot zeroing** as the candidate deep-nesting fix.
   Specifically: in FNCA's success-path rewind, before re-pushing the
   FNCDCL trap region, write zeros to any abandoned slots that the
   failure walker could revisit later.

