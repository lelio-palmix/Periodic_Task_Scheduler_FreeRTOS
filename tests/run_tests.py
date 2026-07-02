import argparse
import subprocess
import sys
import json
import os
import signal
import math
from functools import reduce

# Repo layout: this script lives in tests/, while make must run from the
# repository root. Anchor both so the runner works from any directory.
TESTS_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(TESTS_DIR)

DEFAULT_QEMU_TIMEOUT = 2.0


def ask_flag(name, description):
    while True:
        answer = input(f"{name} ({description}) [0/1]: ").strip()
        if answer in ("0", "1"):
            return int(answer)
        print("  Please enter 0 or 1.")


def dm_guarantee(tasks):
    """Necessary and Sufficient condition for Rate Monotonic scheduling.
    Tasks must be ordered by higher priority (i.e. 1/T).
    Returns True if schedulable, False otherwise.
    """
    sorted_tasks = sorted(tasks, key=lambda t: t["deadline"])
    response_times = []

    for i, task in enumerate(sorted_tasks):
        Ci = task["workload"]
        Di = task["deadline"]
        I = 0
        while True:
            R = I + Ci
            if R > Di:
                response_times.append((task["name"], None, Di))
                return False
            I_new = sum(
                math.ceil(R / st["period"]) * st["workload"]
                for st in sorted_tasks[:i]
            )
            if I_new + Ci <= R:
                response_times.append((task["name"], R, Di))
                break
            I = I_new

    return True


def schedulability_analysis(tests):
    '''Perform schedulability analysis for each test scenario using:
    - EDF: Total utilization U <= 1.0 (Necessary and Sufficient)
    - RM: Product of (Ui + 1) <= 2.0 (Sufficient, not necessary)
    - RM: Necessary and Sufficient condition
    '''
    print("\n--- Schedulability Analysis ---")
    for test in tests:
        tasks = test["tasks"]
        utils = [t["workload"] / t["period"] for t in tasks]
        total_util = sum(utils)

        edf_ok = total_util <= 1.0
        rm_ok = reduce(lambda acc, u: acc * (u + 1), utils, 1.0) <= 2.0
        dm_ok = dm_guarantee(tasks)

        print(f"\n  Test {test['id']}: {test['name']}")
        print(f"    {'Task':<10} {'C':>4} {'T':>5} {'D':>5} {'U':>6}  {'R (DM)':>10}")
        for t, u in zip(tasks, utils):
            print(f"    {t['name']:<10} {t['workload']:>4} {t['period']:>5} {t['deadline']:>5} {u:>6.3f}  ")
        print(f"    Total utilization U = {total_util:.4f}")
        print(f"    EDF (N&S,  U<=1):           {'FEASIBLE' if edf_ok else 'NOT FEASIBLE'}")
        print(f"    RM  (suff, prod(Ui+1)<=2):  {'FEASIBLE' if rm_ok else 'NOT FEASIBLE'}")
        print(f"    DM  (N&S,  response time):  {'FEASIBLE' if dm_ok else 'NOT FEASIBLE'}")
    print("\n" + "-" * 40)


