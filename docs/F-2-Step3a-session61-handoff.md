# F-2 Step 3a Session #61 Handoff

## State at handoff

```
fence_suite: 44 OK, 4 FAIL, 0 CRASH (of 48)
beauty self-host: 42 lines (correct output, just truncated)
fence_function: 10/10 (preserved throughout)
last commit: 9cfb8fa
```

Failing tests: 119, 124, 127, 129 — all produce `unexpected match` (wrong
answer, not crash). Root cause is fully understood.

## Root cause: seal sits below leaked entries

### PDL layout after FNCA success (current broken state)

PDL grows upward. Walker descends (high → low).

```
addr high
  [STREXCCL top]       ← installed by STARP2; restores inner PATBCL
  [leaked alt trap]    ← inner SCIN alternation left on PDL
  [FNCDCL seal]  P1    ← pushed by FNCA success at inner base ← WRONG POSITION
  [STREXCCL bot] P0    ← installed by STARP2; restores outer PATBCL
  [SCFLCL base]        ← STARP6's frame (rewritten to STREXCCL bot)
  ...outer entries...
addr low
```

Walker hits `STREXCCL top` → restores PATBCL → continues down.
Walker hits `leaked alt trap` → **dispatched** → wrong match.
Walker never reaches `FNCDCL seal` at P1.

### Required PDL layout (correct)

```
addr high
  [FNCDCL seal]        ← FIRST thing walker sees → FNCD fires → clean rewind
  [STREXCCL top]       ← inner PATBCL restore (never reached on normal fail path)
  [leaked alt trap]    ← skipped because FNCD already fired above
  [STREXCCL bot] P0    ← outer PATBCL restore
  [SCFLCL base]        ← rewritten
  ...outer entries...
addr low
```

## The fix: seal-on-cstack communication

FNCA cannot push the seal at the top because STARP2 hasn't installed the
STREXCCL sentinels yet when FNCA runs (FNCA runs during the inner SCIN;
STARP2 runs after SCIN returns).

**Fix:** FNCA success path pushes `FNCDCL` descriptor onto the **cstack**
(not PDL). STARP2 pops it from cstack and installs it on PDL at the top,
AFTER installing the STREXCCL sentinels.

### FNCA success path change

Current (wrong):
```c
/* Push FNCD seal at clean PDL position. */
D_A(PDLPTR) += 3*DESCR;
if (D_A(PDLPTR) > D_A(PDLEND))
    BRANCH(INTR31)
D(D_A(PDLPTR) + DESCR) = D(FNCDCL);
D_A(TMVAL) = D_A(PDLPTR);
D_F(TMVAL) = D_V(TMVAL) = 0;
D(D_A(PDLPTR) + 2*DESCR) = D(TMVAL);
D(D_A(PDLPTR) + 3*DESCR) = D(LENFCL);
goto L_SCOK;
```

New (correct):
```c
/* Signal STARP2 to install seal at PDL top after STREXCCL sentinels.
   Push a flag on cstack: FNCDCL descriptor. STARP2 pops and installs. */
PUSH(LENFCL);           /* save LENFCL for seal's slot[3] */
PUSH(FNCDCL);           /* signal: install FNCDCL seal at top */
goto L_SCOK;
```

### STARP2 change

STARP2 currently (after STREXCCL sentinel installation):
```c
goto L_SCOK;
```

New STARP2 (after sentinel installation, check for pending seal):
```c
/* Check for FNCA-deferred seal on cstack.
   FNCA pushes FNCDCL onto cstack when inner P succeeds.
   We install it here at PDL top, above STREXCCL sentinels. */
/* NOTE: only do this if top cstack entry is FNCDCL.
   When STAR runs without FENCE(P), no seal is pending. */
if (D_A(cstack) == (int_t)FNCDCL_fn_addr) {    /* check sentinel */
    POP(seal_descr);        /* FNCDCL descriptor */
    POP(saved_lenfcl);      /* LENFCL */
    D_A(PDLPTR) += 3*DESCR;
    if (D_A(PDLPTR) > D_A(PDLEND))
        BRANCH(INTR31)
    D(D_A(PDLPTR) + DESCR) = D(seal_descr);
    D_A(TMVAL) = D_A(PDLPTR);     /* seal base = current top */
    D_F(TMVAL) = D_V(TMVAL) = 0;
    D(D_A(PDLPTR) + 2*DESCR) = D(TMVAL);
    D(D_A(PDLPTR) + 3*DESCR) = D(saved_lenfcl);
}
goto L_SCOK;
```

