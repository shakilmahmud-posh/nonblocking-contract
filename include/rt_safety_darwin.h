// rt_safety_darwin.h — Apple-specific realtime helpers.
//
// Kept out of rt_safety.h so the portable core compiles unchanged on Windows
// and Linux. Include this only from the macOS/iOS device layer.
//
// SPDX-License-Identifier: MIT

#pragma once

#if !defined(__APPLE__)
  #error "rt_safety_darwin.h is Apple-only. The portable core is rt_safety.h."
#endif

#include "rt_safety.h"

#include <mach/mach_time.h>
#include <os/workgroup.h>

// ------------------------------------------------- trusted system primitives
//
// Everything between TRUST_BEGIN and TRUST_END is a call the compiler cannot
// prove safe but which we assert is safe, with the reason. This is the ONLY
// place in the library where the realtime contract is taken on trust rather
// than proven, and it is deliberately two functions long.

RT_SAFE_TRUST_BEGIN

// mach_absolute_time: reads the Darwin commpage timebase. No syscall, no lock.
inline uint64_t rt_now() noexcept RT_SAFE { return mach_absolute_time(); }

// os_workgroup_join: this one IS a real violation, and we take it knowingly.
//
// Joining the device's realtime workgroup can allocate and lock on its first
// call, and it must be called FROM the thread being admitted — so there is no
// way to do it off the audio thread.
//
// Why that is acceptable: it happens exactly once, at stream start, before any
// audio the audience hears. The alternative — not joining — costs sporadic
// dropouts for the entire life of the process, because the scheduler never
// learns this thread has a deadline. One slow callback at startup in exchange
// for correct scheduling forever is a trade worth making.
//
// But it IS a trade, which is why it is written down here rather than buried
// at the call site. Every serious macOS audio engine makes the same one; most
// of them do not say so anywhere.
inline int rt_join_workgroup(os_workgroup_t wg,
                             os_workgroup_join_token_s* token) noexcept RT_SAFE {
  return os_workgroup_join(wg, token);
}

RT_SAFE_TRUST_END

// Timebase conversion is set up ONCE off the realtime thread, then only read.
// mach_timebase_info() itself is not realtime-safe, which is exactly why init()
// is separate from ns() and is not marked RT_SAFE.
struct RtClock {
  double nsPerTick = 1.0;

  void init() noexcept {                        // main thread only
    mach_timebase_info_data_t tb{};
    mach_timebase_info(&tb);
    nsPerTick = double(tb.numer) / double(tb.denom);
  }

  double ns(uint64_t ticks) const noexcept RT_SAFE {
    return double(ticks) * nsPerTick;
  }
};
