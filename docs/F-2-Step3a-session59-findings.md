# F-2 Step 3a — session #59 findings (continuation; STREXBCL variants tested, dispatch-table architecture issue identified)

GOAL-CSN-FENCE-FIX F-2 Step 3a continuation (session #59, 2026-04-30).

## TL;DR

Session #59 continued from session #58's paired-sentinel design (which
eliminated all fence_suite CRASHes 6→0 but left tests 119/129 as
wrong-answer FAILs).  Tested two variants of a distinct bottom-sentinel
descriptor (STREXBCL, dispatch case 42):

**Variant A — STREXBCL with FAIL-on-fire (terminate inner SCAN call):**
- fence_function: 10/10 ✅ preserved
- fence_suite: **43 OK / 5 FAIL / 0 CRASH** — REGRESSED by 1 vs. s58 (44/4/0)
- test 119: still wrong-answer
- Conclusion: terminating via FAIL is too aggressive — it breaks an
  alt-backtrack case that s58 handled correctly.

**Variant B — STREXBCL with PATBCL=outer + continue walker** (same
behavior as s58 STREXCCL_bottom, just renamed):
- Identical to s58 results (44/4/0, test 119 wrong-answer).
- Confirms the *handler* isn't the discriminator; the issue is which
  entries the walker reaches AFTER the sentinel fires.

## The minimal trace evidence (s58 + S58_TRACE=1 logging at SALT2 entry)

For test 119 (`*outer where outer=ARBNO(*cmd) where cmd=FENCE('a'|'ab')`,
subject `'aab'`, anchored), the failure-walker trace contains exactly 7
events before the spurious "match":

```
[1] SALT2 read: PDLPTR=9f0 PATBCL=bb60 slot[1]={a=NULL,f=0,v=0}   → SALT3 loop
[2] SALT2 read: PDLPTR=9c0 PATBCL=bb60 slot[1]={a=0xb0,f=0,v=240} → SCIN3 fallthrough
[3] SALT2 read: PDLPTR=9f0 PATBCL=b6c0 slot[1]={a=heap,f=1,v=2}   → FNC dispatch
[4] SALT2 read: PDLPTR=9c0 PATBCL=bb60 slot[1]={a=NULL,f=0,v=128}
[5] SALT2 read: PDLPTR=990 PATBCL=bb60 slot[1]={a=heap,f=1,v=2}   → FNC dispatch
[6] STREXC fire: PATBCL bb60->b6c0 PDLPTR=960
[7] SALT2 read: PDLPTR=960 PATBCL=b6c0 slot[1]={a=0x60,f=0,v=0}
unexpected match (alt was tried)
```

Where `bb60`=outer-most pattern, `b6c0`=outer (ARBNO body), `b5a0`=cmd
(FENCE alts).

### Decisive observation

Event [2] is the smoking gun: at PDLPTR=9c0 under PATBCL=bb60, slot[1]
is `{a=0xb0, f=0}` — non-FNC, offset 0xb0 (= 11*DESCR).  L_SALT2 routes
to L_SCIN3 fallthrough.  L_SCIN3 increments PATICL to 0xc0 and loads
`ZCL = D(bb60 + 0xc0)` — the function descriptor at offset 12*DESCR
into outer-most's pattern.

Outer-most's pattern is `s POS(0) *outer RPOS(0)`.  *outer compiles via
STARPT (`v311.sil:12219`) which is **11*DESCR** long:

```
STARPT  TTL+MARK  11*DESCR    ← title
 +1*D   STARFN    FNC,3       ← initial *X dispatch
 +2*D   then-or   to slot[7]  ← skip-link  
 +3*D   1
 +4*D   0
 +5*D   SCOKFN    FNC,2       ← success exit
 +6*D   then-or   7*DESCR     ← back-link
 +7*D   0
 +8*D   DSARFN    FNC,3       ← redo dispatch  ← 8*DESCR offset
 +9*D   then-or   4*DESCR
 +10*D  0
 +11*D  end
```

A then-or pointing to offset 0xb0 (11*DESCR) lands at the END of the
*outer STARPT block — likely the start of RPOS(0) or whatever follows.
**The dispatch via SCIN3 reads ZCL = D(bb60 + 0xc0) which is past the
end of *outer and into RPOS(0)'s function descriptor.**

This dispatch ultimately leads to events [3]-[7] which involve another
*outer STARP6 (silent, but inferred from PATBCL=b6c0 in [3]), inner
*cmd matching with FENCE, and ultimate "success" — but it's a SECOND
*outer evaluation that succeeds with a different cursor position.

### Why this is wrong

SPITBOL on the same input fails the match.  The architectural difference:

- **SPITBOL's pcode addressing:** alt links are direct memory pointers.
  Each pcode node stands alone; dispatching one via failp doesn't depend
  on a containing pattern context.
- **CSNOBOL4's PATBCL+offset addressing:** alt links are offsets within
  the current pattern.  The same offset under a different PATBCL reads
  a completely different node.

