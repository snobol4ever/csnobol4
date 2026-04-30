# F-2 Step 3a — session #54 findings (targeted slot-zeroing rejected; diagnosis sharpened)

GOAL-CSN-FENCE-FIX F-2 Step 3a continuation (session #54, 2026-04-30).

## TL;DR

Session #54 implemented the "targeted slot-zeroing" fix proposed by session
#53's findings: at L_STARP2 / L_DSARP2 success, walk PDL from the
saved-pre-SCIN snapshot to current PDLPTR and zero slot[1] of any
non-FNC-flagged trap. Theory: FNCDCL seals (FNC=1) survive, leaked then-or
alternation traps (FNC=0) become clean fall-throughs.

**Result:** fence_suite improves to 29 OK / 3 FAIL / 0 CRASH (best ever; 2
crashes 119/129 fixed). fence_function 10/10 preserved. **But beauty
regresses from ~32 lines to ~10 lines** because the zeroing destroys
**legitimate inner-pattern alternation backtrack traps**.

Per RULES.md (regression in error class), the fix is NOT committed. Saved
as `docs/F-2-Step3a-session54-zeroing-attempt.diff` so future sessions
don't re-attempt the same approach.

The session's genuine new contribution: **a sharpened diagnosis of why
the leak class crashes** (with full SCIN3-push / SALT2 / PATBCL-POP trace
of test 119) AND **proof that targeted zeroing is wrong** (5-line repro
showing inner-backtrack breakage).

## Why targeted zeroing is wrong

Session #53's hypothesis: "non-FNC trap entries leaked from inner SCIN
have offsets relative to inner PATBCL and crash when dispatched under
outer PATBCL — zero them so SALT2 takes the SALT3 (clean fail-through)
path."

**The hypothesis missed the inner-backtrack mechanism.**

5-line repro that breaks under zeroing but works on SPITBOL and on
csnobol4 baseline (s52 patch only):

```snobol4
        cmd = (LEN(1) | LEN(2))
        outer = (*cmd 'X')
        s = 'ABX'
        s POS(0) outer RPOS(0)                                :S(YES)F(NO)
YES     OUTPUT = 'inner backtrack worked'                     :(END)
NO      OUTPUT = 'fail'
END
```

Expected: `inner backtrack worked` (cmd matches LEN(1) initially, outer
'X' fails on 'B', failure walker re-enters DSAR with UNSCCL=1, re-evaluates
cmd, this time matching LEN(2), then 'X' matches at position 2). All three
runtimes' behavior:

| Runtime                              | Output                    |
|--------------------------------------|---------------------------|
| SPITBOL x64                          | `inner backtrack worked`  |
| csnobol4 + session-#52 patch only    | `inner backtrack worked`  |
| csnobol4 + session-#54 zeroing       | `fail` ❌ regression       |

### How outer-pattern walker correctly reaches inner backtrack

Traced via instrumented SALT2 / SCIN1-entry / L_UNSC / L_DSAR. The
mechanism uses CSNOBOL4's existing UNSCCL-restart machinery:

1. Outer SCIN1 with PATBCL=outer matches up through `*cmd 'X'`.
2. `*cmd` is dispatched as DSAR (case 14). DSARP2 PUSHes outer state
   (MAXLEN/PATBCL/PATICL/XCL/YCL) to cstack, sets UNSCCL=1, calls SCIN1.
3. Inner SCIN1 sees UNSCCL=1 → L_UNSC: `D(PATBCL) = D(YPTR)` — sets
   PATBCL=cmd. Inner SCIN1 matches cmd's first alternative LEN(1).
   During matching, cmd's pattern alts push then-or traps with slot[1]
   = offset-into-cmd-pattern (FNC=0).
4. Inner success → SCIN1 returns 2 → STARP2 → POP cstack restores
   outer PATBCL. Outer continues; outer 'X' fails on 'B'.
5. SALT2 walks back. First it reaches outer's own traps (slot[1]
   offset-into-outer-pattern, dispatches under PATBCL=outer correctly).
