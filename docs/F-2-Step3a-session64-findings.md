# F-2 Step 3a Session #64 Findings

## Genuine new contributions

### 1. TXSP-corruption write site identified and fixed (committable)

Per session #63's plan, instrumented every `S_L(TXSP) = ...` site in
`isnobol4.c`.  The corruption identified by session #63 — TXSP becoming
`140702518392224 = cmd's PATBCL` between SALT2 #5 (STREXCCL fire) and
SALT2 #6 — is at **isnobol4.c line 11498**:

```c
L_SALT2:
    D(XCL) = D(D_A(PDLPTR) + DESCR);
    D(YCL) = D(D_A(PDLPTR) + 2*DESCR);
    D_A(PDLPTR) -= 3*DESCR;
    D(PATICL) = D(XCL);
    if (D_A(PATICL) == 0)
        goto L_SALT3;
    S_L(TXSP) = D_A(YCL);          /* <-- BUG: fires for FNC traps too */
    if (!(D_F(PATICL) & FNC))
        goto L_SCIN3;
    D(PTBRCL) = D(D_A(PATICL));
L_PATBRA:
```

`S_L(TXSP) = D_A(YCL)` executes for **all** non-zero PATICL entries,
including FNC-flagged traps.  For SCIN3-pushed traps (non-FNC),
slot[2] is legitimately a saved cursor (TXSP) and restoring it makes
sense.  For FNC traps (STREXCCL with slot[2]=PATBCL pointer, FNCDCL
with slot[2]=seal-base pointer, NMECL/DNMECL with slot[2]=name-list
pointers), slot[2] is **handler-specific data**, not a cursor.
Writing it into TXSP corrupts the scan cursor.

**Fix** (committed-ready):

```c
L_SALT2:
    D(XCL) = D(D_A(PDLPTR) + DESCR);
    D(YCL) = D(D_A(PDLPTR) + 2*DESCR);
    D_A(PDLPTR) -= 3*DESCR;
    D(PATICL) = D(XCL);
    if (D_A(PATICL) == 0)
        goto L_SALT3;
    /* session #64: only restore scan cursor (TXSP) from slot[2] for non-FNC
     * traps.  For FNC traps (STREXCCL, FNCDCL, etc.), slot[2] holds handler-
     * specific data (e.g. a PATBCL heap pointer), NOT a cursor offset.
     * Writing it into TXSP corrupts the cursor, causing spurious matches. */
    if (!(D_F(PATICL) & FNC)) {
        S_L(TXSP) = D_A(YCL);
        goto L_SCIN3;
    }
    D(PTBRCL) = D(D_A(PATICL));
L_PATBRA:
```

**Gates with TXSP-only fix**:

| Gate | Baseline | s64 |
|------|----------|-----|
| fence_function | 10/10 | 10/10 ✓ |
| fence_suite | 44/4/0 | 44/4/0 ✓ |
| guard5 | OK | OK ✓ |
| beauty self-host | 42 lines | 42 lines ✓ |
| repro5 (test 119 shape) | unexpected match | unexpected match ✗ |

**No regression in any direction.**  Gate counts unchanged.  TXSP-only
fix is correctness-improving and committable.

### 2. Why TXSP fix alone doesn't repair the 4 wrong-answer FAILs

