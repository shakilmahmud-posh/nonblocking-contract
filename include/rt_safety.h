// rt_safety.h — the realtime contract, enforced by the compiler.
//
// Mark every function that can run on a realtime thread RT_SAFE. Build with
// -Wfunction-effects -Werror and clang then REFUSES to compile any allocation,
// lock, or I/O reachable from it. This is not a convention someone has to
// remember; it is a build failure when they forget.
//
//   void process(float* out, int n) noexcept RT_SAFE {
//     buffer.resize(n);            // error: function with 'nonblocking' effect
//   }                              //        must not allocate
//
// The failure mode this prevents does not show up in unit tests. An allocation
// in the audio callback passes every test you own and then drops samples on
// stage, in front of an audience, once. That asymmetry is why this is a build
// failure and not a lint warning.
//
// See test/rt_violations.cpp, which exists to FAIL to compile.
//
// Portable core. Darwin-specific helpers live in rt_safety_darwin.h. For
// toolchains with no compile-time enforcement, rt_guard.h adds an opt-in
// runtime detector.
//
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <cmath>

// ---------------------------------------------------------------- the marker
//
// [[clang::nonblocking]] landed in Clang 20. It is a function effect: the
// compiler propagates it through the call graph and diagnoses anything that
// can block — allocation, locks, I/O, exceptions, indirect calls it cannot
// prove safe.
//
// On every other toolchain RT_SAFE expands to nothing and the contract is
// unenforced. That is not a silent downgrade: you get a #warning, and
// RT_SAFETY_ENFORCED tells you at compile time which world you are in.

// RT_SAFETY_FORCE_INERT lets you compile the unenforced path deliberately, on a
// compiler that could have enforced it. That matters because most people
// writing this code cannot install every toolchain their users have: it is how
// you check that your project still builds and behaves when RT_SAFE does
// nothing, without owning a GCC box.
#if defined(__clang__) && defined(__has_cpp_attribute) && !defined(RT_SAFETY_FORCE_INERT)
  #if __has_cpp_attribute(clang::nonblocking)
    #define RT_SAFE [[clang::nonblocking]]
    #define RT_SAFETY_ENFORCED 1
  #endif
#endif

#ifndef RT_SAFE
  #define RT_SAFE
  #define RT_SAFETY_ENFORCED 0
  #ifndef RT_SAFETY_SILENCE_WARNING
    #warning "rt_safety: this compiler cannot enforce the realtime contract - RT_SAFE is inert. Use clang 20+ with -Wfunction-effects, or see rt_guard.h for a runtime check. Define RT_SAFETY_SILENCE_WARNING to quiet this."
  #endif
#endif

// RT_SAFE_TRUST — for the rare call the compiler cannot prove safe but you
// have verified is. Every use is a hole in the contract, so each one should
// carry its reason on the same line. Keep the list short enough to read in one
// sitting; the moment it needs scrolling, the contract has stopped meaning
// anything.
//
//   RT_SAFE_TRUST_BEGIN
//   inline uint64_t now() noexcept RT_SAFE { return platform_tick(); }  // no syscall
//   RT_SAFE_TRUST_END

#if RT_SAFETY_ENFORCED
  #define RT_SAFE_TRUST_BEGIN \
    _Pragma("clang diagnostic push") \
    _Pragma("clang diagnostic ignored \"-Wfunction-effects\"")
  #define RT_SAFE_TRUST_END _Pragma("clang diagnostic pop")
#else
  #define RT_SAFE_TRUST_BEGIN
  #define RT_SAFE_TRUST_END
#endif

// --------------------------------------------------------------- denormals
//
// A denormal float can cost 100x a normal one on some paths. A reverb tail or
// a filter decaying toward silence generates them by the thousand, so a chain
// that measured fine can blow its deadline minutes later, as things fade out.
// The bug reproduces only after the music stops.
//
// Flush-to-zero once per realtime thread, at the top of the callback. The
// setting is per-thread, not per-process, so it has to happen on the thread
// that will do the work.

inline void rt_flush_denormals() noexcept RT_SAFE {
#if defined(__aarch64__) && !defined(_MSC_VER)
  // FPCR bit 24 (FZ) — flush denormal results to zero.
  uint64_t fpcr;
  __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
  fpcr |= (1ULL << 24);
  __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr));

