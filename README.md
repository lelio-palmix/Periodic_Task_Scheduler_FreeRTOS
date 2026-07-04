# Periodic Task Layer (PTL) for FreeRTOS

> **EOS 2025 - Project 2 · Group 4**
> A priority-based scheduler for periodic tasks built on top of FreeRTOS.

FreeRTOS schedules tasks by priority but has no native notion of **periodicity** or
**deadlines**. This project adds a thin **Periodic Task Layer (PTL)** that lets users
declare periodic tasks with a *period* and *deadline*, releases each job at the correct
time, detects **deadline misses** and **period overruns**, and reacts to overruns with a
configurable policy - all while leaving FreeRTOS's preemptive, priority-based scheduling
completely untouched.

The layer is intentionally non-intrusive: the FreeRTOS kernel is used as-is, and the
periodic behaviour is implemented entirely in user space through the FreeRTOS public API
(tasks, queues, and the tick hook). This keeps the PTL easy to port across FreeRTOS ports.

---

## Table of contents

- [Features](#features)
- [Repository layout](#repository-layout)
- [How it works](#how-it-works)
- [Timing model](#timing-model)
- [Configuration interface](#configuration-interface)
- [Overrun policies](#overrun-policies)
- [Trace &amp; monitoring output](#trace--monitoring-output)
- [Building &amp; running](#building--running)
- [Compile-time options](#compile-time-options)
- [Testing &amp; regression suite](#testing--regression-suite)

---

## Features

- **First-class periodic tasks** - declare `(name, entry, arg, stack, priority, period, deadline, offset)` and the PTL handles the rest.
- **Write only the job body** - the user provides just the code that runs *once per release*; the PTL wraps it in the release loop, so there are no hand-written `for(;;)` loops or delays.
- **Unmodified FreeRTOS semantics** - preemptive, priority-based scheduling with round-robin time-slicing among equal-priority tasks is preserved.
- **Tick-precise deadline &amp; overrun detection** - checks run inside the FreeRTOS tick hook (ISR context) for 1-tick resolution.
- **Configurable overrun policy** - `SKIP`, `KILL`, or `CATCH_UP`, applied globally to all tasks.
- **Phasing / offsets** - optional per-task start offset; by default all tasks start together at *t₀=0*.
- **Automated, declarative test suite** - 15 scenarios described in JSON, with schedulability analysis (EDF / RM / DM) and pass/fail regression checks run under QEMU.

## Repository layout

| Path | Description |
|------|-------------|
| [API/ptl.h](API/ptl.h) | PTL public API: configuration structures, policy/event enums, function prototypes. |
| [API/ptl.c](API/ptl.c) | PTL implementation: init, task-body wrapper, tick hook, overrun task, logging task. |
| [main.c](main.c) | Entry point: initialises UART, loads a test scenario, calls `vPtlInit`. |
| [uart.c](uart.c) / [uart.h](uart.h) | Minimal memory-mapped UART0 driver for the MPS2 board. |
| [delay.c](delay.c) / [delay.h](delay.h) | `vDelayRoutine` - a tick-accurate busy-wait used to simulate a task's WCET. |
| [startup.c](startup.c) | Cortex-M3 startup / vector table. |
| [mps2_m3.ld](mps2_m3.ld) | Linker script for the target board. |
| [FreeRTOSConfig.h](FreeRTOSConfig.h) | Kernel configuration (tick rate, preemption, tick hook, heap, priorities). |
| [Makefile](Makefile) | Build, QEMU run, and GDB debug targets. |
| [tests/test_cases.json](tests/test_cases.json) | Declarative description of all test scenarios. |
| [tests/generate_tests.py](tests/generate_tests.py) | Generates `tests/test.h` (task scenarios) from `test_cases.json`. |
| [tests/run_tests.py](tests/run_tests.py) | Regression runner: schedulability analysis + build + QEMU + oracle checks. |
| `FreeRTOS/` | Vendored FreeRTOS kernel (not documented here). |

## How it works

The PTL is composed of a handful of cooperating FreeRTOS tasks plus the kernel tick hook.
All state lives in the global `xTaskStates[]` array, one entry per periodic task.

- **`vPtlInit(SchedulerConfig)`** - the single entry point (see [API/ptl.c](API/ptl.c#L208)).
  It validates the configuration, creates the log and overrun queues, spawns one FreeRTOS
  task per periodic task (all running the wrapper below), spawns the **logging task** and the
  **overrun (interrupt) task**, and finally starts the scheduler.

- **`vPtlTaskBody(void *)`** - the wrapper that every periodic task actually runs
  ([API/ptl.c](API/ptl.c#L110)). It optionally applies the start offset, then loops forever:
  it logs `START`, invokes the *user job body* once, logs `END`, and sleeps until the next
  release using `xTaskDelayUntil` (constant-cadence, jitter ≤ 1 tick). The user only writes
  the job body - the release loop is provided by the PTL.

- **`vApplicationTickHook(void)`** - the FreeRTOS tick hook ([API/ptl.c](API/ptl.c#L29)),
  enabled via `configUSE_TICK_HOOK`. On every tick it scans all tasks and, at ISR precision:
  - emits a `DEADLINE_MISS` event when the running job passes `release + D`;
  - detects a **period overrun** (`release + T` reached while the job is still running) and
    hands the offending task id to the overrun task through `xOverrunQueue`.

- **`vPtlInterruptTask(void *)`** - deferred handler for overruns ([API/ptl.c](API/ptl.c#L64)).
  It runs at the highest priority so it reacts promptly, advances the task's reference release
  time, and applies the configured policy (`SKIP` / `KILL` / `CATCH_UP`), logging the action.

- **`vPtlLoggingTask(void *)`** - drains `xLogQueue` and formats each event into a
  human-readable line printed over UART ([API/ptl.c](API/ptl.c#L160)). Doing all output from a
  single task keeps tracing thread-safe and off the ISR path.

- **`uxPtlApplyKillPolicy(...)`** - deletes and re-creates a task from its configuration,
  used by the `KILL` policy (and, optionally, by `CATCH_UP`) ([API/ptl.c](API/ptl.c#L9)).

### Priority mapping

User task priorities are declared in the range **1–5**. Internally the PTL shifts them up by
one (2–6) to reserve priority level 1, and creates its service tasks so they never starve
application tasks:

- **Overrun task** runs at `max(user priority) + 1` so overrun handling always preempts.
- **Logging task** runs at `max(user priority)` when `HANDLE_LOG_STARVATION` is enabled
  (default), otherwise at priority 1. `configMAX_PRIORITIES` is 9.

## Timing model

The kernel tick runs at **1000 Hz** (`configTICK_RATE_HZ = 1000`), so **1 tick = 1 ms** and
all period/deadline/workload values below are expressed directly in milliseconds. Definitions
follow the assignment terminology:

| Term | Meaning |
|------|---------|
| Period `T` | Time between two consecutive releases of a task. |
| Deadline `D` | Relative deadline from each release. If unspecified (`<= 0`), `D = T`. |
| Release `Rₖ` | Activation time of the *k*-th job. With no offset, `R₀ = t₀ = 0` for all tasks. |
| Finish `Fₖ` | Time at which job *k* completes. |
| Deadline miss | `Fₖ > Rₖ + D`. |
| Overrun | Previous job still running at `Rₖ₊₁` (i.e. it exceeds `T`). |

## Configuration interface

A schedule is described entirely by two structures declared in [API/ptl.h](API/ptl.h).

**Global - `SchedulerConfig`:**

| Field | Type | Meaning |
|-------|------|---------|
| `ePolicy` | `TaskPolicy` | Overrun policy applied to all tasks (`POLICY_SKIP` / `POLICY_KILL` / `POLICY_CATCH_UP`). |
| `xMaxTasks` | `BaseType_t` | Maximum number of tasks allowed (≤ `MAX_TASKS`, which is 8). |
| `pxTasks` | `TaskConfig *` | Array of task configurations. |
| `xNumTasks` | `BaseType_t` | Number of valid entries in `pxTasks[]`. |

> Tracing is not a runtime configuration field: it is enabled at compile time through the
> `TRACE_ENABLED` flag (default `1`), which compiles the whole trace path in or out — see
> [Compile-time options](#compile-time-options).

**Per task - `TaskConfig`:**

| Field | Type | Meaning |
|-------|------|---------|
| `pcName` | `char[]` | Human-readable task name. |
| `xIdTask` | `BaseType_t` | Task index (assigned by the PTL during init). |
| `pxTaskBody` | `void (*)(void *)` | User job body, executed once per release. |
| `pvParams` | `void *` | Argument passed to the job body on each invocation. |
| `usStackDepth` | `uint16_t` | Stack depth in words. |
| `uxPriority` | `UBaseType_t` | Task priority (1-5). |
| `ulPeriodMs` | `uint32_t` | Period `T`, in milliseconds. |
| `ulDeadline` | `uint32_t` | Relative deadline `D`, in ms. If `<= 0`, set to `ulPeriodMs`. |
| `ulOffsetMs` | `uint32_t` | Initial phase/offset before the first release. |

### Example

```c
void vTaskA_body(void *arg) { /* ... */ }
void vTaskB_body(void *arg) { /* ... */ }

TaskConfig xTasks[MAX_TASKS] = {
    { "A",   0,  vTaskA_body, NULL, configMINIMAL_STACK_SIZE, 3,  10,  10,  0 },
    { "B",   1,  vTaskB_body, NULL, configMINIMAL_STACK_SIZE, 2,  20,  15,  0 },
};

SchedulerConfig xCfg = {
    .ePolicy   = POLICY_SKIP,
    .xMaxTasks = MAX_TASKS,
    .pxTasks   = xTasks,
    .xNumTasks = 2,
};

vPtlInit(xCfg);   /* defines t0, starts all tasks, never returns */
```

> In the tests these structures are produced automatically from
> [tests/test_cases.json](tests/test_cases.json) by [tests/generate_tests.py](tests/generate_tests.py), and
> [main.c](main.c) selects a scenario at compile time via `-DTEST_ID=<n>`.

## Overrun policies

When a job is still running at its next release, the PTL applies one globally selected policy
(handled in [`vPtlInterruptTask`](API/ptl.c#L64)):

| Policy | Behaviour | Log |
|--------|-----------|-----|
| **`POLICY_SKIP`** | The pending release is dropped; the late job runs to completion. Cadence resumes at the following period. | `OVERRUN -> SKIP` |
| **`POLICY_KILL`** | The late job is terminated immediately (task deleted and re-created) and a fresh job is released. | `OVERRUN -> KILL` |
| **`POLICY_CATCH_UP`** | The late job is marked missed and the next release proceeds so nominal cadence is kept. | `OVERRUN -> CATCH_UP` |

The `CATCH_UP` behaviour has two flavours selectable at compile time via `CATCH_UP_VERSION`
(see [Compile-time options](#compile-time-options)).

## Trace &amp; monitoring output

The logging task prints one line per event over UART with tick-level timestamps. The exact
formats emitted by [`vPtlLoggingTask`](API/ptl.c#L161) are:

```
[INFO] t=<tick> task=<name> START
[INFO] t=<tick> task=<name> END
[WARN] t=<tick> task=<name> DEADLINE_MISS
[WARN] t=<tick> task=<name> OVERRUN -> SKIP
[WARN] t=<tick> task=<name> OVERRUN -> CATCH_UP
[WARN] t=<tick> task=<name> OVERRUN -> KILL
```

The trace captures task start/end ticks and deadline misses / forced terminations, providing a
tick-resolution timeline suitable for validation and regression checking.

Tracing is controlled by the `TRACE_ENABLED` define ([API/ptl.h](API/ptl.h#L40-L46), default `1`).
Building with `TRACE_ENABLED=0` compiles tracing out entirely - no events are queued, and the log
queue and the logging task are not created - so the scheduler runs with zero tracing overhead:

```sh
make EXTRA_CFLAGS="-DTRACE_ENABLED=0" all
```

Fatal `[ERROR]` messages from `vPtlInit` are always printed, regardless of `TRACE_ENABLED`.
See [Compile-time options](#compile-time-options) for the full list of build flags.

## Building &amp; running

### Prerequisites

The target is **QEMU Cortex-M3** emulating the ARM **MPS2-AN385** board. You need:

- `arm-none-eabi-gcc` + newlib (`gcc-arm-none-eabi`, `libnewlib-arm-none-eabi`)
- `qemu-system-arm`
- `make`
- `python3` (only for the test suite)
- `gdb-multiarch` (only for debugging)

On Debian/Ubuntu:

```sh
sudo apt-get install gcc-arm-none-eabi libnewlib-arm-none-eabi make qemu-system-arm python3 gdb-multiarch
```

### Build

```sh
make clean      # remove build artifacts
make            # compile and link -> Output/demo.elf
```

To build a specific scenario from the test suite, pass its id (see
[tests/test_cases.json](tests/test_cases.json)):

```sh
make EXTRA_CFLAGS="-DTEST_ID=3" all
```

> **Note:** building requires `tests/test.h`, which is generated from the JSON scenarios by
> `python3 tests/generate_tests.py` (the regression runner does this automatically). `TEST_ID`
> defaults to `1`.

### Run in QEMU

```sh
make qemu_start   # run Output/demo.elf
```

## Compile-time options

Three behaviours are selectable at build time through `-D` flags (defaults in
[API/ptl.h](API/ptl.h#L26-L46)):

| Flag | Values | Default | Effect |
|------|--------|---------|--------|
| `TEST_ID` | `1`–`15` | `1` | Selects which scenario from `test_cases.json` is loaded by `main`. |
| `HANDLE_LOG_STARVATION` | `0` / `1` | `1` | `1`: logging task runs at the highest user priority to avoid dropped logs under load. `0`: logging task runs at priority 1. |
| `CATCH_UP_VERSION` | `0` / `1` | `0` | `0`: on overrun just log and advance the release (SKIP-like). `1`: kill the current job, release a fresh one, and log the previous job as missed (KILL-like). |
| `TRACE_ENABLED` | `0` / `1` | `1` | `1`: trace events are queued and printed on the UART by the logging task. `0`: tracing is compiled out entirely — no log queue, no logging task, zero runtime overhead. |

Example:

```sh
make EXTRA_CFLAGS="-DTEST_ID=4 -DHANDLE_LOG_STARVATION=1 -DCATCH_UP_VERSION=1" all
```

## Testing &amp; regression suite

Testing is fully automated and declarative. Scenarios live in
[tests/test_cases.json](tests/test_cases.json); each entry defines the policy, the task set
(`workload`, `priority`, `period`, `deadline`, `offset`) and an **oracle** of substrings that
**must** and **must not** appear in the trace.

Run the whole suite:

```sh
python3 tests/run_tests.py
```

For non-interactive use, the flags can be passed on the command line:

```sh
python3 tests/run_tests.py --handle-log-starvation 1 --catch-up-version 0
```

The runner ([tests/run_tests.py](tests/run_tests.py)) will:

1. Prompt for the `HANDLE_LOG_STARVATION` and `CATCH_UP_VERSION` flags
   (skipped for any flag passed on the command line).
2. Print a **schedulability analysis** for every scenario:
   - **EDF** - necessary &amp; sufficient utilisation bound `U ≤ 1`.
   - **RM** - sufficient hyperbolic bound `∏(Uᵢ + 1) ≤ 2`.
   - **DM** - necessary &amp; sufficient response-time analysis.
3. Generate `tests/test.h` from the JSON via [tests/generate_tests.py](tests/generate_tests.py).
4. For each scenario: `make clean && make -DTEST_ID=<n> …`, run it under QEMU
   (killed after 2 s by default; a scenario can override this with a `"timeout"`
   key, in seconds, in `test_cases.json`), capture the serial trace, and check it
   against the `must_have` / `must_not_have` oracle.
5. Print a per-test `PASSED`/`FAILED` result and a final aggregate status.

The 15 shipped scenarios cover baseline periodic execution, RM preemption, single- and
multi-task overload under each policy, constrained deadlines (`D < T`), offset/phasing
pipelines, the maximum 8-task load, fault isolation between healthy and overloaded tasks, and
harmonic / non-harmonic schedulable sets.