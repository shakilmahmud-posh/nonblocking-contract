# nonblocking-contract
#
# RT_CONTRACT is not optional and not a lint pass. -Werror on -Wfunction-effects
# means an allocation, a lock, or any I/O reachable from an RT_SAFE function is
# a BUILD FAILURE. That is the whole point: the failure mode it prevents does
# not show up in tests, only in production.

CXX         ?= clang++
STD         ?= -std=c++20
INC          = -Iinclude
RT_CONTRACT  = -Wfunction-effects -Werror
WARN         = -Wall -Wextra
CXXFLAGS    ?= $(STD) -O2 $(WARN) $(INC)

BUILD        = build
CASES        = 1 2 3 4 5 6 7 8 9 10 11
UNAME_S     := $(shell uname -s)

.PHONY: all check verify verify-strict test guard darwin example clean

# Can this toolchain enforce the contract at all? On GCC, MSVC, and older Apple
# clang, RT_SAFE is inert — every violation case then compiles, and a plain
# `verify` would shout "CONTRACT NOT ENFORCED" at someone who never had the
# option. Probe once, up front, and skip honestly instead.
ENFORCED := $(shell $(CXX) $(STD) $(INC) -DRT_SAFETY_SILENCE_WARNING \
              -fsyntax-only test/rt_enforced_probe.cpp >/dev/null 2>&1 \
              && echo 1 || echo 0)

all: check

# The whole gate, in the order that matters: prove rejection first, then prove
# the contract still lets real work through.
check: verify test guard darwin example
	@echo
	@echo "  nonblocking-contract: all checks passed."

# ---------------------------------------------------------------- negative
#
# The one test here that fails by succeeding.
#
# Note what this asserts: not merely that the case failed to build, but that it
# failed WITH THE CONTRACT'S OWN DIAGNOSTIC. A negative test that passes for
# the wrong reason — a typo, a missing header, a most-vexing-parse — reports
# green while proving nothing. Case 6 did exactly that during development.

verify:
ifeq ($(ENFORCED),0)
	@echo "verify — SKIPPED: $(CXX) cannot enforce the contract (RT_SAFE is inert)."
	@echo "         Needs clang 20+ with -Wfunction-effects. rt_guard.h is the"
	@echo "         runtime fallback for this toolchain. Not a failure — but"
	@echo "         nothing here was proven either. CI runs verify-strict."
else
	@echo "verify — the compiler must REJECT every violation:"
	@fail=0; \
	for c in $(CASES); do \
	  out=$$($(CXX) $(STD) $(INC) -DCASE=$$c $(RT_CONTRACT) \
	         -c test/rt_violations.cpp -o /dev/null 2>&1); \
	  if [ $$? -eq 0 ]; then \
	    echo "  CASE $$c: COMPILED — RT CONTRACT NOT ENFORCED"; fail=1; \
	  elif echo "$$out" | grep -q "function with 'nonblocking' attribute"; then \
	    echo "  CASE $$c: correctly rejected"; \
	  else \
	    echo "  CASE $$c: rejected, but NOT by the contract — the test is lying"; \
	    echo "$$out" | grep -m1 'error:' | sed 's/^/         /'; \
	    fail=1; \
	  fi; \
	done; \
	if [ $$fail -eq 0 ]; then echo "  RT contract enforced."; \
	else echo "  RT CONTRACT BROKEN"; exit 1; fi
endif

# The gate CI runs. A skip is a pass for a developer on the wrong compiler; it
# is NOT a pass for the project. This target refuses to be green unless the
# contract was actually exercised.
verify-strict:
	@if [ "$(ENFORCED)" != "1" ]; then \
	  echo "verify-strict: FAILED — $(CXX) cannot enforce the contract."; \
	  echo "               RT_SAFETY_ENFORCED is 0, so nothing would be proven."; \
	  exit 1; \
	fi
	@$(MAKE) --no-print-directory verify

# ---------------------------------------------------------------- positive
#
# Compiled under the identical contract flags. If any helper in the library
# secretly allocates, this target does not build.

test: $(BUILD)/rt_contract_test
	@echo
	@echo "test — the contract must ACCEPT real work:"
	@$(BUILD)/rt_contract_test

$(BUILD)/rt_contract_test: test/rt_contract_test.cpp include/rt_safety.h | $(BUILD)
	@$(CXX) $(CXXFLAGS) $(RT_CONTRACT) -o $@ $<

# ------------------------------------------------------------ runtime guard
#
# Built WITHOUT the contract flags on purpose: this is the fallback path for
# toolchains that have no function effects, and it must work there.

guard: $(BUILD)/rt_guard_test
	@echo
	@echo "guard — runtime fallback for non-clang toolchains:"
	@$(BUILD)/rt_guard_test

$(BUILD)/rt_guard_test: test/rt_guard_test.cpp include/rt_guard.h include/rt_safety.h | $(BUILD)
	@$(CXX) $(STD) -O0 -g $(WARN) $(INC) -o $@ $<

# ------------------------------------------------------------ darwin layer
#
# Exists so the Apple header is never shipped unbuilt. No-ops elsewhere.

ifeq ($(UNAME_S),Darwin)
darwin: $(BUILD)/rt_darwin_test
	@echo
	@echo "darwin — the Apple layer, under contract:"
	@$(BUILD)/rt_darwin_test

$(BUILD)/rt_darwin_test: test/rt_darwin_test.cpp include/rt_safety_darwin.h include/rt_safety.h | $(BUILD)
	@$(CXX) $(CXXFLAGS) $(RT_CONTRACT) -o $@ $<
else
darwin:
	@echo
	@echo "darwin — skipped (not Apple)"
endif

# ---------------------------------------------------------------- example

example: $(BUILD)/minimal
	@echo
	@echo "example:"
	@$(BUILD)/minimal

$(BUILD)/minimal: example/minimal.cpp include/rt_safety.h | $(BUILD)
	@$(CXX) $(CXXFLAGS) $(RT_CONTRACT) -o $@ $<

$(BUILD):
	@mkdir -p $(BUILD)

clean:
	@rm -rf $(BUILD)
