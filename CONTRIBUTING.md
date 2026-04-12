# Contributing to csnobol4

This repo is part of the [snobol4ever](https://github.com/snobol4ever) project.

## Ground rules

- **SPITBOL x64 is the oracle.** When behavior is in doubt, `snobol4ever/x64` is right.
- **v311.sil is the reference.** All interpreter changes should trace back to SIL semantics.
- **Do not build the executable in CI** without first running the test suite.
- **No bison/flex.** Generated files (`*.tab.c`, `lex.*.c`) are committed pre-built.

## Code style

- C: 120-character line max, brace on same line, short bodies on one line.
- SIL: follow existing column alignment (`LABEL  OP  ARGS  comment`).
- Commit messages: imperative mood, one line summary, blank line, then detail.

## Testing

```bash
cd test
make
```

New features must include test cases in `test/`.

## Upstream

This fork tracks Philip L. Budne's CSNOBOL4 2.3.3.
Upstream bug fixes should be cherry-picked, not rebased.
