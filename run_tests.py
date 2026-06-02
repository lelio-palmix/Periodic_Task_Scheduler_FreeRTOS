import subprocess
import sys
import json
import os
import signal
import math
from functools import reduce


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
    
    test_id = test_config["id"]
    test_name = test_config["name"]
    
    print(f"\n[+] Running Test {test_id}: {test_name}")
    
    # 1. Compilation with test-specific flag
    compile_cmd = f"make clean && make EXTRA_CFLAGS='-DTEST_ID={test_id}' all"
    subprocess.run(compile_cmd, shell=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    
    if not os.path.exists("./Output/demo.elf"):
        print(f"  [!] Compilation failed for Test {test_id}")
        return False

    # 2. Execution in QEMU
    qemu_cmd = [
        "make", "qemu_start"
    ]
    
    output = ""
    try:
        process = subprocess.Popen(qemu_cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, preexec_fn=os.setsid)
        
        # Running with a timeout to prevent hangs. If QEMU doesn't finish in time, we kill it.
        try:
            output, _ = process.communicate(timeout=2.0)
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
    print("\n--- Starting Automated Test Suite ---")

    with open('test_cases.json', 'r') as f:
        data = json.load(f)

    schedulability_analysis(data['tests'])

    from generate_tests import generate_h
    generate_h()

    all_passed = True
    for test in data['tests']:
        if not run_test(test):
            all_passed = False

    print("\n" + "="*40)

    if os.path.exists("test.h"):
        os.remove("test.h")

    if all_passed:
        print("FINAL STATUS: ALL TESTS PASSED")
        sys.exit(0)
    else:
        print("FINAL STATUS: TESTS FAILED")
        sys.exit(1)