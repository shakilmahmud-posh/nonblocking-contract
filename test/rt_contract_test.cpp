// rt_contract_test.cpp — the positive half.
//
// rt_violations.cpp proves the contract REJECTS unsafe code. That is only half
// a proof: a contract that rejects everything would pass it. This file proves
// the contract ACCEPTS real work — it is compiled under the identical
// -Wfunction-effects -Werror flags, so if any helper in the library secretly
// allocates, this does not build.
//
// It then checks the numbers, because RT-safe and correct are different
// properties and shipping one without the other is not interesting.
//
// SPDX-License-Identifier: MIT

#include "rt_safety.h"

#include <cmath>
#include <cstdio>
#include <limits>

static int g_failures = 0;

// Deliberately NOT RT_SAFE: printf allocates and locks. Assertions run on the
// test thread, never inside a block marked RT_SAFE.
static void check(bool cond, const char* what) {
  if (!cond) {
    std::printf("  FAIL  %s\n", what);
    ++g_failures;
  } else {
    std::printf("  ok    %s\n", what);
  }
}

static bool near(float a, float b, float eps = 1e-5f) {
  return std::fabs(a - b) <= eps;
}

// ---------------------------------------------------------------------------
// A realistic audio callback. Everything it touches must be RT_SAFE or this
// translation unit does not compile.

struct Voice {
  RtSmoother gain;
  float      phase = 0.0f;

  void prepare(double sampleRate) {         // not RT_SAFE — calls exp()
    gain.configure(sampleRate, 10.0);
    gain.snap(0.0f);
  }

  void render(float* out, int frames, float inc) noexcept RT_SAFE {
    for (int i = 0; i < frames; ++i) {
      phase += inc;
      if (phase > 1.0f) phase -= 1.0f;
      const float raw = (phase * 2.0f) - 1.0f;          // naive saw
      out[i] = rt_sanitise(rt_clamp(raw * gain.next(), -1.0f, 1.0f));
    }
  }
};

// The whole callback, under contract. This is the shape the library exists to
// protect: one function, marked once, and everything it reaches inherits it.
static void audio_callback(Voice& v, float* out, int frames,
                           RtRing<float, 8>& params) noexcept RT_SAFE {
  rt_flush_denormals();

  float target;
  while (params.pop(target)) v.gain.set(target);        // drain control changes

  v.render(out, frames, 0.01f);
}

// ---------------------------------------------------------------------------

int main() {
  std::printf("rt_contract_test  (RT_SAFETY_ENFORCED=%d)\n", RT_SAFETY_ENFORCED);

  // --- rt_clamp
  check(near(rt_clamp(0.5f, -1.0f, 1.0f), 0.5f),  "clamp passes in-range");
  check(near(rt_clamp(2.0f, -1.0f, 1.0f), 1.0f),  "clamp limits high");
  check(near(rt_clamp(-2.0f, -1.0f, 1.0f), -1.0f),"clamp limits low");

  // --- rt_sanitise. The NaN case is the reason this function exists: one NaN
  // reaching the converters can latch a hardware mute until re-plug.
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();
  check(near(rt_sanitise(0.5f), 0.5f), "sanitise passes normal audio");
  check(rt_sanitise(nan) == 0.0f,      "sanitise kills NaN");
  check(rt_sanitise(inf) == 0.0f,      "sanitise kills +Inf");
  check(rt_sanitise(-inf) == 0.0f,     "sanitise kills -Inf");

  // --- RtSmoother. 10ms at 48k is 480 samples; after five time constants it
  // should have settled to better than 1%.
  RtSmoother s;
  s.configure(48000.0, 10.0);
  s.snap(0.0f);
  check(near(s.current(), 0.0f), "smoother snaps to value");
  s.set(1.0f);
  const float firstStep = s.next();
  check(firstStep > 0.0f && firstStep < 0.01f, "smoother ramps, does not step");
  for (int i = 0; i < 480 * 5; ++i) s.next();
  check(s.current() > 0.99f, "smoother settles after 5 time constants");

  // --- RtRing. Capacity 4 holds THREE items: one slot is always kept empty so
  // full and empty are distinguishable without a separate count.
  RtRing<int, 4> ring;
  check(ring.empty(), "ring starts empty");
  check(ring.push(1) && ring.push(2) && ring.push(3), "ring accepts capacity-1");
  check(!ring.push(4), "ring refuses when full (one slot reserved)");
  int v = 0;
  check(ring.pop(v) && v == 1, "ring pops in order (1)");
  check(ring.pop(v) && v == 2, "ring pops in order (2)");
  check(ring.pop(v) && v == 3, "ring pops in order (3)");
  check(!ring.pop(v), "ring refuses to pop when empty");
  check(ring.empty(), "ring is empty again");

  // --- the callback itself, compiled under the contract
  Voice voice;
  voice.prepare(48000.0);
  RtRing<float, 8> params;
  params.push(1.0f);

  float block[64] = {};
  audio_callback(voice, block, 64, params);

  bool finite = true, inRange = true;
  for (float x : block) {
    if (!std::isfinite(x))        finite  = false;
    if (x < -1.0f || x > 1.0f)    inRange = false;
  }
  check(finite,  "callback output is all finite");
  check(inRange, "callback output stays in [-1, 1]");

  std::printf(g_failures ? "\n%d FAILED\n" : "\nall passed\n", g_failures);
  return g_failures ? 1 : 0;
}
