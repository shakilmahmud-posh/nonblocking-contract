# nonblocking-contract

[![check](https://github.com/shakilmahmud-posh/nonblocking-contract/actions/workflows/check.yml/badge.svg)](https://github.com/shakilmahmud-posh/nonblocking-contract/actions/workflows/check.yml)

**A realtime-safety contract for C++, enforced by the compiler instead of by discipline.**

Mark the functions that run on your audio thread. The compiler then refuses to build any
allocation, lock, or I/O reachable from them.

```cpp
#include "rt_safety.h"

void process(float* out, int n) noexcept RT_SAFE {
  buffer.resize(n);     // error: function with 'nonblocking' attribute
}                       //        must not call non-'nonblocking' function
                        //        'std::vector<float>::resize'
```

Not a warning. Not a runtime assert that fires in front of an audience. A build failure, at the
moment the line is typed.

---

## The test that has to fail

The centrepiece of this repo is [`test/rt_violations.cpp`](test/rt_violations.cpp), **a file whose
job is to not compile.**

It contains eleven ordinary-looking functions — resizing a vector, taking a lock, calling `printf`,
constructing a `std::string`, spawning a thread, calling a helper someone else wrote. Every one of
them builds cleanly and passes unit tests in a normal project. `make verify` asserts that the
compiler rejects all eleven:

```
$ make verify
verify — the compiler must REJECT every violation:
  CASE 1: correctly rejected
  ...
  CASE 11: correctly rejected
  RT contract enforced.
```

If that file ever builds, the contract has silently stopped being enforced, and the next regression
will only be discovered by an audience.

**`make verify` checks the reason, not just the failure.** A negative test that fails for the wrong
reason — a typo, a missing header, a most-vexing-parse — reports green while proving nothing. Case
6 did exactly that while this repo was being written: it was "correctly rejected" by
`error: parentheses were disambiguated as a function declaration`, and the contract was never
exercised at all. So the check now greps for the contract's own diagnostic and fails the build if
the rejection came from anywhere else.

## Why this problem is worth a library

An allocation on the audio thread is not a crash. It is a stall — the allocator takes a lock, the
lock is held by a thread the scheduler has descheduled, and your callback misses its deadline. The
buffer goes out half-filled and the listener hears a click.

That failure:

- **passes every test you own.** The code is correct. It just isn't fast enough, sometimes.
- **is load-dependent.** It appears when the heap is fragmented, which is after hours of running.
- **is unreproducible on the machine that reports it.** It happens on stage, once, and not in your
  studio.

The usual defences are code review and a written rule that everyone forgets. This makes it the
compiler's job.

## Quickstart

```bash
git clone https://github.com/shakilmahmud-posh/nonblocking-contract
cd nonblocking-contract
make check          # verify + test + guard + darwin + example
make verify-strict  # same, but FAILS if the toolchain cannot enforce (this is what CI runs)
```

Then copy `include/rt_safety.h` into your project — it is a single header with no dependencies —
and add the two flags:

```
-Wfunction-effects -Werror
```

## What's in it

| File                         |                                                                                                                                            |
| ---------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------ |
| `include/rt_safety.h`        | Portable core. `RT_SAFE`, denormal flush (ARM + x86), `rt_clamp`, `rt_sanitise`, `RtSmoother`, `RtRing` (lock-free SPSC). No dependencies. |
| `include/rt_safety_darwin.h` | Apple only. mach timebase, `os_workgroup` join.                                                                                            |
| `include/rt_guard.h`         | Opt-in **runtime** detector for toolchains with no function effects.                                                                       |
| `test/rt_violations.cpp`     | The negative half. Must not compile.                                                                                                       |
| `test/rt_contract_test.cpp`  | The positive half. Must compile _under the contract_ and produce right numbers.                                                            |
| `test/rt_darwin_test.cpp`    | Builds the Apple layer so it is never shipped unbuilt. Skipped off Darwin.                                                                 |

The positive half matters as much as the negative one. A contract that rejects everything would
pass `make verify`. `rt_contract_test.cpp` is built with the identical `-Werror` flags, so if any
helper in this library secretly allocates, it does not build.

## Requirements, honestly

|                |                                                                                                                                                                                              |
| -------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Clang 20+**  | Full enforcement. `[[clang::nonblocking]]` is a function effect: it propagates through the call graph, so violations are caught at the call site rather than by a human following the chain. |
| **GCC / MSVC** | `RT_SAFE` expands to nothing. You get a `#warning`, and `RT_SAFETY_ENFORCED` is `0`. Use `rt_guard.h` for a runtime net.                                                                     |

**Verified in CI on every push** ([workflow](.github/workflows/check.yml)):

| Toolchain | Result |
| --- | --- |
| clang 20 (upstream, Linux) | `verify-strict` green — all 11 cases rejected with the contract's own diagnostic |
| Apple clang 21.0.0 (macOS) | full `make check` green, including the Darwin layer |
| GCC 13.3.0 (Linux) | `RT_SAFE` inert: `verify` skips, library builds and all assertions pass, and `verify-strict` correctly refuses to go green |

The GCC job is deliberately adversarial about its own result — it asserts that `verify-strict`
**fails** there. A library that reported success on a toolchain where it enforces nothing would be
worse than useless.

**Still unverified: MSVC.** The inert path is exercised by the GCC job and by
`RT_SAFETY_FORCE_INERT`, so the code shape is known to work when `RT_SAFE` does nothing, but no
MSVC build has ever been run against this. If you try it, an issue saying what happened would be
genuinely useful.

## Prior art

[`JanosGit/RealtimeSafetyCheckHelpers`](https://github.com/JanosGit/RealtimeSafetyCheckHelpers) is
the closest thing that exists — helper tools that detect realtime-critical system calls in DSP code
or third-party libraries. It works at **runtime**, catching what you execute, and it has been
dormant since 2019.

This library is the compile-time half of the same problem. The two are complementary rather than
competing: `rt_guard.h` here does roughly what that project does, and exists precisely for the
toolchains where the compile-time contract is unavailable.

If you know of other prior art, an issue pointing at it is welcome — I would rather cite it than
pretend this space is emptier than it is.

## Testing the unenforced path

Most people cannot install every toolchain their users have. `RT_SAFETY_FORCE_INERT` compiles the
unenforced path on a compiler that could have enforced it, so you can check your project still
builds and behaves when `RT_SAFE` does nothing:

```bash
make check CXX="clang++ -DRT_SAFETY_FORCE_INERT -DRT_SAFETY_SILENCE_WARNING"
```

`make verify` then reports SKIPPED rather than failing — a developer on the wrong compiler has not
broken anything. `make verify-strict` still fails, because a skip is a pass for that developer and
is **not** a pass for the project. CI runs the strict one.

## The one deliberate hole

`rt_safety_darwin.h` contains exactly one knowing violation, and it is documented at the call site
rather than buried:

```cpp
// os_workgroup_join: this one IS a real violation, and we take it knowingly.
//
// Joining the device's realtime workgroup can allocate and lock on its first
// call, and it must be called FROM the thread being admitted — so there is no
// way to do it off the audio thread. It happens once, at stream start, before
// any audio the audience hears. The alternative — not joining — costs sporadic
// dropouts for the life of the process.
```

Every serious macOS audio engine makes that trade. Most of them don't say so anywhere. If your
project needs its own exception, `RT_SAFE_TRUST_BEGIN` / `RT_SAFE_TRUST_END` exists for it — and
the point of the macro pair is that exceptions are _visible_ and countable, not that they are
forbidden.

## Where it came from

Extracted from the realtime engine of StagegigAI, a live-performance rig, where the cost of a
dropout is measured in people watching. The engine itself stays closed; this is
the safety harness, which is more useful to everyone else than the DSP is.

Applicable well beyond audio — game audio callbacks, robotics control loops, embedded ISRs, any
deadline you cannot miss.

## Maintenance

Best-effort, and I would rather say so than imply otherwise. Issues are welcome and I read them;
responses are not guaranteed and may be slow. This is extracted from a product I work on, not a
project I staff. Fork freely — that is what the licence is for.

## Licence

MIT. See [LICENSE](LICENSE).

Contains no JUCE code and no JUCE-derived code — verified before extraction.
