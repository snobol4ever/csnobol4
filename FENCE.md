# FENCE(P) — Design and Implementation

## Background

Standard SNOBOL4 has two forms of `FENCE`:

| Form | Syntax | Meaning |
|------|--------|---------|
| 0-argument | `FENCE` or `&FENCE` | Matches null; if backtracking reaches it, the **entire match statement fails** |
| 1-argument | `FENCE(P)` | Matches P; if backtracking tries to re-enter P from outside, those alternatives are **silently discarded** and failure propagates further out |

CSNOBOL4 2.3.3 implements `&FENCE` correctly but does **not** implement `FENCE(P)`.
This document describes the implementation added in this fork.

---

## Semantics

```snobol4
*  0-arg: FENCE kills the whole match
   S  'AB'  LEN(1) FENCE LEN(1)    :S(FOUND)F(NOTFOUND)
*  matches 'A' then null (FENCE) then 'B' -> FOUND
*  if 'AB' not present -> NOTFOUND (FENCE caused total abort on first retry)

*  1-arg: FENCE(P) — P's alternatives are sealed
   S  FENCE(('A' | 'B') 'X')
*  tries 'AX', if that fails tries 'BX', if FENCE(P) itself fails -> outer fail
*  but once past FENCE(P), no backtracking back into ('A'|'B') is possible
```

The 1-argument form is the workhorse used in real programs (see corpus examples).
It lets you commit to a parse branch without paying for global match abort.

---

## Implementation

### Four new pattern nodes

The implementation follows the SPITBOL x64 design exactly (see `bootstrap/sbl.asm`,
`p_fna` through `p_fnd`, and the Minimal source `v37.min` lines 13314–13344).

```
FENCE(P) compiles to:    [FNCA] ──▶ [P nodes] ──▶ [FNCC]
                           │
                     (FNCB trap on inner PDL during P's match)
                           │
                     (FNCD seal on outer PDL after P succeeds)
```

| Node | Opcode | Role |
|------|--------|------|
| `FNCA` | `XFNCA` (37) | Enter fence: save outer `PDLHED`, push `FNCBCL` inner-fail trap, set new `PDLHED` |
| `FNCB` | `XFNCB` (38) | Inner fail: P exhausted all alternatives → restore outer `PDLHED`, fail outward |
| `FNCC` | `XFNCC` (39) | Exit fence: P succeeded → restore outer `PDLHED`; if P left alternatives, push `FNCDCL` seal |
| `FNCD` | `XFNCD` (40) | Outer seal: backtrack tried to re-enter P → reset PDL past all of P's alternatives, fail outward |

### Key invariant

`PDLHED` (history list head, line 10854 of `v311.sil`) is CSNOBOL4's equivalent of
SPITBOL's `pmhbs`. It marks the inner/outer stack boundary. `NHEDCL` (name list head)
is always paired with it.

### Lines added to v311.sil

| Section | Change | Lines |
|---------|--------|-------|
| Opcode EQUs | `XFNCA`–`XFNCD` (37–40) | 4 |
| `FNCP` PROC | Function builder (constructs FNCA→P→FNCC pattern) | 27 |
| `FNCA/B/C/D` XPROCs | Four matching/failure procedures | 58 |
| Descriptor cells | `FNCACL`–`FNCDCL` | 4 |
| Function node descriptors | `FNCAFN`–`FNCDFN` | 4 |
| Static pattern node templates | `FNCAPT`, `FNCCPT` | 10 |
| `SELBRA` dispatch | Append opcodes 37–40 to existing list | 0 (1 modified) |
| Function registration | `DEFINE FNCESP,FNCP,1` | 1 |
| **Total** | | **108 new lines** |

---

## Test Cases

See `test/fence_function/` for the test suite. Key cases:

1. Basic `FENCE(P)` matches P successfully
2. `FENCE(P)` fails when P fails (outer alternatives still tried)
3. Backtracking cannot penetrate the fence (distinguishes from bare concatenation)
4. Nested `FENCE(FENCE(P))` works correctly
5. `FENCE(P)` in combination with `ARBNO`, `$`, `.`

---

## References

- v311.sil lines 4069–4081 — existing 0-arg FNCE XPROC
- SPITBOL x64 `bootstrap/sbl.asm` lines 5034–5068 — p_fna through p_fnd
- SPITBOL Minimal `v37.min` lines 13314–13344 — canonical design comments
- SPITBOL Manual section on FENCE
