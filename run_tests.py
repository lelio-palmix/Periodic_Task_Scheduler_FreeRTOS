import subprocess
import sys

TESTS = [
    {
        "id": 1,
        "name": "Preemption and timing consistency checks",
        "must_have": ["task=TaskA END"],
        "must_not_have": ["OVERRUN", "DEADLINE_MISS"] 
    },
    {
        "id": 2,
        "name": "Stress Test (Overlapping HRT) - Policy SKIP",
        "must_have": ["OVERRUN -> SKIP", "DEADLINE_MISS"],
        "must_not_have": ["OVERRUN -> CATCH_UP", "OVERRUN -> KILL"]
    },
    {
        "id": 3,
        "name": "Stress Test (Overlapping HRT) - Policy CATCH_UP",
        "must_have": ["OVERRUN -> CATCH_UP", "DEADLINE_MISS"],
        "must_not_have": ["OVERRUN -> SKIP", "OVERRUN -> KILL"]
    },
    {
        "id": 4,
        "name": "Edge-case tests (Minimal time gaps)",
        "must_have": ["DEADLINE_MISS"],
        "must_not_have": []
    }
]

def run_test(test_config):
    test_id = test_config["id"]
    test_name = test_config["name"]
    
    compile_cmd = f"make clean && make EXTRA_CFLAGS='-DTEST_ID={test_id}' all"
    subprocess.run(compile_cmd, shell=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    
    qemu_cmd = "make qemu_start"
    
    try:
        result = subprocess.run(qemu_cmd, shell=True, timeout=1.0, capture_output=True, text=True)
        output = result.stdout
    except subprocess.TimeoutExpired as e:
        output = e.stdout.decode('utf-8') if e.stdout else ""

    passed = True
    for req in test_config["must_have"]:
        if req not in output: passed = False
            
    for req in test_config["must_not_have"]:
        if req in output: passed = False

    status = "PASSED" if passed else "FAILED"
    print(f"Test {test_id} – {test_name}: {status}")
    
    return passed

if __name__ == "__main__":
    print("\n--- Running Automated Test Suite ---")
    all_passed = True
    for test in TESTS:
        if not run_test(test):
            all_passed = False
            
    print("------------------------------------")
    if all_passed:
        print("Integration Tests: ALL PASSED")
        sys.exit(0) # 0 = Success for GitLab CI
    else:
        print("Integration Tests: FAILED")
        sys.exit(1) # 1 = Failure for GitLab CI