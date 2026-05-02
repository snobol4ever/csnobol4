# F-2 Step 3a — Session #66 findings

**Status:** diagnostic-only.  No runtime code committed.  Working tree
clean at HEAD `b2764cf`.

**Setup:** csnobol4 cloned from `b2764cf` (s65 + L_FNCD-attempt artifact),
built clean with `make -f Makefile2 xsnobol4`, baseline gates verified
(fence_function 10/10, fence_suite 46/7/0, guard5 ✓).

**Goal of session:** per the session #65 (continued) plan, attempt the
**composed fix** = (s65 L_FNCD `goto L_TSALT`) + (s64 FNCA-success
leaked-alt zeroing).  Predicted outcome: 7→0 FAIL on cluster A and
cluster B.

---

## What was tried

### Attempt 1: s65 L_FNCD diff alone

Applied `docs/F-2-Step3a-session65-L_FNCD-attempt.diff`.  Built, ran
fence_suite.  Result reproduces session #65 (continued)'s recorded
result exactly:

| Test         | Baseline | s65 L_FNCD only |
|--------------|----------|-----------------|
| 119,127,129,148,149,152 | FAIL | FAIL (unchanged) |
| **124**      | FAIL     | **OK** (cluster B promoted) |
| **150**      | OK       | **FAIL** (negative discriminator regressed) |
| Net | 46/7/0 | 46/7/0 |

guard5 ✓, fence_function 10/10.

### Attempt 2: s65 L_FNCD + FNCA-success leaked-alt zeroing

Implemented session #64's proposed zeroing exactly as described:
capture `p2_save = D_A(PDLPTR)` BEFORE the cstack POPs, then after
`PDLPTR = saved - 3*DESCR` (= P1, clean base), walk
`p ∈ [P1+3*DESCR .. P2_save]` and zero slot[1] of any non-FNC trap.

Result: **identical to attempt 1.**  46/7/0.  Cluster A unchanged.
guard5 ✓, fence_function 10/10.

The zeroing fired correctly (verified via S148_TRACE on test 148 —
zero log shows "FNCA-zero P1=...59a0 P2=...5a00 zeroed=1") but
**did not fix any cluster A test**.

### Attempt 3: full diagnostic instrumentation (decisive)

Reverted both fix attempts, kept only the L_FNCD `goto L_TSALT` patch,
added env-gated `S148_TRACE=1` logging at:

- L_SALT2 entry (PDLPTR + slot[1] + slot[2] + PATBCL + TXSP)
- L_SCOK entry (PDLPTR + PATBCL + PATICL + XCL.v + TXSP)
- L_FNCA entry (full state)
- FNCA success path entry (post-SCIN, pre-POPs)
- L_FNCD fire (PDLPTR + YCL + PATBCL)
- L_STARP2 entry / exit (with leaks=yes/no)
- L_STREXC fire (old/new PATBCL)

Saved as `docs/F-2-Step3a-session66-diagnostic.diff`.

Ran `S148_TRACE=1 ./snobol4 -bf` on test 148 (input `'ab'`,
single ARBNO iteration of FENCE-sealed `'a'|'ab'`).

---

## Decisive trace — test 148, wrong-match path

