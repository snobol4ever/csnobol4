SNOBOL4  ?= ../../snobol4
SPITBOL  ?= /home/claude/x64/bin/sbl
CORPUS   ?= /home/claude/corpus/crosscheck/patterns

TESTS = 058_pat_fence_keyword \
        059_pat_fence_fn_basic \
        060_pat_fence_fn_fail \
        061_pat_fence_fn_seal \
        062_pat_fence_fn_outer \
        063_pat_fence_fn_optional \
        064_pat_fence_fn_capture \
        065_pat_fence_fn_decimal \
        066_pat_fence_fn_nested \
        067_pat_fence_fn_vs_kw

.PHONY: all csnobol4 spitbol diff clean

all: csnobol4

csnobol4:
	@echo "=== FENCE tests against CSNOBOL4 (programs in corpus) ==="
	@pass=0; fail=0; \
	for t in $(TESTS); do \
	    actual=$$($(SNOBOL4) $(CORPUS)/$$t.sno 2>/dev/null); \
	    expected=$$(cat $(CORPUS)/$$t.ref); \
	    if [ "$$actual" = "$$expected" ]; then \
	        echo "  OK  $$t"; pass=$$((pass+1)); \
	    else \
	        echo "  FAIL $$t"; \
	        echo "    expected: $$expected"; \
	        echo "    actual:   $$actual"; \
	        fail=$$((fail+1)); \
	    fi; \
	done; \
	echo "--- $$pass passed, $$fail failed ---"

spitbol:
	@echo "=== FENCE tests against SPITBOL oracle ==="
	@pass=0; fail=0; \
	for t in $(TESTS); do \
	    actual=$$($(SPITBOL) $(CORPUS)/$$t.sno 2>/dev/null); \
	    expected=$$(cat $(CORPUS)/$$t.ref); \
	    if [ "$$actual" = "$$expected" ]; then \
	        echo "  OK  $$t"; pass=$$((pass+1)); \
	    else \
	        echo "  FAIL $$t"; \
	        echo "    expected: $$expected"; \
	        echo "    actual:   $$actual"; \
	        fail=$$((fail+1)); \
	    fi; \
	done; \
	echo "--- $$pass passed, $$fail failed ---"

diff: spitbol csnobol4

clean:
	@true
