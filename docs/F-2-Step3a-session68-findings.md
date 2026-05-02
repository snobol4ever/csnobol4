# F-2 Step 3a Session #68 Findings

## Status entering session
- Baseline: **46/7/0** (fence_suite), 10/10 (fence_function), Tier F 16/16
- Two candidate patches from session #67: BB-FENCE (202 lines) and A2 zeroing (54 lines)
- Goal: compose BB+A2, achieve 47/6/0, then diagnose Cluster A

## What was accomplished

### 1. Composed BB + A2 (FNCDCL-gated) → 47/6/0

BB-FENCE already includes the L_FNCD fix (goto L_TSALT).
A2's FNCDCL-gate doesn't apply cleanly on top of BB (conflict at L_FNCD).
Extracted STARP2 zeroing block only, injected after STARP2 POPs.
Result: **47/6/0**, guard5 ✓, fence_function 10/10 ✓, Tier F 16/16 ✓, test 150 ✓.

### 2. Diagnostic trace: root cause of Cluster A confirmed

Added S68_TRACE instrumentation across SCIN3, STARP6_ENTRY, STARP2, FNCA, A2 block.

**Key trace on test 148 (input `'ab'`, cmd=FENCE('a'|'ab'), outer=ARBNO(*cmd)):**

```
SCIN3[4]  pdlptr=b920  xcl.a=60  patbra_case=29(XSCOK)  starp6_depth=1
STARP2 #1 pdlptr=b920  strex_entrypdl=b8c0  fncdcl_found=0
...
STARP6 depth=2  →  FNCA_ENTRY  (scin3_count=10)
STARP2 #2 pdlptr=b9e0  strex_entrypdl=b980  fncdcl_found=1  A2_ZERO(b9b0)
```

**Root cause, precisely:**

1. STARP6 #1 enters for `*outer` (DSAR, depth=1). Inner scan visits ARBNO node (case 3)
   and pushes SCIN3[4] at PDL `b920` with `xcl.a=0x60` — the "retry ARBNO body" offset
   in outer's compiled pattern.
2. STARP2 #1 runs BEFORE FNCA fires → `fncdcl_found=0` → A2 FNCDCL-gate skips zeroing.
3. BB FENCE (FNCA) runs later (depth=2), beta truncates to `fence_trap_pos=b9b0`,
   installs FNCDCL.
4. STARP2 #2 runs, FNCDCL in range → A2 fires, zeros a harmless entry at `b9b0`.
   Installs STREXCCL pair bracketing `[b980..b9b0+3*DESCR]`.
5. STARP2 #1 runs, installs STREXCCL pair: bottom at `b8c0`, top at `b950`.
6. RPOS(0) fails. Walker descends:
   - Top-STREXCCL at `b950` fires → PATBCL = outer's pattern (`b5e0`)
   - `b920`: SCIN3[4] `xcl.a=0x60` — walker restores PATICL=0x60, calls SCIN3
   - SCIN3 reads `D(outer + 0x60 + DESCR)` = ARBNO node (XARBN=3, FNC) at `outer[+70]`
   - PATBRA dispatches XARBN → STARP6 re-entered → `*cmd` → FNCA → `'ab'` matches
   - Spurious match.

**FNCDCL at `b9b0` is ABOVE current walker position `b920` — seal bypassed from below.**

### 3. SPITBOL architectural comparison

SPITBOL's `p_exa` sets `pmhbs = xs` (PDLHED analog) at entry, bounding the walker.
Entries pushed BEFORE `p_exa` (outer navigation) are below `pmhbs` — walker can't reach them.
CSNOBOL4 STARP6 does NOT update PDLHED. Session #59 option (a) was architecturally correct
but insufficient: SCIN3[3]/[4] are ABOVE SCFLCL (they're pushed during inner scan),
so setting PDLHED=SCFLCL-position doesn't protect them.

### 4. XARBN-targeted zeroing: 50/3/0

**New A2 variant:** At STARP2 success, for each non-FNC slot[1] entry with `xcl.a != 0`,
read the node at `innerpat + xcl.a + DESCR`. If it is FNC-dispatched and its opcode is
XARBN (=3), zero the entry.

Result: **50/3/0** — gains tests 119, 120, 148, 149. guard5 ✓, fence_function 10/10 ✓,
Tier F 16/16 ✓.

**Regression: test 150 (negative discriminator) now FAILs.**

Test 150 uses `outer = (*cmd | *cmd *cmd | *cmd *cmd *cmd)` — explicit alternation without
ARBNO wrapping. The XARBN check fires because outer's compiled pattern still contains an
ARBNO-like node at some offset, triggering spurious zeroing of a legitimate continuation.

### 5. Saved artifacts

- `docs/F-2-Step3a-session68-diagnostic.diff` — full S68_TRACE instrumentation (303 lines)
- `docs/F-2-Step3a-session68-BB-A2-XARBN-attempt.diff` — BB + L_FNCD + XARBN A2 (234 lines)

Both are uncommitted working-tree artifacts. HEAD is clean at session #67 commit.

## Session #69 plan

**Goal:** Refine XARBN check to avoid false positives on test 150.

**Option A — XARBN + FNCDCL gate (compose):** Zero XARBN-pointing entries ONLY when
FNCDCL exists somewhere in the PDL (`fncdcl_found=1`). Test 150 has no FENCE → no FNCDCL
→ no zeroing → test 150 OK. But same timing problem: at STARP2 #1, `fncdcl_found=0`
even in test 148. Unless STARP2 is re-ordered or FNCDCL scan extends further.

**Option B — XARBN + inner-pattern FNCA check:** For the XARBN node found at `xcl.a+DESCR`,
follow it one level deeper: read `D(ARBNO_node + parm1_offset)` = the ARBNO's inner pattern
pointer. Check if that inner pattern contains FNCA (XFNCA=37) at any of its nodes.
This directly asks "does this ARBNO contain a FENCE?" Only zero if yes.
Need to know parm1 offset in ARBNO pattern block — check `pat.c` or ARBNO descriptor layout.

**Option C — Tag at STARP6 entry:** At STARP6 entry, scan the pattern being dispatched
(`YPTR` = inner pattern) for FNCA nodes. If found, set a `starp6_contains_fence` flag.
At STARP2, gate zeroing on this flag. Avoids mid-walk introspection.

**Option D — Restrict to SCIN3 pushes from XSCOK context:** SCIN3[4] was pushed when
PATBRA was case 29 (XSCOK). Tag SCIN3 pushes with patbra_case in D_V (which is 0 or 2
normally). At STARP2, zero entries with `D_V(slot1) == XSCOK_TAG`. Low-collision risk.

**Recommended: Option B** — precise, no false positives, single ARBNO lookahead.
Need ARBNO descriptor layout: in sbl.min `p_arb` uses `parm1(xr)` = inner pattern pointer.
In csnobol4: check `pat.c` for `ARBNO` block structure to find inner-pattern offset.

## Cluster A remaining tests (of original 7 FAILs)
After session #68 XARBN fix (before test 150 regression guard):
- ✓ Fixed: 119, 120, 124, 148, 149 (5 tests)
- ✗ Remaining: 127, 152 (2 tests — JSON key-value patterns)
- ✗ Regression: 150 (must fix)

Target for session #69: 51+/2-/0 with 150 restored → 51/2/0 minimum.
