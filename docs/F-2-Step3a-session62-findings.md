# F-2 Step 3a Session #62 — PDL dump captures bug locus precisely

## State at session start
```
HEAD: e723517
fence_suite: 44 OK, 4 FAIL, 0 CRASH (of 48)
fence_function: 10/10
beauty self-host: 42 lines
```

State at session end: identical.  No production code changes.  Working tree
clean.  Diagnostic instrumentation reverted per RULES.md.

## What this session did

Per the session #60 final-handoff plan, instrumented `isnobol4.c` with an
env-gated PDL-dump diagnostic.  The diagnostic walks the PDL from PDLPTR
downward in 3*DESCR strides at three checkpoints — FNCA-success-after-seal,
STARP2-after-sentinels, and every SALT2 entry.  Annotates each slot with
its semantic identity (SCFLCL / FNCDCL / STREXCCL / FNC-disp / alt-cont / null).

Run on test 119 (`s='aab', cmd=FENCE('a'|'ab'), outer=ARBNO(*cmd)`,
`*outer RPOS(0)`).  The diagnostic was reverted before this commit.

## Genuine new contribution: the bug class is reproducible at 5 lines

The bug was thought to require `s='aab'` and the full *outer/ARBNO/*cmd/FENCE
chain.  Empirical bisection this session:

- `s='ab'` reproduces the bug (was thought to need 'aab')
- `*cmd RPOS(0)` ALONE (no ARBNO, no *outer) — works correctly (sealed)
- `ARBNO(FENCE(...))` with inline FENCE — works correctly
- `ARBNO(*cmd)` inline (no *outer) — works correctly
- `outer RPOS(0)` (where outer = ARBNO(*cmd)) — works correctly
- **`*outer RPOS(0)` — FAILS** with "unexpected match"

**The bug requires the conjunction of:**
1. `*var` outer indirection (DSAR dispatch path), AND
2. ARBNO inside that var, AND
3. `*var` inside the ARBNO body (second DSAR dispatch), AND
4. FENCE dispatched from that inner var.

This is a sharper boundary than session #55's Tier F analysis suggested.
Tier F's 16/16 PASS already showed the bug is narrow; this confirms it
needs *exactly* this nesting depth — not just any `*var + ARBNO + FENCE`.

## PDL layout at FNCA-success (test 119, s='ab'; addresses truncated)

```
addr  high
  [-0] PDL=...aa20  s1=FNCDCL    seal-base=aa20  ← FNCA's seal we just wrote
  [-1] PDL=...a9f0  s1=null
  [-2] PDL=...a9c0  s1=SCFLCL    ← STARP6 #2's (`*cmd`) SCFLCL
  [-3] PDL=...a990  s1=null      ← STARP6 #2's pre-state
  [-4] PDL=...a960  s1=0x90      ← THE leaked alt-cont (not from FENCE alts!)
  [-5] PDL=...a930  s1=null
  [-6] PDL=...a900  s1=STREXCCL  s2=bb50  ← outer-DSARP2's bottom sentinel
addr  low
```

**Critical correction to prior session diagnoses.** The leaked alt-cont at
`a960` with `slot[1]=0x90` is NOT pushed by the inner FENCE's `'a'|'ab'`
matching as previous sessions assumed.  It was pushed by the OUTER DSARP2
of `*outer` when SCIN3 dispatched `*cmd` through ARBNO machinery — its
slot[1]=0x90 is an offset INTO outer body (b6b0), not into cmd or
FENCE-inner P.

The 'ab' alt that FENCE seals away is at frame `aa50`+ (above `aa20`) —
correctly truncated by FNCA's `POP(PDLPTR); -=3*DESCR; +=3*DESCR` rewind.
That part of the seal works.

## Trace evidence (key sequence pre-FNCA)

```
[STARP6 #1] PDLPTR=18d0 PATBCL=bb50 YPTR=b6b0    (`*outer` STAR setup)
SALT2 events 19f0(null) → 19c0(0xb0)             (walker descending top-level alts)
[DSARP2 #1] PDLPTR=19c0 PATBCL=bb50 YPTR=b6b0    (`*outer` DSAR redo)
SALT2 19f0(SCFLCL) → 19c0(null) → 1990(STREXCCL@b6b0) → 1960(0x60)
[STARP6 #2] PDLPTR=1990 PATBCL=b6b0 YPTR=b590    (`*cmd` STAR setup, dispatched
                                                  via 0x60 alt-cont under PATBCL=b6b0)
[FNCA #1]   PDLPTR=19f0 PATBCL=b590 PATICL=0x30  (FENCE dispatched in cmd body)
```

So the leaked alt-cont 0x90 at `a960` was **not yet on PDL when STARP6 #2
ran** — it was pushed BEFORE all of this, somewhere in DSARP2 #1's setup
or earlier walker iterations.  This is the "alt-cont at slot[-4] in DUMP #2"
that PRE-EXISTED the cmd matching.

(More precisely: DSARP2 #1 was dispatched from walker by reading slot at
`a9c0`+DESCR=0xb0, an alt-link from the top-level pattern.  SCIN3 read
three slots from PATBCL+0xb0 and pushed the alt-cont, dispatching DSAR.
The 0x60 and 0x90 alt-cont entries were already on PDL from earlier
walker activity — likely outer SCNR setup or pattern compilation
machinery.)

## Why FNCA's seal does NOT prevent the bug

FNCA's seal at `aa20` correctly blocks backtracking into the inner-FENCE
'a'/'ab' alts (above `aa20`).  When the failure walker descends from above
the seal, it dispatches FNCDCL → rewinds PDLPTR to seal-base=`aa20`,
flpops to `a9f0`, BRANCH(FAIL).  At that point the walker is at PDLPTR=`a9f0`
which is still ABOVE the leaked alt-cont at `a960`.

But the walker does NOT EXIT THE OUTER SCAN at this point.  BRANCH(FAIL)
returns from the current SCIN1 with case 1 (failure).  The caller — DSARP2
#1's switch — does `case 1: goto L_STARP5`, which is the failure path of
the OUTER `*outer`.  STARP5 pops cstack, restores outer state, and goes
to L_STARP3 → if LENFCL!=0 goto TSALT (continue walker at OUTER level).

After STARP5's POPs, PDLPTR is at... let's see, DSARP2 #1 saved its own
PDLPTR before SCIN call; STARP5 pops that.  So PDLPTR returns to DSARP2's
saved value = `a9c0` (DSARP2's frame).

**Now the walker at PDLPTR=a9c0 reads its own SCFLCL trap (which DSARP2 #1
pushed) and dispatches BRANCH(FAIL) too.  This propagates failure up another
level — to STARP6 #1's switch.**

But before that — wait, at PDLPTR=a9c0, slot[1]=SCFLCL.  Walker reads it,
dispatches case 27 → BRANCH(FAIL).  STARP6 #1's switch does
`case 1: goto L_STARP5`.  STARP6 #1's STARP5 pops cstack.  PDLPTR returns
to STARP6 #1's saved value.

Eventually the chain bottoms out and *outer fails.  Then OUTER pattern (top-
level) backtracks via its SALT2 walker.

**But the 0x90 alt-cont at `a960` is REACHED somewhere in this descent and
gets dispatched under the wrong PATBCL** (whatever PATBCL is current at
that point — could be top-level `bb50`, or after STREXCCL routing,
something else).  SCIN3 reads `PATBCL+0x90+DESCR` — under top-level PATBCL,
that lands somewhere unexpected, dispatches a node, and the SCAN succeeds
spuriously.

## Hypothesis confirmed by trace silence

**No SALT2 events appear between DUMP #3 (STARP2-after-sentinels for `*cmd`)
and the "unexpected match" output.**  That means the wrong-match path does
not go through the failure walker (no SALT2 iterations).  The match
SUCCEEDS DIRECTLY from FNCA → SCOK propagates up → eventually the outer
pattern returns success.

This means: with FNCA-success matching 'a' at cursor 0, *cmd's STARP2
returns success, ARBNO continues, *cmd is dispatched again at cursor 1...
no wait, but that would show more events.

Possibility: **iter#1 alone matched the entire string somehow**.  With
s='ab' and FNCA matching only 'a' (1 char), cursor would be 1, not 2.
RPOS(0) would fail.  But maybe the match doesn't actually go through
RPOS(0) on the success path — maybe ARBNO terminates immediately and the
top-level pattern never re-checks RPOS(0)?

Or maybe: ARBNO's compiled body has a redo loop that, after iter#1
success, falls through to the END node WITHOUT requiring more iterations,
and the OUTER pattern's machinery sees cursor=1 with success.  Then
*outer's SCAN reports success at length 1.  The outer pattern's RPOS(0)
fires next.

But RPOS(0) on s='ab' at cursor=1 — wants cursor=length=2.  Should fail.
Unless RPOS(0) is being checked at cursor 2 somehow...

**Conclusion:** the trace gap between DUMP #3 and "unexpected match" hides
something we need to see.  The next session needs to instrument SCOK,
SCNR exit paths, RPOS dispatch, and possibly LSCNR3 to see the actual
match-completion path.

## Specifically what session #63 should do

1. **Apply session #56 patch** OR work from current HEAD `e723517` (s58
   STREXCCL state already committed).
2. **Add trace at**:
   - L_SCOK / L_SCIN2 entry — to see when "continue scan" vs RTN2 happens
   - L_SCNR4-6 success-finish path — to see if scan ended normally
   - SCNR's main success path
   - Every L_PATBRA case dispatch (in SALT2's walker dispatcher, log the
     case number)
3. **Possibly also instrument cstack PUSH/POP** to confirm it's truly LIFO
   (one observed weirdness this session: the cstack-saved PDLPTR appears
   to differ from naive expectation by 2*DESCR — see "Cstack-saved PDLPTR
   appears off-by-2-frames" below).
4. **Run the simpler `s='ab'` repro** (5 lines, faster).
5. **Find where `'ab'` actually matches** — the wrong match has to come
   from `'ab'` matching somewhere.  Add a one-shot trap at every place
   the matcher commits a match of 2+ chars, log the cursor and pattern.
6. **Fix candidates** depend on where the bad match originates:
   - If from a leaked SCIN3 alt-cont reachable via SALT2: clear it at
     STARP2 of *cmd or at FNCA-success.
   - If from ARBNO redo machinery: fix the redo path's PATBCL routing.
   - If from a missing SCFLCL exit: ensure STARP6 #1's SCFLCL is
     properly placed.

## Cstack-saved PDLPTR appears off-by-2-frames (anomaly to verify)

Trace evidence with FNCA's pre-PUSH and post-POP instrumentation:
```
[STARP6 #2 entry] PDLPTR=1990
[FNCA #1 entry] PDLPTR=19f0 (= 1990 + 0x60 = 1990 + 2*3*DESCR)
[FNCA pre-PUSH-PDLPTR] PDLPTR=1a20 (= 19f0 + 0x30 — FNCA's own +=3*DESCR)
[FNCA post-POP-PDLPTR] PDLPTR=1a20  ← restored value matches push value ✓
```

This actually checks out — the apparent "off by 2" was my analysis error.
SCFLCL at `a9c0` (slot[-2] in DUMP #2) was pushed by STARP6 #2 (`*cmd`)
and is correctly preserved.  SCIN3 inside FNCA's inner SCAN pushed an
alt-cont at frame `a9f0` (slot[-1]), but its slot[1]=alt-link-of-FENCE-
node which is 0 (FENCE has no alt within cmd's body).  That's why slot[-1]
shows `s1=null` — the alt-link IS 0 because cmd has only one node.

So the PDL state is exactly as expected.  The bug is NOT in cstack
discipline.

## Summary

- 16-session FENCE diagnosis pattern (44-60+) continues — each session
  adds one piece of evidence.
- Session #62: empirical bug-class boundary sharpened (5-line `*outer →
  ARBNO → *cmd → FENCE` repro), full PDL layout dumped at FNCA-success
  and STARP2-after-sentinel checkpoints.
- **The fix is NOT in FNCA, NOT in STARP2's sentinels, NOT in seal arithmetic.**
  All those subsystems work correctly per the dump.  The bug is somewhere
  AFTER DUMP #3 — in the success-propagation path through ARBNO/*outer/
  top-level.
- Session #63 should instrument the SCOK / SCNR success path and the
  PATBRA dispatch case-by-case, with the simpler `s='ab'` test.

## Honest checkpoint

Sessions #44–#62 = 18 sessions.  fence_function preserved 10/10.  Tier F
preserved 16/16 since #55.  CRASH→FAIL completed at #58.  Beauty stuck at
42 lines, unchanged.  fence_suite stuck at 44/4/0 since #58.

Session #62 is exclusively diagnostic — no fix attempted, no code change
committed, no gate movement.  This is the right shape for an "investigation
session" per the F-1 risk note: *"investigation rungs sometimes fail to
converge on a clear answer.  If F-1 produces ambiguous evidence after one
full session, the right move is to commit the partial findings as a notes
file and re-plan, not to start writing code on a hunch."*

The strategic question from session #60's final handoff — "is closing this
bug worth more sessions, or should the goal pivot?" — remains open.  The
bug is now at "wrong-answer in a 5-line repro requiring 4-level
indirection."  Each session shrinks the territory.  But the rate of
progress (16 sessions for ~7 beauty lines, no fence_suite gain since #58)
suggests the effort-to-value ratio deserves Lon's input.
