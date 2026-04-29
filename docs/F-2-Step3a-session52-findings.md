# F-2 Step 3a — session #52 findings (SPITBOL-aligned flpop fix)

GOAL-CSN-FENCE-FIX F-2 Step 3a continuation (session #52, 2026-04-29).

## TL;DR

Session #52 implemented the **first SPITBOL-aligned L_FNCD fix** in the
session #44–#51 sequence.  Unlike sessions #49–#51 (which adjusted seal
slot[2] arithmetic alone), this fix changes **both** the seal slot[2]
write AND L_FNCD itself, faithfully mirroring SPITBOL's
`p_fnc/p_fnd + flpop` mechanism.

**Result:** fence_suite improves 24→26 OK, 6→2 CRASH; fence_function 10/10
preserved.  But beauty regresses clean Parse Error → SEGV at stmt 1074
(35 → 33 lines).  Per RULES.md (regression in error class) the fix is
NOT committed; saved as `docs/F-2-Step3a-session52-flpop-fix.patch` for
the next session to combine with a separate fix for the residual bug.

## SPITBOL reference re-read (sbl.min:11978-12046)

```
p_fna  ent  bl_p0
       mov  -(xs),pmhbs      stack current history stack base
       mov  -(xs),=ndfnb     stack indir ptr to p_fnb (failure)
       mov  pmhbs,xs         begin new history stack
       brn  succp

p_fnc  ent  bl_p0            (success path)
       mov  xt,pmhbs         get inner stack base ptr
       mov  pmhbs,num01(xt)  restore outer stack base
       beq  xt,xs,pfnc1      OPTIMIZE if no alternatives
       mov  -(xs),xt         else stack inner stack base
       mov  -(xs),=ndfnd     stack ptr to ndfnd (the seal)
       brn  succp

p_fnd  ent  bl_p0            (seal fires)
       mov  xs,wb            xs = inner base (from seal entry)
       brn  flpop            pop one more entry and fail

flpop  rtn
       add  xs,*num02        pop two entries off stack
       (falls into failp)
```