### Distinguishing FENCE(P) vs plain STAR in STARP2

The challenge: STARP2 is shared between STAR (which never has a seal) and
FNCA (which always does when it succeeds). Options:

**Option A — cstack peek:** Check if top cstack entry matches FNCDCL's
function address. Reliable if cstack discipline is maintained.

**Option B — global flag:** `static int fnca_seal_pending = 0`. Set to 1
at FNCA success, check+clear at STARP2. Simple, not re-entrant but SNOBOL4
is single-threaded.

**Option C — dedicated cstack register:** Add a `FNCA_SEAL` cell (like
SCFLCL, FNCDCL). FNCA success pushes it; STARP2 checks the top entry's
function pointer.

**Recommended: Option B** (simplest, correct for single-threaded SNOBOL4).

```c
/* near top of isnobol4.c, after STREXCCL globals */
static int fnca_seal_pending = 0;
static int_t fnca_seal_lenfcl = 0;
```

FNCA success:
```c
fnca_seal_pending = 1;
fnca_seal_lenfcl = D_A(LENFCL);
goto L_SCOK;
```

STARP2 (after sentinel installation):
```c
if (fnca_seal_pending) {
    fnca_seal_pending = 0;
    D_A(PDLPTR) += 3*DESCR;
    if (D_A(PDLPTR) > D_A(PDLEND))
        BRANCH(INTR31)
    D(D_A(PDLPTR) + DESCR) = D(FNCDCL);
    D_A(TMVAL) = D_A(PDLPTR);
    D_F(TMVAL) = D_V(TMVAL) = 0;
    D(D_A(PDLPTR) + 2*DESCR) = D(TMVAL);
    D_A(TMVAL) = fnca_seal_lenfcl;
    D_F(TMVAL) = D_V(TMVAL) = 0;
    D(D_A(PDLPTR) + 3*DESCR) = D(TMVAL);
}
goto L_SCOK;
```

### FNCBX (inner fail path) — no seal needed, clear flag

```c
L_FNCBX:
    fnca_seal_pending = 0;   /* inner P failed — no seal */
    POP(NHEDCL);
    ...
```

## FNCD: correct rewind after seal-at-top

With seal at top (above STREXCCL top), FNCD fires first. At FNCD entry:
- PDLPTR is at `seal_base - 3*DESCR` (SALT2 decremented past seal)
- YCL = `seal_base` (= PDLPTR+3*DESCR before decrement = top of sentinels)
- After `D(PDLPTR) = D(YCL)`: PDLPTR = seal_base (top of STREXCCL top)
- After flpop `-= 3*DESCR`: PDLPTR = just below STREXCCL top
- `BRANCH(FAIL)` exits SCIN1 → STAR handles failure correctly

The STREXCCL sentinels and leaked entries are now ABOVE PDLPTR — the outer
walker never sees them again on this failure path. The STREXCCL bottom at P0
handles PATBCL restore for any subsequent retry that descends into that region.

## Key files to read at session start

1. `docs/F-2-Step3a-session60-findings.md` — this session's analysis
2. `isnobol4.c` lines ~12187–12320 (STARP6, STARP2, STARP5, DSARP2, FNCA, FNCBX)
3. `isnobol4.c` lines ~12428–12450 (FNCD, STREXC)

## Build commands

```bash
cd /home/claude/csnobol4
make -f Makefile2 xsnobol4 OPT="-O0 -g"
cp xsnobol4 snobol4
# Gate:
cd test/fence_suite && make csnobol4 SNOBOL4=/home/claude/csnobol4/snobol4
cd /home/claude/corpus/programs/snobol4/demo/beauty
SNO_LIB=. /home/claude/csnobol4/snobol4 -bf -P64k -S64k beauty.sno < beauty.sno | wc -l
```

## Done-when

fence_suite: 48/0/0, beauty ≥ 500 lines (full self-host).
