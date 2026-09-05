// rt_guard_test.cpp — behaviour of the runtime fallback detector.
//
// The compile-time contract is the real product. This tests the net you get on
// toolchains that cannot enforce it, so that GCC and MSVC users are not simply
// told "sorry".
//
// SPDX-License-Identifier: MIT

#define RT_GUARD_IMPLEMENTATION
#include "rt_guard.h"

#include <cstdio>
#include <vector>

static int g_failures = 0;

// Everything below is only reachable when the guard is compiled in. Under
// NDEBUG the guard is meant to vanish entirely, so these would be unused —
// and a library that ships warnings teaches people to ignore warnings.
#if RT_GUARD_ENABLED

static int  g_caught = 0;
static void counting_handler(const char*) noexcept { ++g_caught; }

static void check(bool cond, const char* what) {
  if (!cond) { std::printf("  FAIL  %s\n", what); ++g_failures; }
  else       { std::printf("  ok    %s\n", what); }
}

// Volatile sink so the optimiser cannot delete the allocations we are testing.
static volatile std::size_t g_sink = 0;

static void allocate_something() {
  std::vector<float> v(128, 1.0f);
  g_sink += v.size();
}

#endif  // RT_GUARD_ENABLED

int main() {
  std::printf("rt_guard_test  (RT_GUARD_ENABLED=%d)\n", RT_GUARD_ENABLED);

#if RT_GUARD_ENABLED
  rt_guard::on_violation = &counting_handler;

  // 1. Allocation outside a realtime scope is ordinary and must not report.
  g_caught = 0;
  allocate_something();
  check(g_caught == 0, "allocation outside a realtime scope is ignored");

  // 2. The same allocation inside a scope is a violation.
  g_caught = 0;
  {
    RtRealtimeScope guard;
    allocate_something();
  }
  check(g_caught > 0, "allocation inside RtRealtimeScope is caught");

  // 3. RtAllowBlocking carves out a knowingly-blocking region.
  g_caught = 0;
  {
    RtRealtimeScope guard;
    {
      RtAllowBlocking allow;
      allocate_something();
    }
  }
  check(g_caught == 0, "RtAllowBlocking suppresses inside a realtime scope");

  // 4. ...and the suppression ends with the inner scope, rather than leaking.
  g_caught = 0;
  {
    RtRealtimeScope guard;
    { RtAllowBlocking allow; allocate_something(); }
    allocate_something();
  }
  check(g_caught > 0, "suppression does not leak past RtAllowBlocking");

  // 5. Manual annotation, for the violations a new-override cannot see —
  //    taking a lock, touching a file, calling into a foreign library.
  g_caught = 0;
  {
    RtRealtimeScope guard;
    RT_GUARD_CHECK("pretend mutex lock");
  }
  check(g_caught == 1, "RT_GUARD_CHECK reports inside a realtime scope");

  g_caught = 0;
  RT_GUARD_CHECK("pretend mutex lock");
  check(g_caught == 0, "RT_GUARD_CHECK is silent outside a realtime scope");

  // 6. Nesting restores the previous state rather than clearing it.
  g_caught = 0;
  {
    RtRealtimeScope outer;
    { RtRealtimeScope inner; }
    allocate_something();          // still inside `outer`
  }
  check(g_caught > 0, "nested scopes restore, not clear");
#else
  std::printf("  guard disabled in this build (NDEBUG) — nothing to test\n");
#endif

  std::printf(g_failures ? "\n%d FAILED\n" : "\nall passed\n", g_failures);
  return g_failures ? 1 : 0;
}
