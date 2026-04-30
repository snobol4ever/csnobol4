# F-2 Step 3a — session #57 findings (PATBCL-write + SALT2-read trace; multi-iteration ARBNO leak)

GOAL-CSN-FENCE-FIX F-2 Step 3a continuation (session #57, 2026-04-30).

## TL;DR

Session #57 followed the session #56 plan: applied
`docs/F-2-Step3a-session56-strexccl-attempt.diff`, then instrumented
**all** PATBCL-touching sites in `isnobol4.c` (3 `D(PATBCL) = ...`
writes + 7 `POP(PATBCL)` cstack restores + every SALT2 entry).

The trace on test 119 **refutes both** session #54's hypothesis ("leaked
traps under wrong PATBCL because no DSAR-redo route") and session #56's
hypothesis ("upstream write sets PATBCL=inner before sentinel fires").

The actual mechanism is **multi-iteration ARBNO leak**: STREXCCL only
protects the most recent `*cmd` iteration's leak region. Earlier
iterations' leaks remain BELOW that STREXCCL on PDL. When outer's
failure walker descends past STREXCCL, fires it (now PATBCL=outer-of-
that-iteration), gets popped via STARP5 (PATBCL=outermost), then
**continues walking down into iteration #2's unprotected leak region**
under outer PATBCL.

The crash is at L_SCIN3 dispatching slot `{a=0x200, f=0, v=96}` under
`PATBCL=outer`: reads `D(outer + 0x200)` → garbage → `ZCL=NULL` →
`D(D_A(ZCL))` segfaults at `isnobol4.c:11521`.

Per RULES.md (no fix landed = nothing to commit), all instrumentation
reverted from working tree. Diagnostic patch saved as
`docs/F-2-Step3a-session57-diagnostic.diff` for future reuse.

## What session #57 did

1. **Applied** `docs/F-2-Step3a-session56-strexccl-attempt.diff` (clean,
   90 lines). Verified gates at s56 baseline:
   - fence_function: 10/10 ✅
   - fence_suite: 43 OK / 3 FAIL / 2 CRASH (matches session #56)
   - guard5: ✅

2. **Instrumented all 3 PATBCL-write sites** in `isnobol4.c`:
   ```
   line ~11479 (L_SCIN1A):  D(PATBCL) = D(YPTR);   - SCIN1 entry
   line ~11620 (L_UNSC):    D(PATBCL) = D(YPTR);   - DSAR-redo path
   line ~12459 (L_STREXC):  D(PATBCL) = D(YCL);    - STREXCCL sentinel
   ```
   These are the **only 3 sites in the entire file** that write
   `D(PATBCL)` — found via
   `grep -nE 'D_A\(PATBCL\)\s*=|D\(PATBCL\)\s*='`.

3. **Instrumented all 7 POP(PATBCL) sites**:
   - L_EXPV7 (line ~7794)
   - L_ATP2  (line ~12112)
   - L_STARP2 (line ~12264) — *cmd success path
   - L_STARP5 (line ~12284) — *cmd fail path
   - L_FNCA  (line ~12405) — FENCE success seal-install
   - L_FNCBX (line ~12441) — FENCE fail propagation
   - L_ENMI4 (line ~12626)

4. **Instrumented L_SALT2 entry** to log every PDL slot the failure
   walker reads, including PDLPTR, current PATBCL, slot[1] descriptor
   contents, and dispatch path (FNC vs SCIN3 fallthrough).

5. **Captured full trace on test 119** (CRASH test from fence_suite
   Tier C — canonical beauty stmt 1074 mini-repro). 69 events ending in
   the segfault.

6. **gdb backtrace + descriptor inspection** at the crash site:
   ```
   SCIN1 at isnobol4.c:11521
   11521    D(PTBRCL) = D(D_A(ZCL));

   PATBCL = {a=0x7ffff740bb60, f=16 (PTR), v=3}
   PATICL = {a=0x230, f=0, v=96}            ← non-FNC, large offset
   ZCL    = {a=NULL, f=0, v=0}              ← read garbage from outer+0x230
   PDLPTR = 0x555555607ae0
   LENFCL = 0
   ```

## Address-space legend (test 119 trace)

The trace has 4 distinct pattern objects:

- `bb50` / `bb60` — outer pattern (`*outer RPOS(0)`)
- `b6b0`         — outer (`= ARBNO(*cmd)`)
- `b590`         — cmd (`= FENCE('a' | 'ab')`)
- `b500`         — cmd's inner alternation (`'a' | 'ab'`)

## The decisive trace (last 12 events before crash)

```
PATBCL-WRITE @SCIN1A:  PATBCL b6b0 -> b590  (PDLPTR=0x...c30)  [DSAR-redo iter#3 SCIN1 entry]
PATBCL-WRITE @UNSC:    PATBCL b590 -> b590  (PDLPTR=0x...c30)
SALT2-READ:            PDLPTR=0x...c30  PATBCL=b590  slot={a=NULL,f=0,v=256}  [SCIN3 fallthrough]
SALT2-READ:            PDLPTR=0x...c00  PATBCL=b590  slot={a=...8610,f=1,v=2} [FNC dispatch]
PATBCL-WRITE @STREXC:  PATBCL b590 -> b590  (PDLPTR=0x...bd0)  [STREXC fires - already inner]
SALT2-READ:            PDLPTR=0x...bd0  PATBCL=b590  slot={a=...9a8,f=1,v=2}  [FNC dispatch]
PATBCL-POP   @STARP5:  PATBCL b590 -> b6b0  (PDLPTR=0x...b70)
SALT2-READ:            PDLPTR=0x...b70  PATBCL=b6b0  slot={a=...8e8,f=1,v=2}  [FNC dispatch]
PATBCL-POP   @STARP5:  PATBCL b6b0 -> bb50  (PDLPTR=0x...b40)  ← OUTER restored
SALT2-READ:            PDLPTR=0x...b40  PATBCL=bb50  slot={a=NULL,f=0,v=256}  [SCIN3 fallthrough]
SALT2-READ:            PDLPTR=0x...b10  PATBCL=bb50  slot={a=0x90,f=0,v=192}  [SCIN3 fallthrough]
SALT2-READ:            PDLPTR=0x...b10  PATBCL=bb50  slot={a=NULL,f=0,v=0}    [SCIN3 fallthrough]
SALT2-READ:            PDLPTR=0x...ae0  PATBCL=bb50  slot={a=0x200,f=0,v=96}  [SCIN3 fallthrough]
                                                          ^^^^^
                                                  CRASH TRIGGER
```

The slot at `PDLPTR=0x...ae0` has `{a=0x200, f=0}` — a non-FNC,
540-byte offset. Under `PATBCL=outer (bb50)`, L_SCIN3 reads
`D(bb50 + 0x200)` = some descriptor whose `.a` is NULL → ZCL=NULL →
crash at line 11521.

## Geometric interpretation: ARBNO leak stratification

Walking PDL addresses backward through the trace, the leak structure is:

```
PDL offset    Iteration     Content                   Protected by
-----------   ----------    -----------------------   ---------------
0x...d80      iter#3 leak   inner alt traps           STREXCCL@iter#3
0x...d20      iter#3 base   SCFLCL                    (consumed by FAIL)
0x...d20      iter#2 leak   inner alt traps           STREXCCL@iter#2  ← STREXCCL@iter#2 ALREADY POPPED!
0x...cc0      iter#2 base   SCFLCL                    (consumed)
0x...c30      iter#1 leak   inner alt traps           STREXCCL@iter#1  ← ALREADY POPPED!
0x...bd0      iter#1 base   SCFLCL                    (consumed)
0x...b40      outer base    *outer trap               ← walker reaches here, then descends
0x...b10      outer leak    leaked SCIN3 trap from   UNPROTECTED — CRASH POINT
              0x90, 0x200    unrolled ARBNO success
```

Each ARBNO `*cmd` iteration that **succeeded** left its STREXCCL +
inner-alt-leaks on PDL. When the outer pattern's tail (`RPOS(0)`)
fails, ARBNO's own retry mechanism unwinds the iterations one-by-one
(via STARP5 path each time). **Each STARP5 pops PATBCL from cstack but
does NOT remove that iteration's inner-alt-leaks from PDL.**

After the final outer-PATBCL restore, the walker is back to outer's
PATBCL but PDLPTR is still pointing into the *region of leaked traps
from earlier successful ARBNO iterations*. Those leaks have `f=0`
(non-FNC) slots whose offsets are valid only relative to inner-cmd, not
outer. SCIN3 fallthrough crashes.

## Why STREXCCL alone cannot fix this

STREXCCL as installed by session #56 is **per-iteration** and **at the
top** of the leak region. Each iteration's STREXCCL is consumed by
SALT2 when the walker descends past it within that iteration's
context. Once consumed (iteration ends, walker moved on), the
STREXCCL is gone. The earlier iterations' leaks remain unprotected.

The session #56 trace on test 114 saw STREXCCL fire with PATBCL
already=inner. Session #57 explains why: in test 114, the path that
reaches STREXCCL is the DSAR-redo path which already passed through
L_UNSC (PATBCL=inner). STREXCCL's `D(PATBCL)=D(YCL)` write is a no-op
in that case. STREXCCL is doing nothing wrong; it's just **never the
trap that gets reached on the failing path**.

## Sharpened diagnosis (refines sessions #50, #54, #56)

- Session #50 said: "PATBCL context mismatch on slot[1] read." TRUE.
- Session #54 said: "Walker reaches leaks AFTER consuming all
  DSAR-redo entries; STREXCCL fixes by setting PATBCL=inner at the
  top of the region." TRUE in shape, INSUFFICIENT in scope.
- Session #56 said: "PATBCL is already inner when STREXCCL fires —
  some upstream write does it." TRUE on the DSAR-redo path but
  IRRELEVANT to the bug; the bug is on a path that doesn't touch
  STREXCCL at all.
- Session #57 says: **the leak is not just one region; it's a STACK
  of regions, one per ARBNO iteration. STREXCCL guards only the
  top-most.** Earlier iterations' leaks are walked under whichever
  PATBCL the walker happens to have at that PDL position, which is
  whatever STARP5 most recently restored.

## What the right fix looks like (candidates for session #58)

### (c) NEW — Make the STREXCCL pair persistent across iterations

Currently STREXCCL is pushed at STARP2 success and popped/consumed
within the same outer match cycle. Modify so that STREXCCL persists
across multiple `*var` iterations until the outer enclosing scope
(ARBNO etc.) actually rewinds PDL to before the iteration's start.

This is non-trivial because there's no obvious "outermost ARBNO
boundary" available at STARP2 time.

### (b refined) — Paired BOTTOM-of-region sentinel that switches PATBCL=outer

Push **two** sentinels per STARP6 call:

```
                 PDL state after STARP2 success
+----------------------------------------------------------+
| ... outer's traps ...                                    |
| BOTTOM sentinel: STREXCCL_BOT, slot[2]=outer_PATBCL      |  <- pushed at STARP6 ENTRY
| SCFLCL (inner SCIN's failure boundary)                   |
| inner's leaked SCIN3 traps                               |
| TOP sentinel: STREXCCL_TOP, slot[2]=inner_PATBCL         |  <- pushed at STARP2
+----------------------------------------------------------+
```

When walker descends from above:
1. Reaches STREXCCL_TOP → PATBCL=inner. Walks inner leaks safely.
2. Reaches SCFLCL → BRANCH(FAIL) — but this is wrong if we're walking
   under OUTER SCIN1's frame. SCFLCL only makes sense as inner SCIN1's
   own bottom.

Issue: SCFLCL's BRANCH(FAIL) returns from the *current* C function
frame. If we're in outer SCIN1's frame walking through stranded
SCFLCLs from earlier nested iterations, BRANCH(FAIL) is *premature
return*. May actually be why the crash signature differs from a
clean outer-failure return.

Need to read SPITBOL `p_str / =ndexc / =ndfnb` more carefully to see
how SPITBOL handles the "SCFLCL stranded in outer's PDL" case.

### (d) — Truncate PDL at STARP2 success, but copy alts to a "deferred" stack

Session #53's "naive rewind" was rejected because it broke guard5's
inner-alt backtrack. But guard5 needs only the alts immediately above
SCFLCL, not the whole region. A more surgical fix:

1. STARP6 saves entry-PDLPTR.
2. STARP2 success: walk PDL from entry-PDLPTR to current PDLPTR.
   For each non-FNC slot[1] (an inner alt), record `(slot[1].a, ZCL,
   YCL_value)` triples in a "deferred-alt" array attached to the
   inner pattern descriptor.
3. Truncate PDLPTR to entry-PDLPTR (drops SCFLCL + leaks).
4. If outer later DSAR-redo's *var, the redo handler re-pushes the
   deferred alts from the array (under inner PATBCL via L_UNSC, as
   today).

This decouples "alts as data" from "alts as PDL state." Risk: changes
inner pattern descriptor shape; may break pattern-equality tests.

## What session #57 did NOT do

- Did not modify any production code beyond the s56 patch (now reverted).
- Did not advance beauty self-host (still at the s56 baseline of 35
  lines clean Parse Error).
- Did not implement (b), (c), or (d). All three are credible but
  none has been verified.
- Did not read SPITBOL `p_str` / `flpop` interactions with stranded
  pattern-stack entries from successful prior iterations.

## Recommended session #58 plan

1. **Apply combined patch:** `docs/F-2-Step3a-session56-strexccl-
   attempt.diff` (s52 + STREXCCL).
2. **Optionally re-apply diagnostic:**
   `docs/F-2-Step3a-session57-diagnostic.diff` for trace re-runs.
3. **Read SPITBOL** `sbl.min` lines around `p_str` (≈12100s) and
   `=ndexc` (≈12213) and `flpop` (≈3144). **Specifically look for**:
   - How does SPITBOL clean up `xs` (its pattern-history stack) on
     `p_str` success?
   - If SPITBOL leaves alts on `xs`, what handles them when an
     outer fails the tail and walker descends through earlier
     iterations' leftovers?
   - Is there a SPITBOL test case analogous to test 119 (ARBNO of
     `*var` of FENCE)? What does SPITBOL do?

4. **Implement candidate (b refined) or (c)** based on what SPITBOL
   does. (d) only if both fail.

5. **Test gate stack** (in order):
   - guard5 must produce `inner backtrack worked`
   - Tier F all 16 must remain PASS
   - fence_function 10/10
   - fence_suite total ≥45/48
   - Beauty self-host ≥500 lines

6. Commit only if all gates pass.

## Files added this session

- `csnobol4/docs/F-2-Step3a-session57-findings.md` (this file)
- `csnobol4/docs/F-2-Step3a-session57-diagnostic.diff` (145-line patch
  adding env-gated PATBCL-write/POP loggers and SALT2-entry logger;
  reusable for future tracing)
- No runtime source changes committed. Working tree clean.

## Honest circularity check

Sessions #44–#57 = 14 sessions on F-2 Step 3a. fence_function preserved
10/10 throughout. Tier F preserved 16/16 since session #55. Beauty
stuck at 33–35 lines.

Session #57's genuinely-new contributions:

1. **Direct trace evidence**, not inferred via gdb post-crash, of the
   PATBCL state at every transition AND every PDL slot read by the
   walker. Previous sessions reasoned about the bug; #57 watched it.

2. **Identification of the multi-iteration ARBNO leak class.**
   Sessions #50/#54/#56 framed it as one region. It's a stack of
   regions, all unprotected except the top.

3. **Refutation of session #56's "upstream write" hypothesis.** There
   is no upstream write. There are exactly 3 PATBCL-write sites and
   they all behave correctly. The bug is not about *who writes
   PATBCL*; it's about *who walks under it*.

4. **A `docs/F-2-Step3a-session57-diagnostic.diff` reusable
   instrumentation patch** that future sessions can apply to
   re-investigate without re-deriving.

The pattern of "land a diagnostic, hit deeper structural issue"
continues. The next session's job is to read SPITBOL more carefully
and implement (b), (c), or (d).