When a leaked SCIN3 entry from inner-cmd matching is read under outer
PATBCL via the failure walker, the offset stored in slot[1] points to a
**different node** than what the inner-cmd matching intended.  Sometimes
that different node is ALSO valid pattern syntax (a STAR or DSAR) and
gets dispatched without crashing — leading to spurious successful
re-matches like test 119.

This is fundamentally architectural.  Session #58's paired-sentinel fix
addresses the PATBCL-context part (top STREXCCL → PATBCL=inner; bottom
STREXBCL → PATBCL=outer), but it does NOT prevent the walker from
descending into outer's leaked SCIN3 entries that have the WRONG offset
semantics.

## Two structurally-clean fix candidates

### (a) PDLHED-bound SALT2 walker

Maintain PDLHED as a per-iteration base (analogous to SPITBOL's pmhbs).
At STARP6/DSARP2 entry, save PDLHED to cstack and `MOVD PDLHED, PDLPTR`
(matching what EXPVAL/BAL/ATP already do).  Modify SALT2 to bound:
when `PDLPTR == PDLHED`, do NOT loop back to SALT1; instead invoke the
level's "I'm at base" handler (which does the PATBCL/PDLHED restore and
continues popping in outer scope, like SPITBOL p_exb).

Pros: Architecturally clean.  Mirrors BAL's existing `PCOMP PDLPTR,
PDLHED, TSALF, TSALF, INTR13` at v311.sil:3975 — which BAL already
does correctly.

Cons: Touches the failure-walker's hot loop.  Affects every SALT2 read
in every pattern match.  Risk of regression in non-STAR cases.  Needs
PDLHED save/restore plumbing through STARP6/STARP2/STARP5/DSARP2/STARP3.

### (b) Truncate-on-success (Gimpel deferred-alts)

At STARP2 success (post-SCIN-success), don't leave inner SCIN3 entries
on PDL.  Walk the inner region, copy each non-FNC then-or alt to a
side array on the inner pattern descriptor, then truncate PDLPTR to
saved entry-PDLPTR-3*DESCR.  At DSAR-redo, push the deferred alts back
onto PDL one at a time.

Pros: Removes the cross-PATBCL leak entirely.  No SALT2 hot-loop change.

Cons: Changes pattern descriptor shape (adds deferred-alts array).  Breaks
pattern-equality tests.  Heavier per-success cost.  May not preserve
SPITBOL's exact backtrack semantics for guard5-like cases.

## Recommended session #60 plan

1. **Keep s58 paired-sentinel patch as the working baseline** (44/4/0 in
   fence_suite).  It eliminates all CRASHes.  Save as
   `docs/F-2-Step3a-session58-paired-strexc-attempt.diff`.

2. **Implement candidate (a)** — PDLHED-bound walker.  Touchpoints:
   - L_STARP6 entry: PUSH(PDLHED); MOVD PDLHED, PDLPTR (after the
     SCFLCL frame is pushed).  Establish new level.
   - L_DSARP2: same.
   - L_STARP2 success / L_STARP5 fail: POP(PDLHED) before any other
     state restore.
   - L_SALT2: add `if (D_A(PDLPTR) <= D_A(PDLHED)) goto L_PDLHED_BASE;`
     before the slot-1 read.  L_PDLHED_BASE handler restores outer
     PDLHED and continues failp.
   - Coordinate with FNCA which already does PDLHED save/restore.

3. **Test on test 119** (the canonical wrong-answer repro).  If the
   walker stops at PDLHED instead of descending into outer's leaked
   alts, test 119 should fail-match cleanly (matching SPITBOL).

4. **Tier F + fence_function + guard5 must remain green.**  fence_suite
   target: ≥ 46/2/0 (fixing 119/129 while preserving 124/127 as
   pre-existing baseline FAILs).

5. **Beauty target: ≥ 500 lines.**

## Files this session

- `csnobol4/docs/F-2-Step3a-session59-findings.md` (this file) —
  trace evidence, dispatch-table architectural diagnosis, fix
  candidates (a) and (b).
- No runtime source changes committed.  Working tree clean at HEAD
  `68075bb` (= session #58 commit, which already records the s58 patch
  in docs/ as the working baseline).

## Honest circularity check

Sessions #44–#59 = 16 sessions on F-2 Step 3a.  fence_function preserved
10/10.  Tier F preserved 16/16 since session #55.  Beauty: 35→42 lines
(s58 gain).

Session #59's genuinely-new contribution: **identification of the
PATBCL-relative-offset architectural issue.**  Sessions #50/#54/#56/#57
focused on PATBCL state at sentinel-fire time.  Session #58 added
paired sentinels.  But the trace shows the walker reaches a non-FNC
SCIN3 entry whose offset is interpreted under the wrong PATBCL — leading
to spurious dispatch even when PATBCL routing is correct.  The
architectural fix is to prevent the walker from reaching those entries
at all (candidate a) or to remove them at success time (candidate b).

The session #59 trace (only 7 SALT2 events before crash/wrong-answer)
is much shorter than session #57's 69-event trace — because s58's
paired sentinels eliminate most of the walker's wandering and isolate
the remaining bug to a single dispatch event.  This represents a
significant narrowing of the diagnosis surface.
