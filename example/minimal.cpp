// minimal.cpp — the smallest thing that shows the point.
//
// Build it:
//   clang++ -std=c++20 -Iinclude -Wfunction-effects -Werror example/minimal.cpp
//
// Then uncomment the marked line and build again. It stops compiling.
//
// SPDX-License-Identifier: MIT

#include "rt_safety.h"

#include <cstdio>
#include <vector>

// A gain stage. One marker, and everything this function reaches is now under
// contract — including anything a colleague adds to it next year.
static void apply_gain(float* out, const float* in, int frames,
                       RtSmoother& gain) noexcept RT_SAFE {
  for (int i = 0; i < frames; ++i) {
    out[i] = rt_sanitise(in[i] * gain.next());
  }

  // Uncomment this and the build fails:
  //
  //   error: function with 'nonblocking' attribute must not call
  //          non-'nonblocking' function 'std::vector<float>::resize'
  //
  // Not a warning, not a runtime assert that fires on stage. A build failure,
  // at the moment the line is typed.
  //
  // static std::vector<float> scratch;
  // scratch.resize(static_cast<size_t>(frames));
}

int main() {
  RtSmoother gain;
  gain.configure(48000.0, 5.0);   // not RT_SAFE: does the exp() up front
  gain.snap(0.0f);
  gain.set(1.0f);

  float in[8], out[8];
  for (int i = 0; i < 8; ++i) in[i] = 1.0f;

  apply_gain(out, in, 8, gain);

  std::printf("realtime contract enforced by the compiler: %s\n",
              RT_SAFETY_ENFORCED ? "yes" : "no (see rt_guard.h)");
  std::printf("gain ramp:");
  for (int i = 0; i < 8; ++i) std::printf(" %.4f", out[i]);
  std::printf("\n");
  return 0;
}
