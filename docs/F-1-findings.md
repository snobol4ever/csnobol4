# F-1 findings — FENCE(P) bug investigation (sessions #42–#43)

GOAL-CSN-FENCE-FIX F-1 investigation rung.  This document captures the
state of the FENCE bug investigation at the close of session #43 and
names the chosen design for F-2 (implementation rung).

## What sessions #42 and #43 established

### SPITBOL reference (sbl.min:11978–12039)

SPITBOL's FENCE(P) is implemented as four cooperating primitives
`p_fna`, `p_fnb`, `p_fnc`, `p_fnd` — same arity as CSNOBOL4's FNCA/B/C/D.
Critically, SPITBOL saves only **two** items at FENCE entry:

```
p_fna  ent  bl_p0
       mov  -(xs),pmhbs      stack current history stack base
       mov  -(xs),=ndfnb     stack indir ptr to p_fnb (failure)
       mov  pmhbs,xs         begin new history stack
       brn  succp            succeed
```

`pmhbs` (current history-stack base, analog of CSNOBOL4's PDLHED) and
an indirect pointer to `=ndfnb` (the failure trap label).  Both saves
go on `xs` — the single failure stack — not on a separate cstack.

CSNOBOL4 saves six things (MAXLEN, LENFCL, PDLPTR, PDLHED, NAMICL,
NHEDCL) and saves them on the C-level cstack via PUSH macros.  This
two-stack split is the source of the bug.

### Why FENCE is structurally unique

ATP and BAL also do `PUSH (MAXLEN, LENFCL, PDLPTR, PDLHED, NAMICL,
NHEDCL)`, but each wraps the PUSH/POP around a single `RCALL TRPHND`
C function call — the PUSH and POP balance within one C-stack frame,
inside one SAVSTK/RSTSTK pair.  No issue.

FENCE's FNCA pushes; FNCB and FNCC pop on **separately-dispatched**
labels reached only via the failure walker (SALT2) or via SCIN3
forward-dispatch.  Dozens of `RSTSTK()` calls fire between FNCA's
PUSH and the matching POP, every one of which can rewind cstack to
positions that overwrite FNCA's saved slots with unrelated values.

This is the cstack-overwrite bug class.  The original `0xc0` sentinel
in PDLPTR.a.i at the L_SALT1 crash site is exactly such an overwrite
artifact.

### Layouts attempted in F-1 (Layout A through E)

Multiple variations of design D3 (PDL-trap-entry extension) were tried
in session #43.  All failed.  Summary:

| Layout | Saves at | Result |
|--------|----------|--------|
| A (5-slot)  | extending trap entry from 3 → 5 slots                | Rejected by session #41 reasoning (later shown faulty). |
| B (7-slot above) | trap slots 1-3 + saves at slots 4-7 above PDLPTR | 4/10 fence_function PASS — subsequent SCIN3 pushes overwrite save slots 4-6. |
| C (saves below) | saves at PDLPTR+1..+4, trap above            | 10/10 fence_function PASS but tiny repro still crashes — overwrites the SCIN3-around-FNCA entry at PDLPTR+1..+3 (bug only visible at deep nesting). |
| D | combined attempt, abandoned mid-edit | did not build cleanly |
| E (saves above prev entry) | saves at PDLPTR+4..+7 above SCIN3-around-FNCA, trap at +8..+10 | 10/10 fence_function PASS; tiny repro switched crash signature to L_SCIN4 (session #41 signature) — same secondary bug as the C-helper approach. |

**Conclusion from F-1:** D3 in any layout cannot fix this without
also addressing what the failure walker does after FNCB returns
PDLPTR to outer.  The save/restore mechanism alone is not enough.
The session #42 trace evidence (byte-correct restoration still
crashing) was correct.

## The Gimpel 1973 paper finding (session #43, late)

The paper *"A Theory of Discrete Patterns and Their Implementation
in SNOBOL4"* (Gimpel, CACM 16:2, Feb 1973) gives the canonical
explanation.

### Page 3 — FENCE explicitly named "maverick"

> *"FENCE, which can be written FENCE = NULL | ABORT, also does
> not conform.  It is probably best to treat ABORT as a maverick
> pattern; any development sufficiently general to handle the case
> of ABORT will probably not effectively deal with other cases.
> Also, the implementation treats ABORT specially."*

The original SNOBOL4 implementers acknowledged FENCE as a special
case that does not fit the normal pattern-primitive abstraction.
CSNOBOL4 inherited the special-case implementation and the special
case is what's broken.

### Page 8, Figure 11 — STAR/RESTAR is the working analog

The paper presents `*P` (unevaluated expression) as a compound
that does pattern matching by **recursively calling SCAN**:

```
STAR    GT(CURSOR, LENGTH - RESID(PATTERN))     :S(LF)
        P = ARG(PATTERN)
STAR1   P = EVAL(P)                              :F(MF)
        IDENT(DATATYPE(P), 'EXPRESSION')         :S(STAR1)
        PUSH(NULL)
        PUSH(CURSOR)
        SCAN(LENGTH - RESID(PATTERN), P)         :F(MF)S(S)

RESTAR  CURSOR = POP()
        P = POP()
        IDENT(P, NULL)                           :S(MF)
        SCAN(LENGTH - RESID(PATTERN), P)         :F(MF)S(S)
```

Quoted from page 8:
> *"This can be done by a call (recursive) to the function SCAN
> if we first provide isolation between this call and previous uses
> of the history stack."*

> *"SNOBOL4 has a separate system stack for the purpose of making
> recursive calls, and this is where values such as the cursor, the
> pattern, the subject (via pointer) are saved.  The history stack,
> as we will see, must be different from the system stack."*

**This is exactly FENCE's structural problem.**  STAR achieves
isolation via the C-level recursive call: the locals it saved on
the C call frame come back into scope when SCAN returns.  No
long-lived cstack saves required.

CSNOBOL4's EXPVAL already follows this shape: PUSH/POP wraps a
single RCALL.  ATP and BAL likewise.  All work because the saves
live exactly as long as one C-frame.

CSNOBOL4's FENCE breaks this rule by splitting save and restore
across non-adjacent label dispatches.

## D6 — Recursive-SCAN reimplementation (the chosen design)

Reimplement FENCE(P) following the STAR/EXPVAL pattern.  Single
forward-dispatch primitive with a recursive SCAN call.

### Sketch (Gimpel-style pseudocode)

```
FENCE   PUSH(NULL)                          # PDL sentinel — isolates the
        PUSH(CURSOR)                        # inner SCAN's history walk
        SCAN(LENGTH, ARG(PATTERN))          :F(MF)S(FENCE_OK)

FENCE_OK
        # Inner P succeeded.  Replace the NULL+CURSOR sentinel
        # with an FNCD-trap entry that fails on backtrack — i.e.
        # the seal that gives FENCE its semantics.
        POP()                               # discard sentinel cursor
        POP()                               # discard NULL
        PUSH(FNCD_TRAP)                     # outer-fail seal
        PUSH(CURSOR)
                                            :(S)

FNCD                                        # entered on backtrack into seal
                                            :(MF)
```

### Why this fixes the bug

FNCA's locals (MAXLEN, LENFCL, PDLPTR, PDLHED, NAMICL, NHEDCL) sit
in the C-level call frame of FENCE's C handler for the duration of
the recursive SCAN call.  They cannot be overwritten by RSTSTK
because they're not on cstack — they're on the C runtime stack.

When SCAN returns, those locals are still in scope.  No save/restore
mechanism is needed at all.

### Differences from STAR/EXPVAL

- STAR uses `LENGTH - RESID(PATTERN)` to break left-recursive loops
  via the residual.  FENCE has no left-recursion concern — pass
  full LENGTH.
- STAR has a RESTAR primitive to handle backtracks into uncleaned
  alternatives.  FENCE has FNCD (the seal) which fails outright on
  any backtrack — simpler.
- STAR's argument is an unevaluated expression that needs `EVAL`.
  FENCE's argument is already a compiled pattern.

### What changes in code

- **`v311.sil`**: replace FNCA/FNCB/FNCC/FNCD (lines 4093-4156)
  with a single FENCE primitive that does the recursive RCALL to
  SCAN and an FNCD seal.  Remove the FNCAPT/FNCCPT pattern-node
  templates.
- **`v311.sil:4179-4204` FNCP builder**: change to compile FENCE(P)
  as a single-node pattern carrying P as ARG, instead of the current
  three-node `[FNCA] -> P -> [FNCC]` chain.  Pattern node template
  becomes a single 4-descriptor FENCEPT (function descr, then-or,
  value-residual, ARG).
- **`isnobol4.c` and `snobol4.c`**: regenerate from new SIL via
  `genc.sno`, or hand-edit FNCA/B/C/D regions.
- **`include/macros.h`**: add an RCALL-to-SCAN helper if the
  existing RCALL machinery doesn't already support what we need
  (it should — ATP/EXPVAL use the same shape).
- **Rename** the failure label from `FNCD` to keep the existing
  PATBRA dispatch slot; the PATBRA index 40 should still map to
  FNCD-equivalent code.  The seal entry uses FNCDCL as before so
  the SCIN3 dispatch table doesn't shift.

### Risks

- **Pattern-shape change is a contract break.**  External SNOBOL4
  programs that introspect pattern internals (rare, but possible)
  could be affected.  Standard FENCE users won't notice.
- **The 10-test fence_function/ regression** must still pass after
  the rewrite.  Single-node compilation may shift some node-counting
  tests; expect minor test infrastructure adjustments.
- **EXPVAL composes with FENCE deeply** (beauty.sno hits this in
  statement 1074: `*snoParse *snoSpace` where snoParse contains
  FENCE).  The recursive-SCAN approach handles this naturally —
  no special composition logic needed — but verifying is essential.
- **Backtrack semantics around FNCD** may need re-examination.  The
  current FNCD does `MOVD PDLPTR,PDLHED` to discard alternatives;
  under D6 the same primitive can be retained for seal-entry
  dispatch.

### Validation order for F-2

1. Compile-only smoke (just build, no execution): catch SIL syntax
   errors.
2. fence_function/ regression: 10/10 expected.
3. Tiny repro: `printf '   X         =  ARRAY('1:4')\nEND\n' >
   /tmp/tiny.in; SNO_LIB=. /home/claude/csnobol4/snobol4 -bf
   -P64k -S64k beauty.sno < /tmp/tiny.in` should produce >0 lines
   without segfault.
4. Full self-host: same beauty.sno against itself, expect >500 lines.
5. Smoke gates: one4all Smoke=7, Broker=49.

## State at end of session #43

- **csnobol4 HEAD:** `4172be1` (clean baseline, F-0 already complete)
- **F-1 outcome:** Layouts A-E of D3 attempted and rejected.  Gimpel
  1973 paper read.  Design D6 (recursive-SCAN, modeled on STAR)
  identified as the structurally correct fix.
- **Code in working tree:** none.  All F-1 experiments reverted.
- **Next action:** F-2 implementation rung — implement D6 per the
  sketch above.

## Open questions for F-2

1. Does CSNOBOL4's existing RCALL-to-SCAN machinery support the call
   shape STAR uses?  Read `STARP6`/`L_STARP6`/`L_STAR1` in
   `isnobol4.c` and confirm.
2. Is there a precedent in CSNOBOL4 for compiling a primitive whose
   pattern node carries a sub-pattern as an argument?  STAR does
   exactly this — model FENCE on STAR's pattern-node layout.
3. After the rewrite, can FNCAPT/FNCCPT be deleted or do other
   parts of the code reference them?  `grep -n FNCAPT v311.sil`
   should answer.

