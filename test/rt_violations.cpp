// rt_violations.cpp — this file EXISTS TO FAIL TO COMPILE.
//
// Every function below is a real way to drop samples in front of an audience,
// and every one of them compiles fine and passes unit tests in an ordinary
// build. `make verify` asserts that the compiler rejects each one.
//
// If this file ever builds cleanly, the realtime contract has stopped being
// enforced and the next silent regression will only be heard on stage.
//
// Build one case at a time:
//   clang++ -std=c++20 -Iinclude -DCASE=n -Wfunction-effects -Werror \
//           -c test/rt_violations.cpp
//
// SPDX-License-Identifier: MIT

#include "rt_safety.h"

#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

static std::vector<float> g_buf;
static std::mutex         g_mutex;

#if CASE == 1
// Allocation. The classic. Grows a buffer inside the callback.
void process(float* out, int n) noexcept RT_SAFE {
  g_buf.resize(size_t(n));
  for (int i = 0; i < n; ++i) out[i] = g_buf[size_t(i)];
}

#elif CASE == 2
// Lock. Waits on a thread that may be descheduled. Unbounded stall.
void process(float* out, int n) noexcept RT_SAFE {
  std::lock_guard<std::mutex> lk(g_mutex);
  for (int i = 0; i < n; ++i) out[i] = 0.0f;
}

#elif CASE == 3
// I/O. A printf can take a lock inside libc and can hit the filesystem.
void process(float* out, int n) noexcept RT_SAFE {
  printf("processing %d frames\n", n);
  for (int i = 0; i < n; ++i) out[i] = 0.0f;
}

#elif CASE == 4
// Hidden allocation behind an innocent-looking type. Nobody reads this line
// and thinks "heap".
void process(float* out, int n) noexcept RT_SAFE {
  std::string tag = "block";
  out[0] = float(tag.size() + size_t(n));
}

#elif CASE == 5
// new/delete directly.
void process(float* out, int n) noexcept RT_SAFE {
  float* tmp = new float[size_t(n)];
  for (int i = 0; i < n; ++i) out[i] = tmp[i];
  delete[] tmp;
}

#elif CASE == 6
// Calling a function that is not itself under contract.
//
// This is the case that matters most, and the one a code review will miss.
// `helper` is perfectly ordinary code written by someone who never heard of
// the audio thread. The effect propagates through the call graph, so the
// compiler catches it at the CALL SITE rather than waiting for a human to
// follow the chain.
static void helper(float* out, int n) {
  // static_cast, not size_t(n) — the latter is a most-vexing-parse and would
  // make this case fail to compile for a reason that has nothing to do with
  // the realtime contract. A negative test that passes for the wrong reason
  // is worse than no test: it reports green while proving nothing.
  std::vector<float> scratch(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) out[i] = scratch[static_cast<size_t>(i)];
}

void process(float* out, int n) noexcept RT_SAFE { helper(out, n); }

#elif CASE == 7
// Throwing. Unwinding allocates, and the tables are not in cache.
void process(float* out, int n) noexcept(false) RT_SAFE {
  if (n <= 0) throw std::runtime_error("empty block");
  for (int i = 0; i < n; ++i) out[i] = 0.0f;
}

#elif CASE == 8
// std::function. Type-erasure allocates whenever the callable does not fit in
// the small-object buffer — so this is a violation that depends on how big the
// lambda's captures are, which is exactly the kind of thing that changes under
// you months later.
void process(float* out, int n) noexcept RT_SAFE {
  std::function<float(int)> f = [out, n](int i) { return out[i] * float(n); };
  for (int i = 0; i < n; ++i) out[i] = f(i);
}

#elif CASE == 9
// shared_ptr. The control block is a heap allocation even when the object is
// not.
void process(float* out, int n) noexcept RT_SAFE {
  auto p = std::make_shared<float>(1.0f);
  for (int i = 0; i < n; ++i) out[i] = *p;
}

#elif CASE == 10
// Spawning a thread from the audio callback. Rarer, but it happens — usually
// as "just kick off the file load from here".
void process(float* out, int n) noexcept RT_SAFE {
  std::thread t([] {});
  t.join();
  for (int i = 0; i < n; ++i) out[i] = 0.0f;
}

#elif CASE == 11
// An indirect call the compiler cannot see through. Not provably safe is
// treated as not safe — which is the correct default when the alternative is
// a dropout.
void process(float* out, int n, void (*fn)(float*, int)) noexcept RT_SAFE {
  fn(out, n);
}

#else
  #error "rt_violations.cpp: define CASE=1..11"
#endif

int main() { return 0; }
