## 🕒 Project 2 — Priority-Based Scheduler for Periodic Tasks in FreeRTOS

Goal: Add first-class periodic tasks (period + deadline) on top of the default FreeRTOS scheduler, preserving FreeRTOS’s preemptive, priority-based semantics.


## 🧭 1) Overview

FreeRTOS schedules tasks by priority but does not enforce periodicity or deadlines. This project introduces a thin Periodic Task Layer (PTL) that:

lets users declare periodic tasks with period and deadline,
performs job releases at the correct times,
detects deadline misses and period overruns,
and leaves preemption to FreeRTOS (unchanged).


The PTL must patch the kernel scheduler with minimal intrusivity to make it easily portable.


## 📚 2) Terminology


Period (T): time between releases.

Deadline (D): relative deadline from each release. If not specified, D = T.

Release time (Rₖ): k-th activation time; if all tasks start together, R₀ = t₀ for all.

Finish time (Fₖ): time when job k completes.

Deadline miss: Fₖ > Rₖ + D.

Overrun (period overrun): previous job not finished at Rₖ₊₁ (i.e., exceeds T).


## ⚙️ 3) Functional Requirements


Introduce dedicated periodic tasks API. To create and handle tasks with (period, deadline, priority, stack, name, entry).

All tasks start together (project requirement). Optional: allow a phase/offset parameter; if omitted, all start at t₀.

Priority-based, preemptive scheduling stays as in FreeRTOS.

Round-robin among tasks at the same priority (respecting FreeRTOS config).

Deadline & period checks are mandatory. On every job completion and at every new release, log and handle violations.

Policy on overrun when a new release arrives (pick one globally or per-task):


SKIP: skip the new job; let the late one finish.

KILL: terminate/suspend the running job immediately and release the new one.

CATCH_UP: release now, mark previous job missed, keep nominal cadence.



Config structure listing all periodic tasks and a Configure/Start entrypoint.


## 🧠 4) Task Model & Runtime Semantics

Tasks are functions with infinite loops (or loops controlled by PTL termination). To minimize errors, the programmer must write only the body of the loop, and FreeRTOS must wrap it into the loop, minimizing the number of function calls.

## ⏱️ 5) Deadline & Period Checks  — with Examples

### Checks


Deadline: On JobComplete(), compare Fₖ vs Rₖ + D. If Fₖ > Rₖ + D → DEADLINE_MISS.

Period: At Rₖ₊₁, if previous job isn’t complete → OVERRUN. Apply policy (SKIP/KILL/CATCH_UP), log action.


Example – Deadline miss

Task B: T=20 ms, D=15 ms.
k-th job starts at 40 ms, finishes at 56 ms → Fₖ=56 ms, Rₖ + D = 55 ms → miss.


Example – Overrun with SKIP

Task A: T=10 ms. Job k is still running at 30 ms (= Rₖ₊₁).
Policy SKIP: do not release job k+1 at 30 ms; continue running job k; next release at 40 ms; log OVERRUN + SKIP.


Example – Overrun with KILL

Same scenario, KILL: at 30 ms the PTL stops job k, releases job k+1 immediately; log OVERRUN + KILL.


Example – Overrun with CATCH_UP

At 30 ms release job k+1 immediately, mark job k as missed, maintain cadence; log OVERRUN + CATCH_UP.



## 🧰 6) Configuration Interface — with Examples
All scheduling parameters are provided in a dedicated configuration object you define.

# Must capture


Global: policy (SKIP/KILL/CATCH_UP), tracing enable, max tasks.

Per Task: name, entry, arg, stack, priority, T, D(optional)



If D not specified → D = T.


Example – Config sketch (pseudocode)

**SchedulerConfig cfg = {  
.policy = POLICY_SKIP,  
.trace_enabled = true,  
.max_tasks = 8,  
.tasks = {  
{ "A", TaskA, NULL, 512, 3, .period_ms = 10, .deadline_ms = 10 },  
{ "B", TaskB, NULL, 512, 2, .period_ms = 20, .deadline_ms = 15 },  
{ "C", TaskC, NULL, 512, 2, .period_ms = 50 },  
},  
.num_tasks = 3  
};** 

**Init(&cfg);
Start(); // defines t₀; all tasks start together**

## Assignment for all projects

### Non-Functional Requirements


Release jitter: ≤ 1 tick.

Overhead: ≤ 10% CPU (≤8 tasks).

Portability: QEMU Cortex-M.

Thread safety: use FreeRTOS-safe synchronization.

Documentation: clear API and timing model.

Codign guidelines strictly follow freeRTOS coding guidelines.


#### Error Handling & Edge Cases
Handle all errors with dedicated hook functions following the FreeRTOS style.

#### Trace and Monitoring System
A trace module must be developed for test and validation purposes to log and visualize scheduler behavior with tick-level precision.

### Logged information:

* Task start and end ticks
* Deadline misses or forced terminations
* CPU idle time

Example Output

[ 21 ms ] Task_A start
[ 26 ms ] Task_A end
[ 40 ms ] Task_B start
[ 47 ms ] Task_B deadline miss → terminated



[   0] A RELEASE
[   0] A START
[   5] A COMPLETE
[  10] A RELEASE
[  10] B RELEASE
[  10] A START
[  12] B START
[  25] B DEADLINE_MISS (D=15 @ tick 25)
[  30] A OVERRUN → SKIP
[  40] A RELEASE
...




### Production and Regression Testing:  
Develop an automated test suite to validate the scheduler's correctness and robustness.

The suite must include:

* Stress tests (e.g., overlapping HRT tasks).
* Edge-case tests (e.g., minimal time gaps).
* Preemption and timing consistency checks.

Each test must:

Produce human-readable summaries, and
Include automatic pass/fail checks for regression testing.

Example

Test 3 – Overlapping HRT Tasks: FAILED (Task_A killed at 32 ms)
Test 4 – SRT Preemption: PASSED



Configuration framework (optional)
Create a high-level configuration framework (e.g., a Python script) that enables the user to describe the target problem, perform schedulability analysis (for algorithms that support it), and ultimately generate the FreeRTOS skeleton application.

## 📦 Deliverables


✅ Modified FreeRTOS kernel with timeline-based scheduler.

✅ Configuration data structure and system call for schedule definition.

✅ Trace and monitoring system with tick-level resolution.

✅ Automated test suite for validation and regression checking.

✅ Documentation and example configurations.


🕒 Development Tools and Licenses
It is mandatory to use GitLab for all development purposes. A GitLab account for each group member will be created soon. You will receive an email with credentials.
Every file must be released following the FreeRTOS license schema.