import subprocess
import sys
import json
import os
import signal

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
            output, _ = process.communicate(timeout=3.0)
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

    from generate_tests import generate_h
    generate_h()

    with open('test_cases.json', 'r') as f:
        data = json.load(f)

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