#elif (defined(__x86_64__) || defined(__i386__)) && !defined(_MSC_VER)
  // MXCSR bit 15 (FTZ) flushes denormal results; bit 6 (DAZ) treats denormal
  // inputs as zero. You want both — FTZ alone still pays the penalty when a
  // denormal arrives from elsewhere in the graph.
  //
  // Written with raw stmxcsr/ldmxcsr rather than <xmmintrin.h>'s _MM_SET_*
  // macros so this header pulls in no platform headers of its own.
  unsigned int csr;
  __asm__ __volatile__("stmxcsr %0" : "=m"(csr));
  csr |= (1u << 15) | (1u << 6);
  __asm__ __volatile__("ldmxcsr %0" : : "m"(csr));
#endif
  // Any other target: nothing to do, and nothing to apologise for.
}

// ------------------------------------------------------------ small helpers

inline float rt_clamp(float v, float lo, float hi) noexcept RT_SAFE {
  return v < lo ? lo : (v > hi ? hi : v);
}

// Guards against NaN/Inf reaching the converters. A single NaN written to the
// output buffer can latch a hardware mute on some interfaces until the device
// is re-plugged — a failure that outlives the process that caused it.
//
// The comparison is deliberately written so NaN takes the false branch.
inline float rt_sanitise(float v) noexcept RT_SAFE {
  return (v >= -4.0f && v <= 4.0f) ? v : 0.0f;
}

// One-pole parameter smoother. Every control change ramps rather than steps,
// because a discontinuity in a gain is an audible click.
//
// configure() calls exp() and is deliberately NOT RT_SAFE: work out the
// coefficient when the sample rate is known, then touch only set()/next() on
// the realtime thread.
class RtSmoother {
public:
  void configure(double sampleRate, double milliseconds) noexcept {
    const double n = (milliseconds * 0.001) * sampleRate;
    coeff_ = (n <= 1.0) ? 1.0f : float(1.0 - std::exp(-1.0 / n));
  }
  void  snap(float v)   noexcept RT_SAFE { cur_ = target_ = v; }
  void  set(float v)    noexcept RT_SAFE { target_ = v; }
  float next()          noexcept RT_SAFE { cur_ += coeff_ * (target_ - cur_); return cur_; }
  float current() const noexcept RT_SAFE { return cur_; }

private:
  float cur_ = 0.0f, target_ = 0.0f, coeff_ = 1.0f;
};

// ---------------------------------------------------------- lock-free ring
//
// Single-producer, single-consumer. This is the structure you need before you
// can honestly claim nothing on the realtime thread blocks: parameter changes,
// meter readings and MIDI all have to cross the thread boundary somehow, and
// this is the boundary that does not take a lock.
//
// Capacity is a power of two so the wrap is a mask, not a modulo. T must be
// trivially copyable — a type with a destructor can allocate, and it has no
// business crossing this boundary.
//
// Single-producer, single-consumer is not a detail you can bend later. Two
// producers on this ring corrupts it silently, and the corruption is audible
// long before it is diagnosable.

template <typename T, unsigned Capacity>
class RtRing {
  static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                "RtRing capacity must be a power of two");

public:
  // Producer thread only.
  bool push(const T& v) noexcept RT_SAFE {
    const unsigned w    = write_;
    const unsigned next = (w + 1) & kMask;
    if (next == __atomic_load_n(&read_, __ATOMIC_ACQUIRE)) return false;  // full
    buf_[w] = v;
    __atomic_store_n(&write_, next, __ATOMIC_RELEASE);
    return true;
  }

  // Consumer thread only.
  bool pop(T& out) noexcept RT_SAFE {
    const unsigned r = read_;
    if (r == __atomic_load_n(&write_, __ATOMIC_ACQUIRE)) return false;    // empty
    out = buf_[r];
    __atomic_store_n(&read_, (r + 1) & kMask, __ATOMIC_RELEASE);
    return true;
  }

  bool empty() const noexcept RT_SAFE {
    return __atomic_load_n(&read_,  __ATOMIC_ACQUIRE) ==
           __atomic_load_n(&write_, __ATOMIC_ACQUIRE);
  }

private:
  static constexpr unsigned kMask = Capacity - 1;
  T        buf_[Capacity]{};
  unsigned write_ = 0;
  unsigned read_  = 0;
};
