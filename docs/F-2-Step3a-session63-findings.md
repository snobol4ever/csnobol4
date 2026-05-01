# F-2 Step 3a Session #63 Findings

## What was attempted

Per session #61's "CRITICAL NEW FINDING" handoff: implement the in-place
SCIN3-FENCEPT trap overwrite at FNCA's success path.  The hypothesis was
that the FNCDCL seal currently sits ABOVE the SCIN3-FENCEPT trap, so the
walker reaches the seal first and then descends through the SCIN3 trap
which re-dispatches FNCA → wrong match.

The proposed fix: instead of pushing FNCDCL fresh at PDLPTR=P1 (the SCFLCL
position), leave PDLPTR at P0 (the SCIN3-FENCEPT position) and overwrite
slots [P0+DESCR..P0+3*DESCR] in-place with FNCDCL.  This eliminates the
SCIN3-FENCEPT trap entirely.

## Result: NO change

Implemented exactly as session #61 specified.  Built clean.  Ran all gates:

| Gate | Baseline | s63 attempt |
|------|----------|-------------|
| fence_function | 10/10 | 10/10 |
| fence_suite | 44/4/0 | **44/4/0** (no change) |
| guard5 | OK | OK |
| 5-line bug repro | wrong-match | **wrong-match** (no change) |

The same 4 tests (119, 124, 127, 129) still FAIL with `unexpected match`.
**Reverted before commit per RULES.md.**

## What this rules out

The session #61 hypothesis was partially wrong.  Removing the SCIN3-FENCEPT
trap from PDL does NOT change the wrong-match outcome.  Therefore that trap
is not the dispatch point that produces the wrong match.

## What was found instead — direct trace evidence

Added two env-gated diagnostics (saved as
`docs/F-2-Step3a-session63-diag.txt`):

1. `RPOS_TRACE=1` at L_RPSII entry — log XPTR/TXSP/XSP_len/MAXLEN.
2. `WALK_TRACE=1` at L_SALT2 entry — log PDLPTR, slot1/slot2/slot3,
   PATBCL, TXSP.

Run with `RPOS_TRACE=1 WALK_TRACE=1` on the 5-line repro
(`cmd=FENCE('a'|'ab'); outer=ARBNO(*cmd); s='ab'; s POS(0) *outer RPOS(0)`):

```
RPSII: XPTR=0 TXSP=0 XSP_len=2 MAXLEN=2
RPSII: TVAL=2 NVAL=0
SALT2: PDLPTR=...9a0 slot1=0    slot2=0    slot3=1  PATBCL=...ba40 TXSP=0
SALT2: PDLPTR=...970 slot1=b0   slot2=0    slot3=1  PATBCL=...ba40 TXSP=0
SALT2: PDLPTR=...9a0 slot1=...808 slot2=0  slot3=0  PATBCL=...b5a0 TXSP=0
SALT2: PDLPTR=...970 slot1=0    slot2=0    slot3=0  PATBCL=...ba40 TXSP=0
SALT2: PDLPTR=...940 slot1=...560 slot2=...b5a0 slot3=1 PATBCL=...ba40 TXSP=0
SALT2: PDLPTR=...910 slot1=60   slot2=0    slot3=1  PATBCL=...b5a0 TXSP=140702518392224
unexpected match
```

This trace is DIFFERENT from session #62's claim of "no SALT2 events
between STARP2 and unexpected match" — there ARE SALT2 events.  Session #62's
finding was specific to a different repro variant or a different
checkpoint set.  **6 SALT2 events fire** before "unexpected match".

## Decoded mechanism

1. **RPSII fires correctly** with TXSP=0, MAXLEN=2 → goto L_TSALF (clean
   failure).  RPOS arithmetic is fine.
2. Walker descends the PDL.  PATBCL switches between outer (...ba40) and
   cmd (...b5a0) as STREXCCL sentinels fire.
3. At SALT2 #5: slot1=...560 (heap pointer with FNC bit set), slot2=...b5a0
   (PATBCL of cmd).  This is an **STREXCCL sentinel firing** —
   restoring PATBCL=cmd (...b5a0) for the leaked region below.