The key insight previously underweighted: `p_fnd` does **two** things,
not one:
1. `xs = wb` — rewinds xs to inner base (the position xs had AFTER
   p_fna's two pushes).
2. `flpop` — pops 2 more words = the p_fna entry itself (pmhbs +
   ndfnb).

After p_fnd: xs is at the position BEFORE p_fna was dispatched.  The
outer failure walker then reads whatever entry dispatched p_fna,
naturally consuming it.

## CSNOBOL4 mapping

CSNOBOL4 PDL trap entries are **3 slots** (dispatch, cursor, lenfcl).
SPITBOL `xs` entries are **2 words** (dispatch, cursor).  The
SPITBOL `flpop` "pop 2 words" = "pop 1 trap entry" in CSNOBOL4 terms.

| SPITBOL              | CSNOBOL4                                       |
|----------------------|------------------------------------------------|
| `pmhbs`              | global `PDLHED`                                |
| `xs`                 | global `PDLPTR`                                |
| inner base after p_fna | `PDLPTR` after FNCA's `+= 3*DESCR` SCFLCL push (= entry-PDLPTR) |
| p_fna's 2-word entry | FNCA's 3-slot SCFLCL trap entry                |
| seal slot stores `wb=xt`=inner base | seal slot[2] should store entry-PDLPTR |
| p_fnd: `xs=wb; flpop` | L_FNCD: `PDLPTR=YCL; PDLPTR -= 3*DESCR; FAIL` |

## The fix (verified for tests 109, 113; saved to .patch)

In **`L_FNCA` success path** (around line 12320):

```c
/* OLD (broken — stored outer PDLHED, not inner base) */
D(D_A(PDLPTR) + 2*DESCR) = D(PDLHED);

/* NEW (SPITBOL-aligned: store inner base = entry-PDLPTR) */
D_A(TMVAL) = D_A(PDLPTR) - 3*DESCR;   /* PDLPTR just had += 3*DESCR for seal push;
                                          subtracting gives entry-PDLPTR */
D_F(TMVAL) = D_V(TMVAL) = 0;
D(D_A(PDLPTR) + 2*DESCR) = D(TMVAL);
```

In **`L_FNCD`** (around line 12363):

```c
/* OLD (missing flpop) */
D(PDLPTR) = D(YCL);
D(NAMICL) = D(NHEDCL);
BRANCH(FAIL)

/* NEW (SPITBOL p_fnd analog) */
D(PDLPTR) = D(YCL);            /* xs = inner base */
D_A(PDLPTR) -= 3*DESCR;        /* flpop: consume SCFLCL trap entry */
D(NAMICL) = D(NHEDCL);
BRANCH(FAIL)
```

## Test results with the fix

### fence_function (regression gate): 10/10 PASS ✓

### fence_suite (32-test gate)

| Test  | Before fix     | After fix      | Net change |
|-------|----------------|----------------|------------|
| 109   | CRASH          | OK             | +1         |
| 113   | CRASH          | OK             | +1         |
| 114   | CRASH          | FAIL           | crash→fail |
| 119   | CRASH          | CRASH          | unchanged  |
| 124   | FAIL           | FAIL           | unchanged  |
| 127   | FAIL           | FAIL           | unchanged  |
| 129   | CRASH          | CRASH          | unchanged  |
| 130   | CRASH          | FAIL           | crash→fail |
| **Total** | **24/2/6** | **26/4/2**     | +2 OK, -4 CRASH, +2 FAIL |

The fix correctly handles the **canonical FENCE-via-`*var`** case
(test 109, the explicit beauty-bug minimal repro) and the
**double-`*var`** case (test 113).

### beauty regression (REGRESSION — fix NOT committed)

| State           | Before fix          | After fix       |
|-----------------|---------------------|-----------------|
| beauty tiny     | "Parse Error" (clean) | SEGV at stmt 1074 |
| beauty self-host wc | 35 lines            | 33 lines        |

Beauty's crash signature with the fix matches test 119/129's CRASH
signature — confirming they share root cause.

## SPITBOL oracle truth: 30/32, not 32/32

Running fence_suite against SPITBOL x64 reveals **two `.ref` files
that do not match SPITBOL's actual behavior**:

| Test | SPITBOL output                          | .ref file expects             |
|------|-----------------------------------------|-------------------------------|
| 127  | `k=age s="age":42 n=42 b=`              | `k=age s= n=42 b=`            |
| 130  | `fail`                                  | `sequence of star-cmd-FENCE matched` |

Test 127's bug 2 in the README ("conditional-assign not committed
inside FENCE") is a **wrong-`.ref` artifact** — SPITBOL's behavior
matches what bare `BREAK('"') . S` does (latches the whole-string
match into S even though the outer scan resets the position).
csnobol4's `s=` is also nonconforming but for different reasons.

Test 130's `.ref` is also wrong — SPITBOL itself fails on that input.

**Recommendation:** correct these two `.ref` files to SPITBOL's
output before using fence_suite as the canonical FENCE oracle gate.

## The remaining bug (still unresolved): PATBCL context-mismatch

**Tests 119, 129 still CRASH.**  Their crash signature is identical
to beauty stmt 1074:

- `PATICL.a.i = 0xc0` (= 192 = 12*DESCR — a small offset value)
- `PATICL.f = 0` — **no FNC flag** → SALT2 falls through to L_SCIN3
- `PATBCL` = current OUTER pattern (heap pointer)
- `D(D_A(PATBCL) + D_A(PATICL))` reads at outer-pattern + 0xc0 →
  ZCL = garbage → next deref crashes

Session #50's diagnosis stands: this slot[1] was pushed by an inner
sub-pattern's SCIN3 under a different PATBCL.  When inner-P matched
successfully and the recursive SCIN returned, those traps weren't
cleared from PDL.  The outer failure walker reaches them with the
outer PATBCL and reads garbage.

The session #52 SPITBOL-aligned fix correctly handles the
**single-level** `*var → FENCE → ...` chain (109/113), but does NOT
help when there's an **outer ARBNO loop** (119/129) — because ARBNO's
own iteration traps and any orphaned inner-P traps from prior
ARBNO iterations remain on PDL after the seal fires.

## Why this is the "next-deeper" bug consistent with the goal-file pattern

PLAN.md's goal-file note for session #51:

> Sessions #44–#51 = 8 sessions on F-2 Step 3a, beauty self-host stuck
> at 33–35 lines for the entire run.  Each session has landed a real
> fix and each session has been blocked by the next-deeper bug.

Session #52 fits the same pattern: a real fix (SPITBOL-aligned
flpop), real progress (tests 109, 113 + 130 + 114), real blocking
bug at the next level (ARBNO orphaned-trap PATBCL mismatch).

