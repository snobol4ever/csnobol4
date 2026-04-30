# FENCE 48-test suite — extends fence_function/ with broader coverage.
# Tests live in corpus/crosscheck/patterns/100..147.

SNOBOL4  ?= ../../snobol4
SPITBOL  ?= /home/claude/x64/bin/sbl
CORPUS   ?= /home/claude/corpus/crosscheck/patterns

# Tier A: baseline FENCE (no *var, no ARBNO)
TIER_A = 100_pat_fence_two_alts_first \
         101_pat_fence_falls_through \
         102_pat_fence_complete_fail \
         103_pat_fence_in_concat \
         104_pat_fence_seals_back_in_concat \
         105_pat_fence_empty \
         106_pat_fence_with_capture \
         107_pat_fence_nested

# Tier B: *var indirection
TIER_B = 108_pat_fence_via_var_basic \
         109_pat_fence_via_var_seal_blocks_retry \
         110_pat_fence_via_var_then_extra \
         111_pat_fence_via_var_falls_to_outer \
         112_pat_fence_via_var_two_in_concat \
         113_pat_fence_via_var_two_with_seal_retry \
         114_pat_fence_via_var_in_paren_alt \
         115_pat_fence_via_var_recursive

# Tier C: ARBNO + FENCE
TIER_C = 116_pat_arbno_of_fence_inline \
         117_pat_arbno_of_star_var_fence \
         118_pat_arbno_of_star_var_fence_seal_blocks \
         119_pat_arbno_of_fence_via_var_via_outer

# Tier D: real grammars (calc, regex, JSON)
TIER_D = 120_pat_calc_add \
         121_pat_calc_op_dispatch \
         122_pat_calc_seal_prefix \
         123_pat_regex_alt_class \
         124_pat_regex_keyword_seal \
         125_pat_json_literal \
         126_pat_json_number \
         127_pat_json_keyvalue

# Tier E: beauty-class deeply-nested
TIER_E = 128_pat_recursive_grammar_right_rec \
         129_pat_arbno_star_var_fence_with_alts \
         130_pat_two_star_fence_concat_outer \
         131_pat_boolean_expr_grammar

# Tier F: depth-recursion stress (session #55) — added to demarcate the
# 119/129/130 bug class from generic-recursion bugs. ALL 16 pass on csnobol4
# baseline at HEAD 6d08540. They constitute a regression-prevention floor:
# any fix to the FENCE leak class must keep these green.
TIER_F = 132_pat_fence_eps_recur_shallow \
         133_pat_fence_eps_recur_deep \
         134_pat_fence_eps_recur_stress \
         135_pat_balanced_parens_shallow \
         136_pat_balanced_parens_deep \
         137_pat_balanced_mixed \
         138_pat_calc_paren_expr \
         139_pat_calc_paren_deep \
         140_pat_eval_double_fn_trick \
         141_pat_eval_double_fn_arbno \
         142_pat_arbno_fence_arbno \
         143_pat_regex_quantified_class \
         144_pat_json_nested_array \
         145_pat_left_assoc_via_arbno_fence \
         146_pat_fence_alt_with_capture \
         147_pat_fence_through_unevaluated

TESTS = $(TIER_A) $(TIER_B) $(TIER_C) $(TIER_D) $(TIER_E) $(TIER_F)

.PHONY: all csnobol4 spitbol clean

all: csnobol4

csnobol4:
	@echo "=== FENCE 32-test suite — CSNOBOL4 ==="
	@pass=0; fail=0; crash=0; \
	for t in $(TESTS); do \
	    actual=$$(timeout 5 $(SNOBOL4) -bf $(CORPUS)/$$t.sno 2>&1); \
	    expected=$$(cat $(CORPUS)/$$t.ref); \
	    if [ "$$actual" = "$$expected" ]; then \
	        echo "  OK    $$t"; pass=$$((pass+1)); \
	    elif echo "$$actual" | grep -q "Caught signal"; then \
	        echo "  CRASH $$t"; crash=$$((crash+1)); \
	    else \
	        echo "  FAIL  $$t"; fail=$$((fail+1)); \
	        echo "    expected: $$expected"; \
	        echo "    actual:   $$actual"; \
	    fi; \
	done; \
	echo "--- $$pass OK, $$fail FAIL, $$crash CRASH (of 48) ---"

spitbol:
	@echo "=== FENCE 32-test suite — SPITBOL oracle ==="
	@pass=0; fail=0; \
	for t in $(TESTS); do \
	    actual=$$(timeout 5 $(SPITBOL) -bf $(CORPUS)/$$t.sno 2>&1); \
	    expected=$$(cat $(CORPUS)/$$t.ref); \
	    if [ "$$actual" = "$$expected" ]; then \
	        echo "  OK    $$t"; pass=$$((pass+1)); \
	    else \
	        echo "  FAIL  $$t"; fail=$$((fail+1)); \
	        echo "    expected: $$expected"; \
	        echo "    actual:   $$actual"; \
	    fi; \
	done; \
	echo "--- $$pass OK, $$fail FAIL (of 48) ---"

clean:
