import json
import os
import sys

# Paths are anchored to this script's directory so the generator
# works no matter which directory it is launched from.
TESTS_DIR = os.path.dirname(os.path.abspath(__file__))
TEST_CASES_JSON = os.path.join(TESTS_DIR, 'test_cases.json')
TEST_H = os.path.join(TESTS_DIR, 'test.h')

def generate_h():
    """Generate the test.h file based on the test cases.
    This function:
    1) Reads the test_cases.json file
    2) Extracts the test configurations
    3) Writes a header file with the appropriate task configurations for each test scenario.
    """
    try:
        with open(TEST_CASES_JSON, 'r') as f:
            data = json.load(f)
    except FileNotFoundError:
        print("Error: test_cases.json not found.")
        sys.exit(1)

    with open(TEST_H, 'w') as out:
        out.write('/* File generated automatically by generate_tests.py */\n')
        out.write('#ifndef TEST_H\n#define TEST_H\n\n')
        out.write('#include "ptl.h"\n\n')
        out.write('/* Generic wrapper function: reads workload time from parameters */\n')
        out.write('void vTaskTestWrap(void *pvParameters)\n')
        out.write('{\n')
        out.write('    int xDuration = *((int *)pvParameters);\n')
        out.write('    vDelayRoutine(pdMS_TO_TICKS(xDuration));\n')
        out.write('}\n\n')
        out.write('static inline void vLoadTestScenario(int xTestId, SchedulerConfig *pxSchedulerConfig, TaskConfig *pxTasks) {\n')
        out.write('    switch(xTestId) {\n')

        for test in data['tests']:
            out.write(f'        case {test["id"]}:\n')
            out.write(f'            pxSchedulerConfig->ePolicy = {test["policy"]};\n')
            out.write(f'            pxSchedulerConfig->xMaxTasks = MAX_TASKS;\n')
            out.write(f'            pxSchedulerConfig->pxTasks = pxTasks;\n')
            out.write(f'            pxSchedulerConfig->xNumTasks = {len(test["tasks"])};\n')

            for i, t in enumerate(test['tasks']):
                var_name = f'work{test["id"]}_{t["name"]}'
                out.write(f'            static int {var_name} = {t["workload"]};\n')
                out.write(f'            pxTasks[{i}] = (TaskConfig){{"{t["name"]}", {t["id"]}, vTaskTestWrap, &{var_name}, configMINIMAL_STACK_SIZE, {t["priority"]}, {t["period"]}, {t["deadline"]}, {t.get("offset", 0)}}};\n')
            out.write('            break;\n')

        out.write('        default:\n')
        out.write('            break;\n')
        out.write('    }\n')
        out.write('}\n\n#endif // TEST_H\n')

if __name__ == '__main__':
    generate_h()