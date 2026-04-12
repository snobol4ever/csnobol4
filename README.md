# csnobol4

**CSNOBOL4 2.3.3** — the C implementation of SNOBOL4, originally by Philip L. Budne,
maintained here by the [snobol4ever](https://github.com/snobol4ever) project as the
base for the **SCRIP** language family.

---

## What is CSNOBOL4?

CSNOBOL4 is a free, faithful port of the original Bell Labs SNOBOL4 implementation.
It is derived directly from v311.sil — the SIL (SNOBOL4 Implementation Language)
macro source — with C as the target language. The result is a complete, standard-
conforming SNOBOL4 interpreter including:

- Full pattern matching with backtracking
- Dynamic typing and garbage collection
- User-defined data types (`DATA`)
- On-the-fly compilation (`CODE`, `EVAL`)
- The complete standard function library

Upstream home: <http://www.regressive.org/snobol4/csnobol4>  
Original author: Philip L. Budne  
License: BSD 2-Clause (see [COPYRIGHT](COPYRIGHT))

---

## snobol4ever Enhancements

This fork tracks upstream 2.3.3 and adds:

| Feature | Status | Description |
|---------|--------|-------------|
| `FENCE(P)` function | 🚧 In progress | 1-argument FENCE: match P with no backtrack into P from outside |

See [FENCE.md](FENCE.md) for the full design and implementation notes.

---

## Building

```bash
./configure
make
```

Requires: C compiler, `make`. No bison or flex needed (generated files are committed).

Tested on: Linux x86-64, macOS.

```bash
# Quick smoke test
echo "OUTPUT = 'hello world'" | ./snobol4
```

---

## SPITBOL Oracle

The [snobol4ever/x64](https://github.com/snobol4ever/x64) repo contains SPITBOL x64,
the primary correctness oracle for all pattern matching behavior. When in doubt,
SPITBOL is right.

---

## Repository Layout

```
v311.sil          SIL source — the canonical reference implementation
snobol4.c         Main interpreter (generated + hand-edited)
isnobol4.c        Instrumented interpreter variant
data.c / data.h   Static data tables (generated from v311.sil)
data_init.c       Runtime data initialization
syn.c / syn.h     Syntax tables
test/             Test suite
doc/              Documentation
FENCE.md          Design notes for FENCE(P) implementation
```

---

## License

Copyright © 1993–2021 Philip L. Budne. BSD 2-Clause License — see [COPYRIGHT](COPYRIGHT).  
snobol4ever modifications: same license.