## Recommendations for session #53

1. **First, correct the two bad `.ref` files** (127, 130) to match
   SPITBOL output.  The fence_suite gate then becomes a legitimate
   30-target oracle (not a 32-target one).

2. **Combine the session #52 patch with a fix for ARBNO trap leaks.**
   The session #52 patch is verified correct in isolation (109, 113,
   130-shape, 114-shape).  It only needs to be paired with a
   mechanism that prevents inner-pattern PDL traps from leaking
   across an ARBNO/STAR success boundary.

   Two design candidates:

   - **Candidate A: PDL slot zeroing on SCIN success.**  When an
     inner SCIN call returns success, walk back from current PDLPTR
     to the pre-call PDLPTR-snapshot and zero out the dispatch slots
     so SALT2 hits `PATICL == 0` → L_SALT3 (clean fail-through).
     This needs PDLPTR-snapshot to be saved at every SCIN entry.

   - **Candidate B: ARBNO-style explicit pop on success.**  Have STAR
     and ARBNO do the equivalent of FNCA's `D_A(PDLPTR) -= 3*DESCR`
     on success — explicitly rewinding PDL to a known position.
     This is closer to how SPITBOL's `p_arb` and `p_str` work.

3. **Read SPITBOL `p_arb` and `p_str` carefully** before designing
   the fix.  Same source-of-truth principle that worked for FENCE.

## Files of interest (this session)

| File | Role |
|------|------|
| `docs/F-2-Step3a-session52-flpop-fix.patch` | Verified-in-isolation patch (apply with `git apply`) |
| `docs/F-2-Step3a-session52-findings.md` | This file |
| `isnobol4.c:12325` | seal slot[2] write (FNCA success path) — change target |
| `isnobol4.c:12371` | L_FNCD body — change target |
| `x64/sbl.min:12041` | SPITBOL p_fnd reference |
| `x64/sbl.min:3144` | SPITBOL flpop reference |
| `x64/sbl.min:16234` | SPITBOL flpop documentation |
| `corpus/crosscheck/patterns/119_pat_arbno_of_fence_via_var_via_outer.sno` | 5-line beauty-class repro |

## Honest circularity check (session #52)

Session #52's genuine new contributions:

1. **First time in the F-2 Step 3a series that L_FNCD itself is
   modified** (sessions #49–#51 only adjusted seal slot[2]
   arithmetic, leaving L_FNCD as `PDLPTR=YCL; FAIL`).
2. **First fix that makes test 109 pass** (was CRASH every prior
   session).  Test 109 is the canonical *var-FENCE seal case — the
   minimal expression of the beauty bug class.
3. **fence_suite improvement from 24/2/6 to 26/4/2** — net +2 OK
   tests, +2 fewer CRASHes.
4. **Identification of the bad `.ref` files** (127, 130) — shrinks
   the real target from 32/32 to 30/32.
5. **Confirmation that ARBNO+`*var`+FENCE (test 119/129) is a
   distinct bug class** from the seal-rewind class — same
   crash signature as beauty, present both before and after the
   session #52 fix.

What session #52 did NOT do: write the ARBNO trap-leak fix.  That
needs a fresh session to read SPITBOL's `p_arb`/`p_str` properly
and probably another session beyond that to land it.

The session #52 patch should NOT be re-derived in a future session
— it's saved verbatim.  Apply it with:

```bash
cd /home/claude/csnobol4 && git apply docs/F-2-Step3a-session52-flpop-fix.patch
```

before stacking any further fix on top of it.
