SNOBOL4 ?= ../../snobol4
SPITBOL ?= /home/claude/x64/bin/sbl
TESTS = t01_basic_match t02_outer_alternatives t03_fence_seals_backtrack \
        t04_optional_suffix t05_fence_vs_keyword t06_with_assignment \
        t07_arbno_fence t08_decimal_literal t09_nested_fence t10_error_handling

.PHONY: all csnobol4 spitbol clean

all: csnobol4

csnobol4:
	@echo "=== FENCE(P) tests against CSNOBOL4 ==="
	@pass=0; fail=0; \
	for t in $(TESTS); do \
	    result=$$($(SNOBOL4) $$t.sno 2>/dev/null); \
	    if echo "$$result" | grep -q "^PASS"; then \
	        echo "  OK  $$t"; pass=$$((pass+1)); \
	    else \
	        echo "  FAIL $$t: $$result"; fail=$$((fail+1)); \
	    fi; \
	done; \
	echo "--- $$pass passed, $$fail failed ---"

spitbol:
	@echo "=== FENCE(P) tests against SPITBOL (oracle) ==="
	@pass=0; fail=0; \
	for t in $(TESTS); do \
	    result=$$($(SPITBOL) $$t.sno 2>/dev/null); \
	    if echo "$$result" | grep -q "^PASS"; then \
	        echo "  OK  $$t"; pass=$$((pass+1)); \
	    else \
	        echo "  FAIL $$t: $$result"; fail=$$((fail+1)); \
	    fi; \
	done; \
	echo "--- $$pass passed, $$fail failed ---"

diff: spitbol csnobol4
	@echo "=== Comparing CSNOBOL4 vs SPITBOL outputs ==="
	@for t in $(TESTS); do \
	    s=$$($(SPITBOL) $$t.sno 2>/dev/null); \
	    c=$$($(SNOBOL4) $$t.sno 2>/dev/null); \
	    if [ "$$s" = "$$c" ]; then \
	        echo "  MATCH  $$t"; \
	    else \
	        echo "  DIFFER $$t"; \
	        echo "    SPITBOL:  $$s"; \
	        echo "    CSNOBOL4: $$c"; \
	    fi; \
	done

clean:
	rm -f *.out
