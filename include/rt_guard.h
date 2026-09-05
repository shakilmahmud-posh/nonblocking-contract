// rt_guard.h — runtime realtime-safety detector, for toolchains that cannot
// enforce the contract at compile time.
//
// The compile-time contract in rt_safety.h is the real product: it fails the
// build, costs nothing at runtime, and catches violations before they exist.
// It needs Clang 20+. On GCC and MSVC, RT_SAFE is inert.
//
// This header is the fallback for those toolchains. It cannot prove absence of
// violations the way the compiler can — it only catches the ones you actually
// execute. Use it in debug and test builds; it is a net, not a proof.
//
// USAGE — exactly one translation unit must define the implementation macro
// before including this header:
//
//     #define RT_GUARD_IMPLEMENTATION
//     #include "rt_guard.h"
//
// Then wrap the realtime callback:
//
//     void audio_callback(float* out, int n) {
//       RtRealtimeScope guard;          // anything that allocates from here
//       process(out, n);                // until the closing brace is reported
//     }
//
// Enabled only when RT_GUARD_ENABLED is 1 (the default in debug builds). In a
// release build it compiles to nothing at all — no flag check, no branch.
//
// SPDX-License-Identifier: MIT

#pragma once

#include "rt_safety.h"

#ifndef RT_GUARD_ENABLED
  #if defined(NDEBUG)
    #define RT_GUARD_ENABLED 0
  #else
    #define RT_GUARD_ENABLED 1
  #endif
#endif

#if RT_GUARD_ENABLED

#include <cstddef>

namespace rt_guard {

// Set while a realtime scope is active on THIS thread. Thread-local, because
// the main thread allocating is fine and expected — only the audio thread is
// under contract.
extern thread_local bool  in_realtime_scope;
extern thread_local int   violation_count;

// What to do when a violation is seen. The default aborts with a message,
// which is what you want in CI. Tests replace it to count instead.
using ViolationHandler = void (*)(const char* what);
extern ViolationHandler on_violation;

void report(const char* what) noexcept;

}  // namespace rt_guard

// RAII marker for a realtime region.
class RtRealtimeScope {
public:
  RtRealtimeScope() noexcept : previous_(rt_guard::in_realtime_scope) {
    rt_guard::in_realtime_scope = true;
  }
  ~RtRealtimeScope() noexcept { rt_guard::in_realtime_scope = previous_; }

  RtRealtimeScope(const RtRealtimeScope&)            = delete;
  RtRealtimeScope& operator=(const RtRealtimeScope&) = delete;

private:
  bool previous_;
};

// Escape hatch: a region inside a realtime scope that is knowingly allowed to
// allocate — a startup path, a test fixture, a workgroup join.
class RtAllowBlocking {
public:
  RtAllowBlocking() noexcept : previous_(rt_guard::in_realtime_scope) {
    rt_guard::in_realtime_scope = false;
  }
  ~RtAllowBlocking() noexcept { rt_guard::in_realtime_scope = previous_; }

private:
  bool previous_;
};

// Manual annotation, for the things a new-override cannot see: taking a lock,
// touching a file, calling into something you do not trust.
#define RT_GUARD_CHECK(what) ::rt_guard::report(what)

#else  // !RT_GUARD_ENABLED

class RtRealtimeScope { public: RtRealtimeScope() noexcept {} };
class RtAllowBlocking { public: RtAllowBlocking() noexcept {} };
#define RT_GUARD_CHECK(what) ((void)0)

#endif  // RT_GUARD_ENABLED

// ---------------------------------------------------------- implementation

#if defined(RT_GUARD_IMPLEMENTATION) && RT_GUARD_ENABLED

#include <cstdio>
#include <cstdlib>
#include <new>

namespace rt_guard {

thread_local bool in_realtime_scope = false;
thread_local int  violation_count   = 0;

static void default_handler(const char* what) noexcept {
  std::fprintf(stderr,
               "\n*** rt_guard: realtime contract violated: %s\n"
               "*** This ran on a thread inside RtRealtimeScope.\n\n",
               what);
  std::abort();
}

ViolationHandler on_violation = &default_handler;

void report(const char* what) noexcept {
  if (!in_realtime_scope) return;
  ++violation_count;
  // Clear the flag around the handler so the handler's own printf/allocation
  // does not re-enter and recurse forever.
  const bool saved  = in_realtime_scope;
  in_realtime_scope = false;
  if (on_violation) on_violation(what);
  in_realtime_scope = saved;
}

}  // namespace rt_guard

// Global allocation replacement. This is the blunt part: every allocation in
// the process now costs one thread-local read. That is why it is debug-only.

void* operator new(std::size_t n) {
  ::rt_guard::report("operator new");
  void* p = std::malloc(n ? n : 1);
  if (!p) throw std::bad_alloc();
  return p;
}

void* operator new[](std::size_t n) {
  ::rt_guard::report("operator new[]");
  void* p = std::malloc(n ? n : 1);
  if (!p) throw std::bad_alloc();
  return p;
}

void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
  ::rt_guard::report("operator new(nothrow)");
  return std::malloc(n ? n : 1);
}

void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
  ::rt_guard::report("operator new[](nothrow)");
  return std::malloc(n ? n : 1);
}

void operator delete(void* p)                        noexcept { std::free(p); }
void operator delete[](void* p)                      noexcept { std::free(p); }
void operator delete(void* p, std::size_t)           noexcept { std::free(p); }
void operator delete[](void* p, std::size_t)         noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

#endif  // RT_GUARD_IMPLEMENTATION && RT_GUARD_ENABLED
