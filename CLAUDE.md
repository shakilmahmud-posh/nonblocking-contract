# rt-safety

A realtime-safety contract for C++ audio (and any other hard-realtime callback), enforced by the
compiler rather than by discipline. Extracted 2026-09-05 from `StagegigAI/engine-rt`, which stays
closed source — this is the safety harness, not the DSP.

**Purpose:** AI Factory's first public open-source release. Built to be genuinely useful, not to
pad a profile.

## What it is

`RT_SAFE` maps to `[[clang::nonblocking]]` (Clang 20+). Built with `-Wfunction-effects -Werror`,
the compiler refuses to build any allocation, lock, or I/O reachable from a marked function.

The centrepiece is `test/rt_violations.cpp`, **a file that exists to fail to compile**. `make
verify` asserts the compiler rejects every case in it. If that file ever builds cleanly, the
contract has stopped being enforced.

## Layout

- `include/rt_safety.h` — portable core: `RT_SAFE`, denormal flush (ARM + x86), clamp/sanitise,
  one-pole smoother, SPSC ring.
- `include/rt_safety_darwin.h` — Apple only: mach timebase, `os_workgroup` join (a knowingly
  accepted violation, documented at the call site).
- `include/rt_guard.h` — opt-in **runtime** detector for toolchains with no function effects
  (GCC, MSVC). Requires one TU to define `RT_GUARD_IMPLEMENTATION`.
- `test/rt_violations.cpp` — negative tests. Must NOT compile.
- `test/rt_contract_test.cpp` — positive tests. Must compile under the contract and pass.
- `example/minimal.cpp` — smallest useful demo.

## Commands

```bash
make verify    # negative: assert the compiler REJECTS every violation case
make test      # positive: real DSP compiles under the contract and produces right numbers
make example   # build + run the demo
make check     # verify + test + example
```

`make verify` is the gate. It is the only test here that fails by succeeding.

## Status

Local. **Not yet published** — pending Shakil's go-ahead on making the GitHub repo public.

## Licensing

MIT. Verified clean before extraction: no JUCE headers, no JUCE linkage, no JUCE-derived code.
StagegigAI's app layer uses JUCE (GPL-or-commercial), `engine-rt` does not. The `juce_max` /
`juceClamp` helpers in the _parent_ project are self-written one-liners that merely have
misleading names; they were not carried over here.

## Gotchas

- `RT_SAFETY_ENFORCED` is 0 on non-Clang toolchains and RT_SAFE is inert. That is by design and
  it warns. Do not "fix" it by deleting the warning.
- `configure()` on `RtSmoother` calls `exp()` and is deliberately NOT `RT_SAFE`.
- Denormal flush is per-thread, not per-process — it must run on the realtime thread itself.
