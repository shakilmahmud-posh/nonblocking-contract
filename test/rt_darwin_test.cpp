// rt_darwin_test.cpp — the Apple layer, built under the contract.
//
// Small, but it exists so the Darwin header is never shipped unbuilt. An
// untested header in a repo about enforcement is a bad look.
//
// Skipped entirely on non-Apple platforms by the Makefile.
//
// SPDX-License-Identifier: MIT

#include "rt_safety_darwin.h"

#include <cstdio>

static int g_failures = 0;

static void check(bool cond, const char* what) {
  if (!cond) { std::printf("  FAIL  %s\n", what); ++g_failures; }
  else       { std::printf("  ok    %s\n", what); }
}

// A realtime block using the Darwin clock. Compiled with -Wfunction-effects
// -Werror, so this failing to build means rt_now() has stopped being trusted
// or the trust pragma has broken.
static double elapsed_ns(RtClock& clock, uint64_t since) noexcept RT_SAFE {
  return clock.ns(rt_now() - since);
}

int main() {
  std::printf("rt_darwin_test  (RT_SAFETY_ENFORCED=%d)\n", RT_SAFETY_ENFORCED);

  RtClock clock;
  clock.init();
  check(clock.nsPerTick > 0.0, "timebase initialises to a positive scale");

  const uint64_t t0 = rt_now();
  check(t0 != 0, "rt_now returns a tick count");

  // Burn a little time without allocating or sleeping.
  volatile double sink = 0.0;
  for (int i = 0; i < 200000; ++i) sink += double(i) * 1e-9;
  (void)sink;

  const double dt = elapsed_ns(clock, t0);
  check(dt > 0.0, "elapsed time is positive");
  check(dt < 1e9, "elapsed time is under a second (sanity, not a benchmark)");

  const uint64_t t1 = rt_now();
  check(t1 >= t0, "the clock is monotonic");

  std::printf(g_failures ? "\n%d FAILED\n" : "\nall passed\n", g_failures);
  return g_failures ? 1 : 0;
}