Re-traced repro5 with TXSP-only fix in place.  TXSP correctly stays at
0 throughout walker descent (was 140702518392224 at SALT2 #6 in s63).
But output is still `unexpected match` because:

The 6th SALT2 event has `slot1=0x60` (non-FNC), `PATBCL=cmd` (pre-set
by STREXCCL at event #5), and the walker takes SCIN3 fall-through.
SCIN3 reads `D(PATBCL + PATICL + DESCR) = D(cmd + 0x68)` which is a
valid pattern node inside cmd — specifically the `'ab'` alternative
of cmd's `'a' | 'ab'` alternation.  The walker dispatches that
alternative, which matches `'ab'` from cursor 0, advancing to cursor 2.
RPOS(0) succeeds → "unexpected match".

The walker is dispatching a **leaked inner-FENCE alt-cont** that was
pushed during the original `*cmd` matching.  FENCE should have
prevented this.

### 3. FNCDCL-at-P2 fix (attempted, not committable)

Hypothesis: place the FNCDCL seal at P2 (top of leaked region) instead
of P1 (clean base) so the failure walker hits FNCDCL before any
leaked alts.

Implemented as:

```c
{
    int_t p2_save = D_A(PDLPTR);  /* P2 = top of leaks */
    POP(NHEDCL); POP(NAMICL); POP(PDLHED);
    POP(PDLPTR);                  /* P1 + 3*DESCR */
    D_A(PDLPTR) -= 3*DESCR;       /* discard SCFLCL → P1 */
    POP(YCL); POP(XCL); POP(PATICL); POP(PATBCL); POP(MAXLEN);
    D_A(TMVAL) = D_A(PDLPTR);     /* TMVAL.a = P1 */
    D_F(TMVAL) = D_V(TMVAL) = 0;
    D_A(PDLPTR) = p2_save + 3*DESCR;
    if (D_A(PDLPTR) > D_A(PDLEND)) BRANCH(INTR31)
    D(D_A(PDLPTR) + DESCR) = D(FNCDCL);
    D(D_A(PDLPTR) + 2*DESCR) = D(TMVAL);   /* slot[2] = P1 */
    D(D_A(PDLPTR) + 3*DESCR) = D(LENFCL);
}
goto L_SCOK;
```

**Result**: still `unexpected match`.  Trace evidence shows the FNCDCL
**is** placed at P2 (e.g. PDLPTR=0x...a30) but **never fires** — the
walker never reaches it.

### 4. Architectural finding — why FNCDCL-at-P2 doesn't help

After FNCA returns success via SCOK, control returns up the call stack
to the enclosing `*outer` scan (DSAR/STARP6 dispatched `*outer`).
That scan continues (more ARBNO iterations of `*cmd`).  Eventually
`*outer` returns and **its STARP2 success path runs**.

STARP2 does:
1. POPs cstack including the saved-pre-SCIN PDLPTR — restoring
   PDLPTR to its `*outer`-entry value (which is BELOW the FNCDCL
   I placed at P2).
2. Installs its own STREXCCL sentinels at top + bottom of the
   `*outer`-leak region.

After STARP2, **PDLPTR is far below my FNCDCL position**.  FNCDCL is
now in "abandoned" memory — still physically present, but above
PDLPTR.  When subsequent operations push PDL entries (RPOS(0)'s
matching machinery, etc.), they overwrite that abandoned region in
some cases, but not always cleanly.

When RPOS(0) finally fails and the failure walker descends from
current PDLPTR, it walks through STARP2's STREXCCL bracketed region.
The walker descends past STARP2's top STREXCCL (PATBCL → cmd), then
into the leak region — which contains the still-present leaked
inner-FENCE alt-conts (pushed during cmd's original matching, still
in physical memory).  Walker dispatches them under PATBCL=cmd, finds
valid pattern nodes, matches.

**The key insight**: FENCE's leaked alt-conts and `*outer`'s STARP2
leak region OVERLAP in physical memory.  STARP2 thinks the entire
region is its own legitimate ARBNO redo region.  STREXCCL-top says
"PATBCL=inner-cmd applies to everything below".  The walker then
treats leaked FENCE alts as legitimate cmd alts.

### 5. The deeper problem (architectural)

The bug class is **multi-iteration ARBNO of `*var`-FENCE leaks** as
session #57 named it, but the failure mechanism is more subtle than
session #57 understood:

- Session #57's hypothesis: walker descends into iter#N's leaks
  unprotected because their STREXCCL was consumed.
- Session #63's hypothesis: TXSP corruption from FNC slot[2].
- **Actual mechanism (session #64)**: STARP2's PDLPTR-restore
  abandons FENCE's seal (FNCDCL) along with the leaked alts.
  STARP2 then installs STREXCCL sentinels that route the walker
  THROUGH the abandoned region under inner PATBCL.  FENCE seal is
  bypassed because it's no longer in PDL, but its leaks are still
  in physical memory and reachable via STARP2's STREXCCL routing.

### 6. Implications for fix design

The fix candidates session #57 proposed (a/b/c/d) need to be
re-evaluated against this finding:

- **Session #57 (c) — persistent STREXCCL across iterations**:
  doesn't help because by the time the walker reaches the relevant
  region, it's under STARP2's outer STREXCCL routing, not FNCA's.
- **Session #57 (b refined) — paired bottom STREXCCL**:
  this is what session #58 implemented (and works for memory
  corruption — eliminated all 6 CRASHes).  Doesn't help with
  semantic FENCE-seal violations.
- **Session #57 (d) — truncate-on-success with deferred-alts array**:
  is the architecturally correct fix.  FNCA's success path should
  ZERO or PHYSICALLY REMOVE leaked alt-conts from PDL memory, so
  even after STARP2 abandons the region and re-routes through it,
  the leaks are no longer dispatchable.

### 7. Recommended session #65 plan

**Step 1: Commit session #64 TXSP fix.**  It's correctness-improving
and gate-neutral.  Locks in the elimination of one bug-class.

**Step 2: Implement session #57 (d) — leaked-alt zeroing at FNCA
success.**  The targeted-zeroing approach session #54 attempted was
right in spirit but wrong in placement (zeroed at STARP2/DSARP2
instead of FNCA-success).  At FNCA-success, walking from P1+3*DESCR
to P2 and zeroing slot[1] of any non-FNC trap should:
- Eliminate leaked FENCE alts permanently.
- NOT affect guard5's inner-backtrack (because that's NOT inside
  a FENCE — guard5's pattern is `cmd=(LEN(1)|LEN(2)); outer=*cmd 'X'`,
  no FENCE involved).
- NOT affect `*outer`'s STARP2 STREXCCL routing (the routing still
  works; it just routes through zeroed slots which take SCIN3-fall-
  through with PATICL=0 → goto L_SALT3 → continue descent).

The session #54 zeroing wrapped STAR/DSAR's success path; session
#65's zeroing should wrap FNCA's success path only.  Different
locus, different semantics.

**Step 3: Test gate stack** — same as before:
- fence_function 10/10
- fence_suite ≥ 45/48 (target: 119/124/127/129 → OK)
- guard5 → `inner backtrack worked`
- beauty ≥ 500 lines

If guard5 fails, the zeroing-at-FNCA hypothesis is wrong and we
need to look elsewhere.

## Files this session

- `csnobol4/docs/F-2-Step3a-session64-findings.md` — this file.
- `csnobol4/isnobol4.c` — TXSP fix (committable, gate-neutral).

## Honest checkpoint

Sessions #44-#64 = 20 sessions on F-2 Step 3a.  Each has narrowed
the bug.  Session #64's contributions:

1. **Identified and fixed** the exact line (isnobol4.c:11498) that
   session #63 traced the corruption to.  Fix is gate-neutral.
2. **Refuted FNCDCL-at-P2** as a sufficient fix via direct trace
   evidence — FNCDCL gets abandoned by `*outer`'s STARP2.
3. **Identified the architectural mechanism**: STARP2's
   PDLPTR-restore abandons FNCA's seal along with the leaked alts;
   STARP2 then re-routes the walker through the abandoned region.
4. **Refined fix recommendation** for session #65: zero leaked
   alts at FNCA-success (session #57 (d) approach, not session
   #54's zeroing).