6. Among outer's traps is the one DSAR pushed signaling "redo *cmd".
   This trap dispatches via `case 14: goto L_DSAR` (FNC-flagged
   function descriptor — DSAR's redo entry).
7. L_DSAR re-enters with PATBCL=outer, sets UNSCCL=1, calls SCIN1.
8. Inner SCIN1 sees UNSCCL=1 → L_UNSC: PATBCL=cmd AGAIN. Now walker
   continues PAST the L_UNSC into L_SALT3 → L_SALT1 → L_SALT2 — but
   PDLPTR is now at the position WHERE the inner alt traps were left.
9. Walker dispatches the inner alt trap (slot[1]=offset-into-cmd-
   pattern) under PATBCL=cmd. This finds the alternate (LEN(2))
   correctly. cmd matches with LEN(2) instead.
10. Outer continues, 'X' matches.

**Step 9 is what zeroing destroys.** The leaked inner trap that
session #53 thought was "stale and crashing" is actually the
legitimate inner-alternation continuation. When PATBCL has been
correctly restored to inner via the DSAR redo path, the trap
dispatches correctly.

### Where session #53's hypothesis came from

Test 119's actual crash signature: SALT2 reads slot1=0x200 with
PATBCL=outer. The 0x200 IS valid as a cmd-pattern offset, NOT as
an outer-pattern offset. Session #53 correctly identified the trap
as a leak. Session #53 incorrectly assumed all such leaks were stale.
But many such leaks are LIVE — they're consulted via the UNSC path.

The 119/129 crash is a leak that DOESN'T get consulted via UNSC —
it's reached after PATBCL was restored to outer and there's no DSAR
redo trap above it to re-route walker through UNSC. THAT specific
leak is stale and crashes. Distinguishing live-leak from stale-leak
is what targeted zeroing fails to do.

## The actual problem class (sharpened)

Test 119 crash trace summary (from instrumented session #54 run):

```
ARBNO iteration 1: outer SCIN with PATBCL=outer pushes traps at PDL=ab0..b10
  with cmd-PATBCL-relative offsets (slot1=0x200, 0x90, 0).  These are
  pushed under PATBCL=cmd via SCIN3 during cmd's matching, but their
  PHYSICAL location on PDL is in the outer's region (above where outer
  traps were pushed before *cmd dispatched).
FENCE evaluation pushes SCFLCL+inner traps at PDL=b70..bd0.
FNCA success: pops cstack, rewinds PDLPTR back to FENCE-entry, pushes
  fresh FNCDCL seal.
DSARP STARP2 success: pops cstack restoring outer PATBCL.  PDL is NOT
  rewound; cmd-level traps at PDL=ab0..b10 remain, and the fresh
  FNCDCL seal remains.
ARBNO iterates more.
Eventually outer pattern fails.  Walker walks back through PDL.
At PDL=b70 walker hits FNCDCL → L_FNCD → rewinds PDLPTR to seal-slot[2]
  = FENCE-entry-PDLPTR (= just below cmd-level pushes from FENCE
  evaluation).
Walker continues from new PDLPTR, dispatching cmd-level traps from
  FENCE evaluation via FNC dispatch (function pointers, FNC=1 — these
  are the legitimate FENCE machinery cleanup traps).  POP-PATBCL fires
  via FNCBX or STARP5 paths — restoring cmd PATBCL or outer PATBCL.
After all those legitimate dispatches, walker reaches PDL=ab0 with
  PATBCL=outer. Reads slot1=0x200. Dispatches via SCIN3 with PATBCL=
  outer → reads pattern at outer+0x200 → wild read → SEGV.
```

**Two key facts:**

1. The crashing slot WAS pushed by SCIN3 under cmd-PATBCL during cmd's
   matching, then never overwritten because PDL grew above it but never
   rewound below it during the entire flow.

2. The walker reaches it under outer-PATBCL because there is NO DSAR
   redo trap between the current walker position and this leaked slot.
   DSAR redo traps live in OUTER's PDL region (pushed by the outer
   SCIN3 dispatching DSAR for `*cmd`). Once the walker has consumed
   all outer's DSAR redo traps, any further leaks below them get
   dispatched under outer PATBCL.

## What the right fix shape looks like

The architectural answer is the SPITBOL `=ndexc` sentinel approach,
mirrored from `p_nth` (sbl.min:12213).

When STARP6/DSARP success path runs, AND the inner SCIN pushed entries
on PDL (i.e. PDLPTR > saved_snapshot), install a **NEW sentinel trap**
that, when dispatched, restores PATBCL to inner and continues failure
walk. The leaked inner traps remain BELOW the sentinel; walker reaches
sentinel FIRST and switches PATBCL before walking through them.

Concretely this requires:

1. A new dispatch label `L_STREXC` (or similar) that, when fired by
   SALT2, restores PATBCL = saved-inner-PATBCL (from the trap's slot[2]
   or from a new cstack save) and either jumps to L_SALT3 (if no further
   inner traps remain — clean fail) or L_SALT2 (continue walking).
2. A new constant `STREXCCL` analog of FNCDCL — descriptor with FNC flag
   and a.i = pointer to the L_STREXC handler.
3. A new PATBRA case (case 41+) wired up.
4. STARP6/DSARP success path: if inner pushed entries, push the STREXCCL
   trap with slot[2] = inner PATBCL (or saved-inner-PATBCL recovered
   from cstack).

This also requires the SYMMETRIC restoration: when walker descends past
the inner region and hits THE BOTTOM (where outer traps begin), PATBCL
must restore to OUTER. This happens automatically in the existing
mechanism IF outer's existing traps (e.g. DSAR redo) are still on PDL
below the inner region. They are.

The session #54 zeroing approach was wrong because it conflated "leaked
inner trap" with "stale memory" — the leaks are real and live, just
need PATBCL switching when consulted.

## Open question for session #55

Should the new STREXCCL sentinel be installed at STARP2/DSARP2 success
ONLY (when leaks are detected), or unconditionally? SPITBOL's `p_nth`
checks `beq xt,xs` (no entries pushed → optimize away the sentinel).
In CSNOBOL4 terms: `D_A(PDLPTR) == saved_snapshot` → no sentinel needed.

Implementation choice for session #55:
- **Conditional install** (mirror SPITBOL): saves an unnecessary push
  when inner P pushed nothing. Adds a branch but matches battle-tested
  SPITBOL design.
- **Unconditional install**: simpler code; one extra trap per `*var`
  match. Probably negligible perf cost.

Recommend: **conditional install**, exactly mirroring `p_nth`'s
optimization, for fidelity to the SPITBOL oracle's design.

## Files changed this session

- `docs/F-2-Step3a-session54-zeroing-attempt.diff` — the rejected fix,
  preserved as a "don't try this again" artifact.
- `docs/F-2-Step3a-session54-findings.md` — this file.
- `isnobol4.c` — UNCHANGED at HEAD. Working tree clean.

## Honest circularity check

Session #54's genuine new contributions:

1. **First attempt to apply session #53's targeted-zeroing proposal.**
   Verified that the approach IS the right idea for crash class 119/129
   (those tests now pass) but WRONG for legitimate inner-backtrack
   (kills 4+ previously-passing patterns, blows up beauty).

2. **First explicit demonstration of the inner-backtrack mechanism.**
   The 5-line repro `cmd=(LEN(1)|LEN(2))`, `outer=(*cmd 'X')`, `s='ABX'`
   is now a REGRESSION GUARD — any future fix that breaks it is wrong
   regardless of how many fence_suite tests it improves.

3. **Sharpened diagnosis of the 119 crash.** It is NOT a
   stale-memory-not-overwritten bug; it IS a leaked trap that the walker
   reaches with wrong PATBCL because no DSAR redo trap routes the walker
   through L_UNSC first. The fix needs to install PATBCL-restore
   sentinels in STARP6/DSARP success paths, mirroring SPITBOL p_nth's
   `=ndexc` mechanism.

What session #54 did NOT do:

- Did not write the STREXCCL sentinel implementation. That is session
  #55's work.
- Did not commit any code changes (regression guard violated).

## Pattern continues

Sessions #44–#54 = 11 sessions on F-2 Step 3a. fence_function preserved
10/10 throughout. fence_suite has graduated from 24/2/6 (s51 baseline)
through 27/3/2 (s52/s53 patches) to a discovered-but-not-committed
29/3/0 (s54 zeroing). Beauty self-host stuck at 32–35 lines.

The new finding — the architectural cause is "missing PATBCL-restore
sentinel on STAR/DSAR success" — gives session #55 a concrete next
step that's distinct from "zero leaked slots" or "rewind PDL". The
SPITBOL reference for the implementation (`p_nth` at sbl.min:12213)
is well-understood.

## Recommended session #55 plan

1. Apply `docs/F-2-Step3a-session52-flpop-fix.patch`.
2. Define `STREXCCL` constant + `XSTREX`/`L_STREXC` handler that
   restores PATBCL from slot[2] and falls into L_SALT3 (or L_SALT2 if
   slot[3]'s LENFCL flag indicates).
3. Wire up new PATBRA case.
4. At STARP6 success and DSARP2 success: if `D_A(PDLPTR) > saved_snapshot`,
   push STREXCCL trap with slot[2] = inner PATBCL (saved on cstack at
   STARP6/DSARP2 entry — ADD a new cstack push for inner-PATBCL alongside
   the existing PDLPTR-snapshot push).
5. Run fence_function (must be 10/10), fence_suite (target 30+/30+
   including 119, 129), 5-line inner-backtrack repro (must produce
   `inner backtrack worked`), beauty self-host (target ≥ 500 lines).
6. If all gates pass, commit. If any regress, do NOT commit; document
   findings.

## Files of interest (for session #55)

- `csnobol4/isnobol4.c` lines 12163–12245 (STARP6/STARP2/STARP5/DSAR/DSARP)
- `csnobol4/isnobol4.c` lines 11465–11572 (SALT/SALT1/SALT2/SALT3/UNSC)
- `csnobol4/isnobol4.c` lines 11481–11562 (PATBRA dispatch table)
- `csnobol4/v311.sil` lines 10872–10880 (FNCDCL etc. — model for STREXCCL)
- `x64/sbl.min` lines 12213–12230 (`p_nth` — the canonical reference)
- `x64/sbl.min` lines 11515–11600 (expression-pattern doc block)
- `csnobol4/docs/F-2-Step3a-session54-zeroing-attempt.diff` (rejected
  approach; reading prevents repeating it)
