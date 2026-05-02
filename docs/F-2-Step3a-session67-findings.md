# F-2 Step 3a — Session #67 Findings (2026-05-02)

**Status:** Composed fix landed (L_FNCD goto-L_TSALT + A2 zeroing with
PDLHED..PDLPTR FNCDCL gate). Cluster B **closed** (test 124 → OK).
Cluster A (119/129/148/149) and bug 2 (127/152) still FAIL.

**Working tree at session end:** dirty with `docs/F-2-Step3a-session67-A2-attempt.diff`
(54 lines). NOT committed — only test 124 net gain isn't enough to justify
a runtime commit per F-2 Step 3a's `Done when` (≥500 line beauty self-host).

---

## Gates this session

| Gate | s64 baseline | s67 (composed) |
|------|--------------|----------------|
| guard5 | OK | **OK** ✓ |
| fence_function | 10/10 | **10/10** ✓ |
| Tier F (132–147) | 16/16 | **16/16** ✓ |
| fence_suite total | 46/7/0 | **47/6/0** (gained 124) |
| beauty self-host | 42 lines | (not measured — same architecture) |

The composed fix:
1. **L_FNCD: `BRANCH(FAIL)` → `goto L_TSALT`** (1 line) — closes Cluster B.
2. **A2 zeroing at L_STARP2** (33 lines) — gated on `FNCDCL anywhere in
   [PDLHED..PDLPTR]`. Walks `[STREX_entrypdl+3*DESCR..PDLPTR]` and zeros
   `slot[1]` of non-FNC entries.

Three gate variants tested:

| Variant | Cluster A | guard5 |
|---------|-----------|--------|
| A2 + FNCDCL-in-current-region (session #66 plan) | unchanged | OK |
| A2 + FNCDCL-anywhere-PDLHED..PDLPTR (committed shape) | unchanged | OK |
| A2 + unconditional zero | **FIXED** (51/2/0) | **BROKEN** |

The unconditional variant achieves the gate target (51/2/0, only bug 2
remains) but breaks guard5 (same regression session #54 hit). The bounded
variants don't reach Cluster A because **at the STARP2 where the bug entry
lives, FNCDCL doesn't exist on PDL yet** — see architectural finding below.

---

## Genuinely new architectural finding (refutes session #66 framing)

Session #66 hypothesized the bug entry was a "leaked inner-FENCE alt-cont"
sitting between `*outer`'s STARP2 STREXCCL pair. Trace evidence on test 148
**refutes this**.

### The two STARP6/STARP2 invocations in test 148

Pattern: `s POS(0) *outer RPOS(0)` where `outer = ARBNO(*cmd)` and
`cmd = FENCE('a' | 'ab')`, subject `s = 'ab'`.

Trace order (env-gated A2_TRACE=1, instrumentation in
`docs/F-2-Step3a-session67-diagnostic.diff`):

```
STARP6 entry #1: PATBCL=...baf0 YPTR=...b650 PDLPTR=...8c0 (pre-push)
A2 STARP2: PATBCL=...baf0 STREX_entrypdl=...8f0 PDLPTR=...950 fncdcl_found=0
  pdl[8f0] slot1={a=...8e8 f=1 v=2} case=27        <- SCFLCL
  pdl[920] slot1={a=0    f=0 v=48}                 <- ???
  pdl[950] slot1={a=0x60 f=0 v=0}                  <- *** BUG ENTRY ***
DSARP2 entry #1: PATBCL=...baf0 ...
STARP6 entry #2: PATBCL=...b650 YPTR=...b530 PDLPTR=...980
FNCA-SUCCESS placed FNCDCL at PDLPTR=...a10
A2 STARP2: PATBCL=...b650 STREX_entrypdl=...9b0 PDLPTR=...a10 fncdcl_found=1
  pdl[9b0] slot1={a=...8e8 f=1 v=2} case=27        <- SCFLCL
  pdl[9e0] slot1={a=0    f=0 v=0}                  <- harmless zero
  pdl[a10] slot1={a=...9a8 f=1 v=2} case=40        <- FNCDCL
  ZERO pdl[9e0]
```

### What this proves

- **STARP2 #1 fires BEFORE FNCA**. The bug entry `{a=0x60}` at PDL position
  950 sits in STARP2 #1's leak region. At this moment, **no FNCDCL exists
  anywhere on PDL** because FNCA hasn't run yet.
- **FNCA only runs AFTER STARP2 #1**, inside DSARP2 #1 → STARP6 #2.
- STARP2 #2 (FNCA's enclosing STARP2) DOES contain FNCDCL — but its leak
  region only has a benign zero entry, not the bug entry.

### Why the bounded gate fails

A2 with `FNCDCL-anywhere-PDLHED..PDLPTR` returns `fncdcl_found=0` at
STARP2 #1 (no FNCDCL exists yet) and `fncdcl_found=1` at STARP2 #2 (FNCDCL
visible). Zeroing fires on STARP2 #2's region — but the bug entry isn't
there. STARP2 #1's region is left untouched. Bug persists.

### What's actually leaking the `{a=0x60}` entry

The bug entry is pushed during STARP6 #1's inner SCIN call **before**
DSARP2 #1 (which dispatches `*cmd`) and **before** STARP6 #2 (which
dispatches the FENCE).