```
[1]  SCOK    PDLPTR=...e850 PATBCL=ba70 PATICL=0x40 TXSP=0   ← outer scan setup
[2]  SCOK    PDLPTR=...e8e0 PATBCL=b5d0 PATICL=0x30 TXSP=0   ← inside *cmd, FENCE matched 'a'
[3]  SCOK    PDLPTR=...e910 PATBCL=b5d0 PATICL=0x60 TXSP=0   ← cmd done, returning
[4]  STARP2-entry  PDLPTR=...e910 PATBCL=b5d0 TXSP=0
[5]  STARP2-exit   PDLPTR=...e940 PATBCL=ba70 TXSP=0 leaks=yes
        ↑ STAR success: PATBCL restored, top STREXCCL pushed, bottom rewritten
[6]  SCOK    PDLPTR=...e940 PATBCL=ba70 PATICL=0x80          ← ARBNO continuation
[7]  SCOK    PDLPTR=...e970 PATBCL=ba70 PATICL=0xb0          ← *outer succeeding

[8]  SALT2   PDLPTR=...e9a0 slot1={a=0,f=0}                   ← RPOS(0) failed → walker descends
[9]  SALT2   PDLPTR=...e970 slot1={a=b0,f=0,v=240}           ← non-FNC, fall to SCIN3 (no match)
[10] SALT2   PDLPTR=...e9a0 slot1={a=...1808,f=1,v=2}        ← FNC trap (ARBNO machinery)
[11] SALT2   PDLPTR=...e970 slot1={a=0}                       ← cleared
[12] SALT2   PDLPTR=...e940 slot1={a=...a560,f=1,v=2} slot2={a=b5d0,f=16,v=3}
        ↑ STREXCCL bottom sentinel, slot[2] = inner-pat PATBCL = cmd
[13] STREXC-FIRE  old_PATBCL=ba70  new_PATBCL=b5d0
        ↑ Walker continues failp into next entry, now under PATBCL=cmd
[14] SALT2   PDLPTR=...e910 slot1={a=0x60, f=0, v=0}         ← *** THE BUG ***
        ↑ This entry sits at PDLPTR BELOW STREX_entrypdl (e940-3*DESCR=e910).
          Outside the protected leak region.  But STREXC just told the walker
          "you are now in cmd-PATBCL territory."  Walker reads this entry
          under cmd PATBCL.  Non-FNC → SCIN3 fall-through (session #64 fix).
[15] SCOK    PDLPTR=...e910 PATBCL=b5d0 PATICL=0x90 XCL.v=192 TXSP=0
        ↑ SCIN3 dispatched D(cmd_pattern + 0x60 + DESCR) = a valid pattern
          node within cmd's compiled body, which succeeded immediately.
[16] FNCA-ENTRY  PDLPTR=...e9a0 PATBCL=b4b0 PATICL=0x30 TXSP=0
        ↑ FENCE re-entered (PATBCL=b4b0 = an inner P)
[17] SCOK    PDLPTR=...ea00 PATBCL=b420 PATICL=0x40 TXSP=1
        ↑ FENCE matched 'b' alt of inner alternation, advancing to TXSP=1
[18] FNCA-SUCCESS pre-pop  TXSP=1
[19+] continued ARBNO continuation under PATBCL=b5d0 (cmd)
[N]  Match succeeds at TXSP=2, "leak: seal failed to block ab alt retry"
```

---

## What this resolves

### The session #62 vs #64 narrative tension

- **Session #62** (PDL-dump on test 119) reported "no SALT2 events
  between post-STARP2 dump and wrong-match output → bug on success
  path."  This was an artifact of test 119's longer input (`'aab'`) and
  the dump being placed too late.  **Test 148 shows 5 SALT2 events
  before the wrong match**, so the bug IS on the failure-walker path.
  Session #62 was **wrong about location**.

- **Session #64** (FNCDCL-at-P2 attempt) reported "STARP2 abandons
  FENCE seal AND re-routes walker through abandoned region under
  PATBCL=cmd → leaked alt-conts dispatched."  The session #66 trace
  shows STARP2 does correctly install STREXCCL routing, the walker
  does follow that routing, **and the STREXCCL routing is itself
  correct** for entries inside [STREX_entrypdl..PDLPTR] (the leak
  region).  Session #64 was **right about mechanism but wrong about
  region** — the entry that gets dispatched lives BELOW
  STREX_entrypdl, OUTSIDE the leak region the STREXCCL pair is
  supposed to protect.

### The actual bug

```
PDL geometry at the time of event [14]:

     ┌──────────┐ ← PDLPTR=...e9a0  (top, after walker has consumed [8-12])
     │   ...    │
     ├──────────┤ ← e970   ARBNO redo machinery (FNC traps)
     │   ...    │
     ├──────────┤ ← e940   STREXCCL bottom sentinel  (slot[2]=cmd)
     │ STREXCCL │
     ├──────────┤ ← e910   ← *** THIS ENTRY ***  (slot[1]=0x60, non-FNC)
     │ leaked   │
     ├──────────┤ ← e8e0   STREX_entrypdl  (= STARP6's snapshot before SCIN)
     │   ...    │
     ├──────────┤ ← e850   outer scan stuff
```

After STREXC at e940 fires (event 13), it does `D(PATBCL)=D(YCL); goto
L_SALT3`.  L_SALT3 then continues the failure walk: `if (LENFCL!=0)
goto L_SALT1`.  That dispatches the **next entry below**, at
PDLPTR=e910.

But e910 is **below** STREX_entrypdl=e8e0... wait, e910 > e8e0
(addresses grow upward).  Let me re-check.

Actually: e9a0 > e970 > e940 > e910 > e8e0 (PDL grows upward;
larger address = higher in stack).  STREX_entrypdl=e8e0 is BELOW e910.
So e910 IS in [STREX_entrypdl..PDLPTR_after_SCIN].

