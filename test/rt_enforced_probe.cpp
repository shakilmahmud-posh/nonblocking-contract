// rt_enforced_probe.cpp — a capability probe, not a test.
//
// The Makefile compiles this with -fsyntax-only to decide whether this
// toolchain can enforce the contract at all. If it cannot, `make verify` skips
// loudly instead of failing confusingly: on a compiler where RT_SAFE is inert,
// every violation case compiles, and a plain `verify` would report
// "RT CONTRACT NOT ENFORCED" at someone who never had the option.
//
// `make verify-strict` is the one that fails hard. That is what CI runs.
//
// SPDX-License-Identifier: MIT

#include "rt_safety.h"

#if !RT_SAFETY_ENFORCED
  #error "this toolchain cannot enforce the realtime contract"
#endif

int main() { return 0; }