4. At SALT2 #6 under PATBCL=cmd: slot1=0x60 (= 96 = 12*DESCR offset),
   FNC=0.  Walker takes SCIN3 fall-through (line 11500) under
   PATBCL=cmd.  **This is a leaked alt-cont from earlier processing.**
5. SCIN3 fall-through reads pattern node at PATBCL+0x60 — under cmd's
   PATBCL this is some valid-shaped descriptor.  Dispatches via PATBRA.
6. **TXSP becomes 140702518392224** — a heap-pointer-sized value.  This
   could only happen if `S_L(TXSP) = D_A(YCL)` was executed with a YCL
   that contains a heap pointer.  But SALT2 #6 shows YCL.a (slot2) = 0
   at entry.  So TXSP is being set elsewhere — likely inside the
   dispatched PATBRA case.
7. With TXSP=huge, eventually some primitive matches and `goto L_SCOK`
   propagates success.  Final match output: `unexpected match`.

## Why session #61's fix was insufficient

The session #61 fix targeted the wrong trap.  It tried to remove
SCIN3-FENCEPT (which lives BELOW the FNCDCL seal in the current layout).
But:

- SCIN3-FENCEPT is only relevant to the **outer-most SCAN's failure path**
  AFTER the SCAN call to `*cmd` returns.  It would only get dispatched
  if the walker descends past the SCFLCL frame of `*cmd` and the
  STREXCCL sentinels.
- The actual wrong-match path (per the trace) goes through STREXCCL
  sentinels and dispatches a leaked **alt-cont from inner-FENCE matching**
  (the `'a'|'ab'` alternation's then-or trap), under the wrong PATBCL.
- That leaked alt-cont sits ABOVE the SCFLCL/STREXCCL frame, in the
  "leak region" that session #58's paired sentinels were supposed to
  bracket.

## The remaining diagnosis question

Why does TXSP become 140702518392224 between SALT2 #5 and SALT2 #6?

Candidates:
- **(a)** The dispatched PATBRA case at SALT2 #5 (FNC-flagged trap with
  slot1=...560) does something that sets TXSP from a stale register.
  This is the STREXCCL handler — `D(PATBCL) = D(YCL); goto L_SALT3;`
  — which only writes PATBCL, not TXSP.  But L_SALT3 → L_SALT1 →
  L_SALT2.  L_SALT1 reads `D(LENFCL) = D(PDLPTR + 3*DESCR)`.  But TXSP
  isn't written there either.
- **(b)** Some SCIN3 trap dispatched between SALT2 #5 and SALT2 #6 sets
  TXSP via the SCIN3-trap-pop path: line 11498 `S_L(TXSP) = D_A(YCL);`
  reads YCL from the slot[2] of a trap.  If a trap's slot[2] contained
  a heap-pointer-shaped value, this would explain it.
- **(c)** A pattern primitive (e.g., L_SPNC5 line 11808-11809
  `S_L(TXSP) += D_A(XPTR);`) executes with XPTR=heap-pointer-shaped.

Looking at the trace again: SALT2 #5's slot2 was `...b5a0` (heap pointer
= cmd PATBCL).  After SALT2 #5, `D(YCL) = D(PDLPTR+2*DESCR)` reads that
same slot2 = ...b5a0 into YCL.  Then L_PATBRA dispatches based on slot1.

But SALT2 #6's TXSP = 140702518392224.  Decoding hex of cmd PATBCL was
...b5a0.  Let me check: 140702518392224 in hex = 0x7FF7DBA0B5A0.  **THAT
IS PATBCL of cmd** (the same value session #61's session #54 trace also
saw).  So TXSP got assigned PATBCL!

**This happens when SCIN3 fall-through executes under wrong PATBCL/slot
state.**  Specifically, SCIN3 line 11498:
```c
S_L(TXSP) = D_A(YCL);
```
fires after popping a trap, where YCL came from slot2 of that trap.

But which trap?  Looking between SALT2 #5 and #6: SALT2 #5's slot1 =
...560 (FNC) — STREXCCL — handler does `D(PATBCL) = D(YCL); goto L_SALT3`.
L_SALT3 → L_SALT1 → reads slot3 → L_SALT2 #6.  No `S_L(TXSP) = ...` in
that path... unless within SALT2 itself.