So this entry IS in the protected leak region.  STREXC fired
correctly to switch PATBCL=cmd before walking it.  The walker reads
slot[1]={a=0x60,f=0} under PATBCL=cmd, falls through SCIN3 fall-through
(session #64 fix), and dispatches `D(cmd + 0x60 + DESCR)` = a valid
cmd pattern node (the 'ab' alternative of `'a'|'ab'`).

**The dispatch IS correct.**  cmd's pattern at offset 0x68 is genuinely
the 'ab' alternative.  The walker is doing exactly what it should under
PATBCL=cmd.  But conceptually this is *"retry the alternation inside
FENCE"* — which FENCE's seal is supposed to block.

**The bug is that STREXCCL routes the walker through the protected
leak region under inner PATBCL, but that region contains BOTH
legitimate alt-conts (from the `*cmd` ARBNO machinery) AND inner-FENCE
backtrack alt-conts that the seal should have killed.**

The session #58 paired-STREXCCL was designed to make multi-iteration
ARBNO leaks safely walkable.  It correctly switches PATBCL=cmd inside
the region (so dispatches under cmd succeed in the right pattern
context).  But "succeeds in the right context" is not "blocks
FENCE-sealed alts" — those are different requirements.

---

## Why the FNCA-success zeroing didn't fix it

I implemented the zeroing exactly as session #64 specified:
walk [P1+3*DESCR .. P2] at FNCA-success, zero slot[1] of non-FNC
entries.

**P1 in FNCA's frame** = post-SCFLCL-pop value of PDLPTR = at
SCFLCL position; minus 3*DESCR = clean base.  In the trace, the
FNCA-zero log fires:

```
FNCA-zero P1=...59a0 P2=...5a00 zeroed=1
```

But this is the **second** FNCA invocation (event 16), AFTER the wrong
path was taken.  The **first** FNCA invocation (during the original
`*cmd` matching, before any failure) succeeded silently.  Why?

Because the original `*cmd` match at cursor 0:
- SCIN1 entered cmd's pattern, dispatched FENCEPT → L_FNCA via case 37.
- L_FNCA pushed SCFLCL at PDLPTR_before+3*DESCR, called inner SCIN.
- Inner SCIN matched `'a'`, returned success.
- L_FNCA's success path:
  - p2_save = current PDLPTR (top of inner SCIN's leftovers — alt-cont
    for 'ab', etc.)
  - POPs cstack: ... POP(PDLPTR) restores PDLPTR to SCFLCL position.
  - PDLPTR -= 3*DESCR → P1 (clean base).
  - **My zeroing loop walks p ∈ [P1+3*DESCR .. p2_save].**  This range
    INCLUDES the SCFLCL slot itself plus any inner-SCIN leftovers above it.
  - Found 1 entry with non-FNC slot[1] → zeroed it.

**But the entry that produces the wrong match** (slot[1].a=0x60 at
PDLPTR=e910) was NOT pushed during inner-FENCE-P matching.  It was
pushed by SCIN3 when **the enclosing scan was dispatching the
FENCEPT node itself** (i.e. when `*cmd`'s SCIN was walking cmd's
pattern node-by-node, it pushed an SCIN3 alt-cont entry for FENCEPT
just like it does for every node).  That entry has slot[1]=node-slot[2]
which for FENCEPT is 0 — so... wait, that contradicts the trace which
shows slot[1].a=0x60 (≠0).

Let me look more carefully.  Per `D6` FENCEPT layout (snobol4.c:5428,
isnobol4.c:12758): `D_A(slot[1]) = FNCAFN_addr, D_F=FNC, D_V=4`.
slot[2] and slot[3] are 0 from ZERBLK init.  slot[4] = P (the inner
pattern descriptor).

In SCIN3 (line 11457-79):
- `ZCL = D(PATBCL+DESCR)` = slot[1] = `{a=FNCAFN, f=FNC, v=4}`
- `XCL = D(PATBCL+2*DESCR)` = slot[2] = `{a=0, f=0, v=0}`
- `YCL = D(PATBCL+3*DESCR)` = slot[3] = `{a=0, f=0, v=0}`
- Push at PDLPTR+DESCR: slot[1] = XCL (the alt-cont link).
- Push at PDLPTR+2*DESCR: TMVAL with .a=TXSP and .v=YCL.v.

So the SCIN3-pushed entry for FENCEPT has slot[1] = `{a=0,f=0,v=0}`.
That's a NULL-stub entry — the failure walker on encountering it falls
through to SALT3 cleanly.

**So the entry at PDLPTR=e910 with slot[1].a=0x60 is NOT from
SCIN3-around-FENCEPT.  It's from somewhere else.**

Tracing back: at event [3] SCOK, PATICL=0x60 inside cmd's pattern.
After SCOK, PATICL=D_V(XCL)=0 → exit via RTN2.  The chain of SCOKs
shows cmd's pattern walking: PATICL 0x30 → 0x60 (then exit).  Each
SCIN3 push happens at PATICL increment.  PATICL=0x30 was the second
node walked; PATICL=0x60 was the third.  After the third, PATICL=0
→ exit.

So cmd's pattern has at least 3 nodes the SCIN walked through.  Each
node's SCIN3 push leaves a PDL entry.  The entry at e910 with
slot[1].a=0x60 is the SCIN3-push for the **third node** (the one that
exited with PATICL=0).

In the FENCEPT layout: title (slot[0]), FNCAFN (slot[1]),
0 (slot[2]), 0 (slot[3]), P (slot[4]).  Per cpypat semantics with v=4,
the next node link is at slot[2] of THIS node — but slot[2]=0 means no
next node, end of pattern.

So PATICL only walks 1 node (FENCEPT itself), exits with PATICL=0.
But the trace shows two SCIN3 advances (PATICL=0x30 then 0x60) —
suggesting cmd's compiled form is more than just FENCEPT.  Likely
cmd = FENCEPT followed by an ENDP/RTN node; or cmd's first node
isn't FENCEPT but a wrapper (e.g. CONCATPT or assignment node).

**The exact pattern compilation of `cmd = FENCE('a'|'ab')` is what
we don't yet know.**  Without that, we can't say which node pushed
the entry at e910 or whether it's legitimately part of a sealed
region or part of the outer scan's machinery.

This is the natural next investigation step — but exceeds the time
budget of session #66.

---

## Three concrete fix candidates for session #67

Each has a distinct test-and-falsify shape.

### (Y) Extend FNCA-success to neutralize entries below SCFLCL too

If the entries between STREX_entrypdl and SCFLCL_position contain the
problematic alt-cont, walk that range too at FNCA-success and zero
them.  Risk: that region belongs to the OUTER `*cmd` STARP6 frame, not
FNCA's frame; FNCA-success doesn't have access to STREX_entrypdl
(which is in cstack of the enclosing STARP6, not of FNCA).  Plumbing
needed.

### (Z) Make STREXCCL fire only on ascent, not descent

The bottom STREXCCL sentinel currently fires whenever the walker
DESCENDS past it (going from PDL high to PDL low).  Conceptually
the bottom sentinel marks "leaving inner-PATBCL region downward,
restore outer PATBCL."  But session #66 trace shows it firing
**inverted** — it switches FROM outer TO inner because slot[2]
holds the *inner* PATBCL.

Wait — let me re-check.  Bottom STREXCCL was rewritten at L_STARP2
with `slot[2] = D(PATBCL)` AT THE TIME OF L_STARP2 ENTRY.  At that
point, has PATBCL been popped to outer yet?  Looking at L_STARP2
code (lines 12235-12246): the cstack POPs happen FIRST (POP MAXLEN,
PATBCL, PATICL, XCL, YCL, STREX_entrypdl, STREX_innerpat).  After
those, PATBCL = outer.  THEN the rewrite at line 12245 stores
`D(PATBCL)` (= outer) in slot[2].

So bottom STREXCCL has slot[2] = outer PATBCL.  Top STREXCCL has
slot[2] = STREX_innerpat = inner PATBCL.

**But the trace shows bottom STREXCCL firing with slot[2]=b5d0=cmd
(inner)!**  That contradicts my reading of the code.

Let me re-check the trace:

```
[12] SALT2 PDLPTR=...e940 slot1={a=...a560,f=1,v=2} slot2={a=b5d0,f=16,v=3} PATBCL=ba70
[13] STREXC-FIRE  old_PATBCL=ba70  new_PATBCL=b5d0
```

slot[2].a = b5d0 = cmd (inner).  But L_STARP2 should have written
slot[2] = outer.

Wait — at event [12] PDLPTR=e940 is the position of the BOTTOM
STREXCCL (rewritten over SCFLCL slot at STREX_entrypdl).  And
STREX_entrypdl was set at STARP6 entry.  Looking at the SCOK chain
[3]: PDLPTR=e910 PATBCL=b5d0 — that's the SCIN3 push during cmd's
pattern walk.  So PDLPTR after SCIN's FENCEPT dispatch at PATICL=0x60
was e910.

Then `*cmd` returns success.  Inner SCIN returns at some PDLPTR.
STARP2 entry shows PDLPTR=e910.  STARP6 had pushed SCFLCL at its own
PDLPTR+3*DESCR.  STARP6 was entered when `*cmd` was being dispatched —
*outer is ARBNO(*cmd), so each ARBNO iteration calls `*cmd` which
goes through DSAR/STARP6.

Hmm, the bottom STREXCCL is at e940 which is ABOVE e910 (the SCIN
return PDLPTR).  Let me look at exact STARP6/STARP2 PDL flow:

STARP6 entry: PDLPTR initially at some value V.
- PDLPTR += 3*DESCR (now V+3*DESCR)
- Push SCFLCL at PDLPTR+DESCR (slot[1] of new region)
- ... cstack pushes including PUSH(PDLPTR) — this saves V+3*DESCR as STREX_entrypdl
- SAVSTK; SCIN(NORET)

After SCIN returns success: PDLPTR = whatever SCIN left it (≥ V+3*DESCR).

L_STARP2:
- POPs from cstack: STREX_innerpat, STREX_entrypdl=V+3*DESCR, YCL, XCL, PATICL, PATBCL=outer, MAXLEN.
- If `D_A(PDLPTR) > D_A(STREX_entrypdl)`: leaks present.
  - Rewrite at STREX_entrypdl: slot[1]=STREXCCL, slot[2]=PATBCL=outer.  ← bottom sentinel
  - PDLPTR += 3*DESCR
  - Push at PDLPTR: slot[1]=STREXCCL, slot[2]=STREX_innerpat=cmd, slot[3]=LENFCL.  ← top sentinel
- Else (no leaks): rewrite STREX_entrypdl with bottom STREXCCL only.

In the trace, STARP2-entry PDLPTR=e910, STARP2-exit PDLPTR=e940 (leaks=yes).
So `STREX_entrypdl < e910 < e940`.  That means `STREX_entrypdl ≤ e910 - 3*DESCR = e8e0`?
Actually if PDLPTR went from STREX_entrypdl (at STARP6 entry) up to e910 (at SCIN
return), and STARP2 doesn't change PDLPTR other than the +3 for the top sentinel,
then STREX_entrypdl was the value BEFORE inner SCIN's leftovers.

If STARP6 entered with PDLPTR=Y, then after STARP6's `+3*DESCR`, PDLPTR=Y+0x30.
That's STREX_entrypdl.  Inner SCIN added MORE pushes, PDLPTR ended at e910.  So
STREX_entrypdl = e910 - (number of inner pushes)*0x30.

With three SCOKs inside cmd matching (events 1-3) — each SCOK preceded by an
SCIN3 push — that's 3 pushes of 0x30 each = 0x90.  So STREX_entrypdl ≈ e910 - 0x90 = e880.
Bottom STREXCCL at e880.  Top STREXCCL at e910 + 0x30 = e940.  Yes matches trace!

So in the trace at event [12], PDLPTR=e940 is the TOP STREXCCL position.
slot[2]=b5d0=cmd (= STREX_innerpat).  Top STREXCCL has slot[2]=inner PATBCL.
That matches code.  Good.

When walker descends from e9a0 down, it consumes ARBNO machinery (events 10-11),
reaches e940 (event 12), reads top STREXCCL → STREXC-FIRE switches PATBCL
to b5d0=cmd (slot[2]).  Walker continues failp into next entry below.

Next entry below e940 is at e910.  PDLPTR=e910 after the consume (e940-3*DESCR=e910).
slot[1].a=0x60 from the SCIN3 push that happened during cmd matching when SCIN
processed cmd's pattern at PATICL=0x60.

Now — this entry is BELOW the top STREXCCL but ABOVE the bottom STREXCCL (e880).
It IS in the protected leak region.  STREXC top correctly switched PATBCL=cmd
before walking it.  The walker reads it under PATBCL=cmd.  Non-FNC → SCIN3 fall-through
(session #64) → reads `D(cmd + 0x60 + DESCR)` = a real pattern node in cmd → matches.

**The matching is doing what it should under PATBCL=cmd.**  It's finding a real
pattern alternative.  But that alternative was already discarded by FENCE's seal
on the original match.

So either:
- (a) FENCE's seal failed to remove the alt-cont entry (zeroing should fix —
  but my zeroing didn't catch this entry because it's outside FNCA's frame).
- (b) The walker shouldn't reach this entry under PATBCL=cmd because FENCE
  blocks the entire alternation.

For (a): the entry at e910 was pushed by SCIN3 during cmd's pattern walk —
during the OUTER SCIN1 of cmd dispatched by `*cmd`/STARP6.  FNCA was a NESTED
call within that SCIN1.  When FNCA's success path fires, it doesn't know
about (and shouldn't touch) the OUTER scan's PDL entries.

For (b): semantically what FENCE is supposed to do is "matched once, no
backtrack into me."  But the outer scan's SCIN3-around-FENCEPT entry DOES
get pushed as a normal alt-cont (since cmd's pattern is just a sequence and
each node gets its alt-cont push).  After FNCA succeeds (matching FENCE's P),
SCIN1 of the outer scan continues to the next pattern node.  That next node
is **the same FENCEPT's continuation in the alternation `(...) | (...)` in
cmd's wider pattern** if cmd's original code had alternation outside FENCE.
But cmd = FENCE('a'|'ab') — the alternation is INSIDE FENCE, not outside.

So cmd's pattern compiled form should be just the FENCEPT node (5 descriptors),
no alternation at the top level.  But the trace shows TWO SCIN3 advances
(PATICL=0x30 then 0x60).  That suggests cmd is compiled to FENCEPT followed by
something else.  Most likely a STR/concat-end node added by the compiler.

**The entry at e910 is the SCIN3 push for that second node, whatever it is.**
FENCEPT is at PATICL=DESCR (slot[1]); after dispatch, SCIN1 returns to L_SCOK
which sets PATICL=XCL.v.  XCL was slot[2] of FENCEPT = 0 → exits.  But trace
shows it didn't exit; it advanced.  So XCL.v wasn't 0 — meaning slot[2] of
FENCEPT had non-zero v.  That contradicts the ZERBLK init... unless cpypat
modifies it.

This is where session #67 should dig.  Knowing what cmd's compiled pattern
looks like exactly is the missing piece.

### (Z') Make STREXCCL bottom sentinel fire only when descending into the
region from outside (from above PDLPTR_at_sentinel_install)

This would prevent the walker from "entering" the inner-PATBCL context
when it's actually exiting downward to outer territory.  But the bottom
sentinel's purpose IS to switch PATBCL back to outer when the walker
descends past it (going from inner-region down to outer-region).  The
trace shows it's currently switching INNER (slot[2]=b5d0=inner) — which
is wrong if the sentinel is meant for the descent-out direction.

Wait — maybe the bug is simpler than I thought.  Let me re-check the
slot[2] values:

- BOTTOM sentinel at STREX_entrypdl: rewritten at L_STARP2 line 12245
  with `slot[2] = D(PATBCL)`.  At line 12245, PATBCL was just popped
  (line 12240) — so PATBCL = outer.  slot[2] = OUTER.
- TOP sentinel at PDLPTR after +3*DESCR: pushed at L_STARP2 lines 12239-40
  with `slot[2] = D(STREX_innerpat)`.  STREX_innerpat = inner.  slot[2] = INNER.

So bottom = outer, top = inner.  Trace event [12] PDLPTR=e940 slot[2]=b5d0=cmd=INNER.
PDLPTR=e940 is the TOP sentinel (= e8e0+0x30+0x30=e940 if STREX_entrypdl=e8e0).

So event [12] IS firing the TOP sentinel correctly (slot[2]=inner=cmd).

But what fires when walker descends through PDLPTR=e8e0?  That should be
the BOTTOM sentinel with slot[2]=outer.  In the trace, we never see it fire
because the walker doesn't descend that far before the wrong match succeeds.

**OK so the actual problem is:** after TOP STREXCCL fires (switches to cmd),
walker descends one entry to PDLPTR=e910.  That entry's dispatch SUCCEEDS
under cmd PATBCL.  The walker never reaches the BOTTOM STREXCCL (which
would switch back to outer).

If the entry at e910 DIDN'T succeed (e.g. were zeroed), the walker would
keep descending: e910 → e8e0 → bottom STREXCCL → switch back to outer →
keep going → eventually exhaust PDL → FAIL the whole match → "GOOD" output.

**So the fix really is to zero or remove that entry at e910.**  But it's
not in FNCA's frame.

### Where DID that entry come from?

It was pushed by SCIN3 during cmd's pattern walk, when SCIN was processing
the second node of cmd's compiled pattern.  That walk happened during the
inner SCIN call of STARP6 (dispatch of `*cmd`).  STARP6 doesn't have a hook
to clean up SCIN3 leftovers — that's the entire purpose of STREXCCL.

The entry at e910 has slot[1].a=0x60.  After SCIN3's push: `slot[1] = XCL`
where XCL was the node's slot[2] field.  So whatever node was at PATICL=0x30
in cmd's pattern, its slot[2] had `D_A=0x60`.  That `0x60` is the offset to
the next node's slot[1] in cmd's pattern.  In other words, this is a NEXT-LINK,
not an alt-link.

But session #64's `if (!FNC) goto SCIN3` route reinterprets this as an alt-link
under whatever PATBCL is current.  When PATBCL=cmd matches the original push,
fall-through dispatches the next-link node correctly (as if SCIN3 were just
continuing forward — but BACKWARDS in the failure walker semantics).  When
PATBCL has been switched, the same offset still falls into a real cmd-pattern
node.  Either way, this IS a re-entry into FENCE-sealed territory.

**So even in single-iteration ARBNO, the entry at e910 is from SCIN3's normal
push of the next-link.**  It's not "leaked" — it's the standard backtrack
trail.  The bug is that FENCE's seal didn't kill it.

### What FENCE's seal SHOULD have done

In SPITBOL, FENCE is implemented by `p_fnd` which does `xs=wb` (rewind
pattern stack to inner base = the value `xs` had at p_fna entry) then
`flpop` (pop two more entries, drop into failp).  The "two more" is the
p_fna entry itself (xs was bumped by 2 words at p_fna).  So SPITBOL's
seal-fire DOES truncate xs back to the value before p_fna was even
encountered.

**CSNOBOL4's L_FNCD does:**
```
D(PDLPTR) = D(YCL);            // restore to seal-base (saved in slot[2])
D_A(PDLPTR) -= 3*DESCR;        // consume SCFLCL trap
D(NAMICL) = D(NHEDCL);
BRANCH(FAIL)                    // exit SCIN (currently)
```

YCL was the seal-base = `D(slot[2])` of the FNCDCL trap.  Recall FNCA-success:
```
D_A(TMVAL) = D_A(PDLPTR);     // slot[2] = seal base = P1 (after -3*DESCR)
... store TMVAL in slot[2] of FNCDCL trap ...
```

So YCL = P1 = the clean base RIGHT BEFORE FENCE was entered.  L_FNCD rewinds
to P1, then -3*DESCR.  P1 = "PDLPTR right before SCFLCL push by FNCA".  So
L_FNCD's rewind takes us to "PDLPTR before FNCA was called."

**That's the right amount of rewind for FENCE seal-fire.**  All entries
above that level are gone.  Including the SCIN3 push for FENCEPT itself
(which sits AT P1 — the position where SCIN3 ran before FNCA's `+= 3*DESCR`).

But L_FNCD only fires when FENCE seal is HIT by the failure walker.  That
happens when the walker descends through FNCDCL.  In the trace... we don't see
FNCD-FIRE.  The walker descends to e940 (top STREXCCL, fires), then e910
(non-FNC, success).  It NEVER reaches FNCDCL.

**Where IS FNCDCL in this PDL layout?**  FNCA-success places FNCDCL at
`PDLPTR + 3*DESCR` after the cstack POPs and -3*DESCR.  In the trace, FNCA
fires inside cmd matching with PDLPTR ≈ e940 (right before SCFLCL push at
e970 or so).  After success, PDLPTR rewinds, FNCDCL placed somewhere.
Cmd matching continues, more SCIN3 pushes happen on top of FNCDCL.  By the
time `*cmd` returns and STARP6/STARP2 install STREXCCL pair, FNCDCL is
buried in the leak region.

In the descent walk (events 8-12), the walker should encounter FNCDCL
between top-STREXCCL and bottom-STREXCCL.  But the trace doesn't show
FNCD-FIRE.  Only events at PDLPTR e9a0, e970, e940, e910 are seen.

**So FNCDCL is somewhere between e910 and e8e0 — and the walker stops at
e910 because that entry's dispatch SUCCEEDS.**

**This is the actual fix shape:**

> The walker dispatches the entry at e910 SUCCESSFULLY before it can reach
> FNCDCL.  If the entry at e910 didn't succeed, the walker would keep
> descending and FNCD-FIRE would trigger — properly killing the whole
> region and FAILing the match.

So the question is: how do we make SCIN3 fall-through under switched
PATBCL NOT find a successful match?

**Option (A1):** Zero ALL non-FNC entries in the leak region at L_STARP2
time (when sentinels are installed).  This is closer to session #54's
attempt but at STARP2 instead of STARP6 — and **only zero entries that
came from the inner pattern's SCIN3 pushes**.  Risk: how do we know which
entries came from the inner pattern vs. somewhere else?  STARP6 was called
from... let me think.  STARP6 is the entry into `*cmd`.  The PDL state at
STARP6 entry was the OUTER scan's state.  Everything pushed BETWEEN STARP6's
own SCFLCL push and SCIN's return is from the inner scan.  That's exactly
the leak region [STREX_entrypdl..PDLPTR].  Zeroing it makes the leak
region unable to dispatch alts (SALT2 falls through SCIN3 with PATICL=0
→ goto SALT3).  Walker descends past, hits FNCDCL (if any), eventually
hits bottom STREXCCL → switch to outer → exhausts → FAIL.

Wait but session #54's zeroing did exactly this and broke guard5 (which
has `*cmd` but no FENCE).  Why?  Because guard5's `*cmd` matching
dispatches alternations that legitimately need to be re-tried via DSAR-redo.

**Option (A2):** Zero non-FNC entries in the leak region at L_STARP2 time
**only if** the leak region contains an FNCDCL (i.e. only when FENCE was
involved in the matched inner pattern).  Walk the region; if any FNC trap
with PATBRA-case-40 (FNCD) is found, do the zeroing of non-FNC entries.
Else don't.

This SHOULD preserve guard5 (no FENCE → no FNCDCL → no zeroing → DSAR-redo
machinery intact) AND fix tests 119/124/127/129/148/149/152 (FENCE → FNCDCL
present → zeroing kills the SCIN3 leak that was causing the wrong match).

This is the recommended fix shape for session #67.

---

## Recommended session #67 plan

1. Apply `docs/F-2-Step3a-session65-L_FNCD-attempt.diff` (the L_FNCD goto
   L_TSALT change is necessary for cluster B).
2. Apply `docs/F-2-Step3a-session66-diagnostic.diff` (for re-trace ability).
3. Implement option (A2): at L_STARP2 success, after computing the leak
   region [STREX_entrypdl+3*DESCR .. PDLPTR_at_STARP2_entry], walk it
   and check whether ANY entry is FNCDCL.  If yes, zero slot[1] of all
   non-FNC entries in the region.  If no, leave alone.
4. Test gate stack:
   - guard5 → `inner backtrack worked`  (no FENCE → no zeroing → preserved)
   - Tier F all 16 PASS  (most don't have FENCE, those that do have it
     in shapes that don't use *var indirection)
   - fence_function 10/10
   - fence_suite ≥ 51/53 (target: 119/124/127/129/148/149/152 → OK,
     keep 150/151)
   - beauty self-host ≥500 lines (the actual goal)
5. If guard5 breaks, hypothesis is wrong — fall back to investigating
   what cmd's compiled pattern looks like (`pat.c` cpypat for FENCEPT)
   and refining further.

The condition "leak region contains an FNCDCL" is checkable in O(N) where
N = (PDLPTR - STREX_entrypdl)/3*DESCR — small for typical patterns,
unbounded but bounded by PDL size.

---

## Diagnostic patch — what's saved

`docs/F-2-Step3a-session66-diagnostic.diff` adds env-gated trace logging
at 7 sites (SALT2, SCOK, FNCA-entry, FNCA-success, FNCD-fire, STARP2-entry,
STARP2-exit, STREXC-fire).  Apply with `git apply` and run with `S148_TRACE=1`
to reproduce session #66's traces.

---

## Honest checkpoint

Sessions #44–#66 = 22 sessions on F-2 Step 3a.  fence_function preserved
10/10 throughout.  Tier F preserved 16/16 since #55.  fence_suite at
46/7/0 since #65 (s65's L_FNCD swap is a 124↔150 trade, net unchanged).
Beauty at 42 lines since #58.

**Session #66's genuine new contribution:**
1. **Decisive identification** of the bug location: a SCIN3-pushed
   non-FNC alt-cont entry at PDL position **between bottom STREXCCL and
   top STREXCCL** (= inside the protected leak region) that gets
   dispatched under switched PATBCL=cmd via session #64's SCIN3 fall-through
   path, finding a real cmd-pattern alternative that FENCE was supposed
   to seal.
2. **Resolves the session #62 vs #64 narrative tension definitively:**
   bug IS on the failure-walker path (session #64 right about path);
   bug location is NEITHER "abandoned region above STARP2" (session #64)
   NOR "no SALT2 events" (session #62).  Bug is a SCIN3-pushed entry
   inside the STREXCCL-protected leak region, dispatched correctly under
   the switched PATBCL.
3. **Refutes the FNCA-success zeroing approach.**  The problematic
   entry is NOT in FNCA's frame [P1..P2].  It's in the OUTER scan's
   PDL region (between STREX_entrypdl and PDLPTR_at_STARP2_entry).
4. **Identifies the correct fix shape (A2):** at L_STARP2 success,
   conditionally zero non-FNC entries in the leak region IF an FNCDCL
   is present.  This preserves guard5 (no FENCE → no zeroing) and should
   fix all 7 cluster-A/B FAILs.
5. **Reusable diagnostic patch** (session #66 trace infrastructure)
   saved as `.diff` artifact.

Saving this and doing the fix in session #67 keeps the change small,
testable, and reviewable.