So the leak is NOT from FENCE's inner alt-conts. It's from some pattern
matching activity happening INSIDE STARP6 #1's SCIN that pushes a non-FNC
trap and never cleans it up. The walker later mis-routes through it
because the STARP2 #1 success path installs STREXCCL sentinels — and the
top STREXCCL switches PATBCL=cmd. Under cmd's PATBCL, the offset 0x60
maps to a real pattern node (the 'ab' alt of `'a'|'ab'`). Match succeeds
spuriously.

### What STARP6 #1 actually is

STARP6 #1 has PATBCL=...baf0 (the OUTER caller's PATBCL), YPTR=...b650
(which equals STARP6 #2's PATBCL). So STARP6 #1 is dispatching the
pattern at b650 — which is the outer pattern (likely the `*outer`
DSAR-equivalent before DSARP1's I/P/S type-check downgraded it to STARP6).

Wait — the trace shows STARP6 entries, not DSARP entries. But `*outer`
is `*var` which is DSAR. Hmm. Likely STARP6 #1 is `STAR(...)` from
within outer's RHS pattern compilation, OR from the pattern `s POS(0)
*outer RPOS(0)` where some sub-expression uses STAR.

(Investigation incomplete — the exact identity of STARP6 #1 needs more
tracing. But the key observation — that STARP2 #1 fires before any FENCE
machinery — is established beyond doubt.)

---

## Implications for next session (#68)

**Session #66's recommended fix shape (A2 with FNCDCL-in-region) is too
narrow.** The bug entry lives in a STARP2 region where FNCDCL hasn't been
placed yet. The right fix must either:

### Candidate 1: Side-channel FENCE-active flag

Add a global counter `fence_active`:
- Increment at FNCA entry.
- Decrement at FNCD dispatch AND at FNCBX (FENCE failure path).
- Decrement when the SCIN frame containing FNCA returns to caller (need
  to track which scope owns it).
- A2 zeroing fires at L_STARP2 when `fence_active > 0` AT ANY POINT during
  the STARP2's SCIN call (not just at STARP2 entry).

This catches "FENCE will run later inside this scan" via detection that
FNCA has been entered before the scan completes. But the timing is wrong
— STARP2 fires AFTER its SCIN returns, which is AFTER any FNCA runs in
that SCIN, BUT the STARP2 #1 leak entry is pushed BEFORE STARP6 #2 (which
runs FNCA). So `fence_active` would be 0 throughout STARP6 #1's SCIN.

### Candidate 2: Track FNCAFN ownership at trap-push time

Every SCIN3 push tags slot[1] with metadata indicating whether it was
pushed inside a FENCE-sealable context. At STARP2 zeroing, only zero
entries with this metadata.

This requires modifying every SCIN3 push site — invasive but precise.

### Candidate 3: Re-frame as "leak detection at walker time"

Don't zero at STARP2. Instead, at STREXCCL's L_STREXC handler, before
switching PATBCL, validate that the next PDL entry the walker will read
is a legitimate alt-cont under the new PATBCL. If the slot[1] offset is
out of range for the new PATBCL's compiled pattern, treat it as garbage
and SALT3-skip it.

This requires knowing the size of cmd's compiled pattern. PATBCL pattern
nodes have headers with size info — can be computed.

### Candidate 4: Walk PDL deeper than the leak region

Modify A2 to walk **all the way down to PDLHED**, not just from
STREX_entrypdl. Zero non-FNC entries between PDLHED and PDLPTR, gated
on FNCDCL-anywhere or unconditional.

This may break pattern primitives that rely on PDL state below
STREX_entrypdl. Risky.

### Candidate 5: Investigate STARP6 #1's identity

Find out what pattern primitive STARP6 #1 actually dispatches. If it's
something specific (like a particular STAR variant), maybe its OWN
success path needs a cleanup, not the generic STARP2 success path.

The most architecturally clean fix likely lives at STARP6 #1's
dispatcher — wherever pushed the `{a=0x60}` entry.

---

## Recommended session #68 plan

1. Apply `docs/F-2-Step3a-session67-A2-attempt.diff` (composed fix:
   L_FNCD + A2-with-PDLHED-gate, gains test 124, all floors preserved).

2. Apply `docs/F-2-Step3a-session67-diagnostic.diff` for trace
   instrumentation.

3. Identify STARP6 #1's identity: instrument SCIN3 with a "trap pushed
   from PATBRA case X" tag, run test 148, find which PATBRA case
   pushed the `{a=0x60}` entry. The entry lives in STARP6 #1's region,
   was pushed during STARP6 #1's SCIN call.

4. If the source primitive is identifiable (e.g. a SCIN3-around-some-
   pattern-node), examine its success-path cleanup. Likely it's missing
   a PDLPTR rewind step that ATP/BAL/EXPVAL have but this primitive
   doesn't.

5. Test gates: guard5, fence_function 10/10, Tier F 16/16, fence_suite
   ≥51/53 (ideally ≥52, with bug-2 = 127/152 separately addressable).

6. If beauty advances ≥500 lines: commit + close F-2 Step 3a.

---

## Files this session

- `csnobol4/docs/F-2-Step3a-session67-A2-attempt.diff` — clean composed
  patch (54 lines): L_FNCD goto-L_TSALT + A2 zeroing with PDLHED gate.
- `csnobol4/docs/F-2-Step3a-session67-diagnostic.diff` — diagnostic
  instrumentation (env-gated `A2_TRACE=1`): STARP6/DSARP2 entry counters,
  A2 STARP2 PDL dump with FNC-case identification, FNCA-success FNCDCL
  placement log.
- `csnobol4/docs/F-2-Step3a-session67-findings.md` — this document.
- This goal-file update + PLAN.md state cell.
- No runtime source committed.

## Honest checkpoint

Sessions #44–#67 = 24 sessions on F-2 Step 3a. fence_function preserved
10/10. Tier F preserved 16/16 since #55. fence_suite at 46/7/0 since #65;
session #67 bumps to 47/6/0 (Cluster B closed via L_FNCD fix, not
committed).

**Session #67's genuinely-new contribution:** trace-evidence refutation of
session #66's "leaked inner-FENCE alts" framing. The bug entry sits in a
STARP2 region BEFORE FENCE machinery has run anywhere on PDL. The leak
source is something OTHER than FENCE's inner alts. Five new fix candidate
shapes documented for session #68.

The pattern of "narrow the bug, hit deeper architectural question"
continues. Session #67 did NOT find the source primitive that pushes the
bug entry — that is session #68's primary diagnostic task.

---

## Session #67 update — Byrd Box FENCE implementation (per user direction)

After the A2 attempts, user requested a structurally different approach:
implement FENCE as a proper Byrd Box (4 ports: alpha/beta/gamma/omega) with
per-instance local storage, replacing the cstack PUSH/POP save/restore.

### Implementation

`csnobol4/docs/F-2-Step3a-session67-BB-FENCE.diff` (202 lines).

Key design decisions:

1. **C-stack-local storage** for all per-FENCE saves. Declared as
   `struct fnc_bb` at top of `SCIN1` — each SCIN1 frame owns its own.
   Nested FENCEs each get their own SCIN1 frame (FNCA calls SCIN → SCIN1).
   Sequential FENCEs in one frame each clobber `fnc_bb` at their alpha,
   but each prior FENCE's beta has already installed its PDL trap and no
   longer needs `fnc_bb`.

2. **Two-trap protocol for the seal:**
   - alpha pushes **SCFLCL** (inner-base sentinel — bottoms out the walker
     if P fails entirely during P-internal backtrack)
   - beta replaces it **in-place** with **FNCDCL** (the seal — walker
     dispatching this just continues past via `goto L_TSALT`)

3. **PDL truncation at beta** — `D_A(PDLPTR) = fnc_bb.fence_trap_pos;`
   drops any leaks P pushed during matching. Makes the seal physical.

4. **No cstack PUSH/POP** — entire save/restore is C-locals. Immune to
   RSTSTK and nested-SCIN cstack manipulation. The FNCA save/restore bug
   class (sessions #37–#48) is **structurally eliminated**.

### Gate results

| Gate | s64 baseline | s67 A2-attempt | **s67 BB-FENCE** |
|------|--------------|----------------|------------------|
| guard5 | OK | OK | **OK** ✓ |
| fence_function | 10/10 | 10/10 | **10/10** ✓ |
| Tier F (132–147) | 16/16 | 16/16 | **16/16** ✓ |
| fence_suite | 46/7/0 | 47/6/0 | **46/7/0** |

The BB version closes test 124 (Cluster B, like A2) but regresses test 150
(negative discriminator) — same trade-off as the L_FNCD-only attempt
session #65 hit. Net suite count unchanged.

Failures with BB:
- 119, 127, 129, 148, 149, **150**, 152

vs s67 A2 (composed) failures:
- 119, 127, 129, 148, 149, 152

So A2 is gate-incrementally better; BB is architecturally better.

### Why BB doesn't close Cluster A

Session #67's earlier diagnostic established Cluster A's bug entry is
pushed by ARBNO/STAR machinery **before** any FENCE machinery runs. BB
truncation only drops leaks **inside** P's matching scope. Leaks pushed
by outer ARBNO are below the FENCE trap on PDL — truncation cannot reach
them, and shouldn't (they belong to outer scope).

### Architectural verdict

The BB FENCE is **strictly cleaner** than the previous cstack-PUSH/POP
implementation:
- 9-PUSH/9-POP per call → 9 C-local assignments per call
- No RSTSTK hazard
- No SAVSTK/cstack interaction with nested SCIN
- 4 ports clearly labeled in code comments
- Truncation makes the seal physical rather than relying on walker
  cooperation

The 150 regression isn't a defect of BB design — it's the same residual
gap A2 zeroing covered. **A2 zeroing on top of BB FENCE would close 150**
without disturbing the BB structure (zeroing operates on outer-scope PDL,
BB operates on inner-scope PDL — orthogonal concerns).

### Recommended composition for session #68

1. Apply `docs/F-2-Step3a-session67-BB-FENCE.diff` (architectural foundation).
2. Apply A2 zeroing on top (the L_STARP2 hook with FNCDCL-anywhere gate)
   to recover test 150 and gain test 124 again.
3. Resulting gates expected: 47/6/0 with cleaner architecture than s64.
4. Cluster A still requires the STARP6 #1 source-primitive investigation
   for its remaining 4 tests + bug 2 for tests 127/152.

### Files this session (final)

- `csnobol4/docs/F-2-Step3a-session67-A2-attempt.diff` — composed L_FNCD
  + A2-with-PDLHED-gate (54 lines, gives 47/6/0).
- `csnobol4/docs/F-2-Step3a-session67-BB-FENCE.diff` — Byrd Box FENCE
  (202 lines, gives 46/7/0 with cleaner architecture).
- `csnobol4/docs/F-2-Step3a-session67-diagnostic.diff` — instrumentation
  (96 lines, env-gated `A2_TRACE=1`).
- `csnobol4/docs/F-2-Step3a-session67-findings.md` — this document.
- No runtime committed. Working tree: only docs untracked.

### Honest checkpoint

Session #67 produced two competing patches (A2 and BB), each gate-equivalent
or better than baseline, neither closing F-2 Step 3a's gate. The
genuinely-new contribution is the architectural insight that Cluster A's
bug is **outside FENCE** — established beyond doubt via tracing. No prior
session had isolated this. Session #68 should compose BB+A2 and pivot
diagnosis to STARP6 #1's source primitive.