Looking at SALT2 line 11498: `S_L(TXSP) = D_A(YCL);` — this fires for
NON-FNC traps before the SCIN3 fall-through.  SALT2 #5 was FNC (took
PATBRA dispatch path, did NOT execute line 11498).

But wait — at SALT2 #6: slot1=0x60 (FNC bit not set), so line 11499
`if (!(D_F(PATICL) & FNC)) goto L_SCIN3;` fires.  Right BEFORE that,
line 11498 `S_L(TXSP) = D_A(YCL);` fires.  YCL was loaded from slot2
at SALT2 #6 entry: slot2=0.  So TXSP should become 0, not the heap pointer.

But the trace shows TXSP=140702518392224 IN SALT2 #6 (i.e., AT ENTRY).
That's BEFORE line 11498 fires.  So TXSP was already corrupted between
SALT2 #5's exit and SALT2 #6's entry.

Re-reading SALT2 sequence: SALT2 #5 fires, dispatches via PATBRA case 41
(STREXCCL).  L_STREXC: `D(PATBCL) = D(YCL); goto L_SALT3;`.  L_SALT3:
`if (D_A(LENFCL) != 0) goto L_SALT1; goto L_SALF1;`.  LENFCL was loaded
in the path to SALT2 — earlier from L_SALT1 at line 11490
`D(LENFCL) = D(D_A(PDLPTR) + 3*DESCR);`.  That doesn't touch TXSP either.

So **between SALT2 #5 (FNC-flagged dispatched as STREXCCL) and SALT2 #6,
TXSP went from 0 to 140702518392224 = cmd's PATBCL value**, but no code
in the STREXCCL/SALT3/SALT1/SALT2 path writes TXSP.

**Conclusion: there must be ANOTHER SALT2 event missed by the trace, or
TXSP is being aliased/clobbered by some non-obvious path.**  The trace
needs more granularity:

1. Log at every L_SCIN3 fall-through entry.
2. Log at every place TXSP is written (every `S_L(TXSP) = ...`).
3. Log at every L_PATBRA case dispatch.

## Recommended session #64 plan

1. Apply `docs/F-2-Step3a-session63-diag.txt` for RPSII + SALT2 trace
   baseline.
2. Add additional traces:
   - At every `S_L(TXSP) = ...` write site in `isnobol4.c`.
   - At every L_PATBRA case dispatch (log the case number + PATBCL +
     PATICL + YCL + TXSP).
   - At L_SCIN3 entry (log PATBCL, PATICL, TXSP).
3. Run repro5 with `TXSP_TRACE=1 PATBRA_TRACE=1 SCIN3_TRACE=1`.
4. Identify the exact instruction that sets TXSP to PATBCL's value.
5. Fix candidates depend on what's found:
   - If TXSP is set by a SCIN3 push that was given wrong YCL: fix the
     trap-creation site.
   - If TXSP is set by a primitive (e.g., L_TBII) under wrong YCL/XPTR:
     the issue is the primitive being dispatched under wrong PATBCL,
     so the leaked alt-cont needs to be either zeroed or unreachable.

## Files this session

- `csnobol4/docs/F-2-Step3a-session63-findings.md` — this file.
- `csnobol4/docs/F-2-Step3a-session63-diag.txt` — diagnostic patch.
- No code changes committed.  Working tree clean.

## Honest checkpoint

Sessions #44–#63 = 19 sessions on F-2 Step 3a.  fence_function preserved
10/10.  Tier F preserved 16/16 since #55.  fence_suite stuck at 44/4/0
since #58.  Beauty stuck at 42 lines since #58.

Session #63's genuine new contribution: **direct trace evidence that
the wrong-match path goes through 6 SALT2 events** (refining session
#62's incomplete claim of zero events), and **identification that TXSP
becomes corrupted with a heap-pointer-shaped value (= cmd PATBCL)**
during the walker descent.  This is a concrete handle on the bug:
TXSP corruption, not pattern dispatch corruption.

The bug is now narrowed to: "between SALT2 #5 (STREXCCL fire) and
SALT2 #6 (next descent step), TXSP is mysteriously set to cmd's PATBCL
value."  Session #64 has a clear path: instrument every TXSP write
site and find the exact line that does it.