def run_test(test_config):
    """Run a single test scenario based on the provided configuration.
    This function:
    1) Compiles the code with the appropriate test id
    2) Executes the compiled binary in QEMU
    3) Captures the output and validates it against the expected results defined in the test configuration.
    """
    
    global HANDLE_LOG_STARVATION, CATCH_UP_VERSION

    test_id = test_config["id"]
    test_name = test_config["name"]
    
    print(f"\n[+] Running Test {test_id}: {test_name}")
    
    # 1. Compilation with test-specific flag
    compile_cmd = f"make clean && make EXTRA_CFLAGS='-DTEST_ID={test_id} -DHANDLE_LOG_STARVATION={HANDLE_LOG_STARVATION} -DCATCH_UP_VERSION={CATCH_UP_VERSION}' all"
    compile_result = subprocess.run(compile_cmd, shell=True, capture_output=True, text=True, cwd=REPO_ROOT)

    if compile_result.returncode != 0 or not os.path.exists(os.path.join(REPO_ROOT, "Output", "demo.elf")):
        print(f"  [!] Compilation failed for Test {test_id}")
        print("\n--- DEBUG: COMPILER OUTPUT ---")
        print(compile_result.stderr if compile_result.stderr else compile_result.stdout)
        print("------------------------------\n")
        return False

    # 2. Execution in QEMU
    qemu_cmd = [
        "make", "qemu_start"
    ]
    
    output = ""
    try:
        process = subprocess.Popen(qemu_cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, preexec_fn=os.setsid, cwd=REPO_ROOT)
        
        # Running with a timeout to prevent hangs. If QEMU doesn't finish in time, we kill it.
        # Each scenario can override the default via a "timeout" key in test_cases.json.
        try:
            output, _ = process.communicate(timeout=test_config.get("timeout", DEFAULT_QEMU_TIMEOUT))
        except subprocess.TimeoutExpired:
            os.killpg(os.getpgid(process.pid), signal.SIGTERM)
            output, _ = process.communicate()
            
    except Exception as e:
        print(f"  [!] Error running QEMU: {e}")
        return False

    passed = True
    # 3. Validation of output against oracle
    for req in test_config.get("must_have", []):
        if req not in output: 
            print(f"  [!] MISSING: '{req}'")
            passed = False
            
    for req in test_config.get("must_not_have", []):
        if req in output: 
            print(f"  [!] FORBIDDEN FOUND: '{req}'")
            passed = False

    status = "PASSED" if passed else "FAILED"
    print(f"Result: {status}")
    
    if not passed:
        print("\n--- DEBUG: QEMU OUTPUT ---")
        print(output if output else "[Empty Output]")
        print("--------------------------\n")
    
    return passed

if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Compile and run every scenario in test_cases.json under QEMU. "
                    "Flags not passed on the command line are asked interactively."
    )
    parser.add_argument("--handle-log-starvation", type=int, choices=[0, 1], default=None,
                        help="0 = disabled, 1 = enabled")
    parser.add_argument("--catch-up-version", type=int, choices=[0, 1], default=None,
                        help="0 = Similar to SKIP, 1 = Similar to KILL")
    args = parser.parse_args()

    print("\n--- Starting Automated Test Suite ---")

    print("\n--- Flags Configuration ---")
    if args.handle_log_starvation is not None:
        HANDLE_LOG_STARVATION = args.handle_log_starvation
        print(f"HANDLE_LOG_STARVATION = {HANDLE_LOG_STARVATION}")
    else:
        HANDLE_LOG_STARVATION = ask_flag(
            "HANDLE_LOG_STARVATION",
            "0 = disabled, 1 = enabled"
        )
    if args.catch_up_version is not None:
        CATCH_UP_VERSION = args.catch_up_version
        print(f"CATCH_UP_VERSION = {CATCH_UP_VERSION}")
    else:
        CATCH_UP_VERSION = ask_flag(
            "CATCH_UP_VERSION",
            "0 = Similar to SKIP, 1 = Similar to KILL"
        )

    with open(os.path.join(TESTS_DIR, 'test_cases.json'), 'r') as f:
        data = json.load(f)

    schedulability_analysis(data['tests'])

    from generate_tests import generate_h
    generate_h()

    all_passed = True
    for test in data['tests']:
        if not run_test(test):
            all_passed = False

    print("\n" + "="*40)

    test_h = os.path.join(TESTS_DIR, "test.h")
    if os.path.exists(test_h):
        os.remove(test_h)

    if all_passed:
        print("FINAL STATUS: ALL TESTS PASSED")
        sys.exit(0)
    else:
        print("FINAL STATUS: TESTS FAILED")
        sys.exit(1)