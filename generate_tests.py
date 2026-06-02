import json
import sys

def generate_h():
    """Generate the test.h file based on the test cases.
    This function:
    1) Reads the test_cases.json file
    2) Extracts the test configurations
    3) Writes a header file with the appropriate task configurations for each test scenario.
    """
    try:
        with open('test_cases.json', 'r') as f:
            data = json.load(f)
    except FileNotFoundError:
        print("Error: test_cases.json not found.")
        sys.exit(1)

    with open('test.h', 'w') as out:
        out.write('/* File generated automatically by generate_tests.py */\n')
        out.write('#ifndef TEST_H\n#define TEST_H\n\n')
        out.write('#include "ptl.h"\n\n')
        out.write('/* Generic wrapper function: reads workload time from parameters */\n')
        out.write('void TaskTest_wrap(void *params)\n')
        out.write('{\n')
        out.write('    int duration = *((int *)params);\n')
        out.write('    delay_routine(pdMS_TO_TICKS(duration));\n')
        out.write('}\n\n')
        out.write('static inline void load_test_scenario(int test_id, SchedulerConfig *sconfig, TaskConfig *tasks) {\n')
        out.write('    switch(test_id) {\n')

        for test in data['tests']:
            out.write(f'        case {test["id"]}:\n')
            out.write(f'            sconfig->policy = {test["policy"]};\n')
            out.write(f'            sconfig->trace_enabled = 1;\n')
            out.write(f'            sconfig->max_tasks = MAX_TASKS;\n')
            out.write(f'            sconfig->tasks = tasks;\n')
            out.write(f'            sconfig->num_tasks = {len(test["tasks"])};\n')
            
            for i, t in enumerate(test['tasks']):
                var_name = f'work{test["id"]}_{t["name"]}'
                out.write(f'            static int {var_name} = {t["workload"]};\n')
                out.write(f'            tasks[{i}] = (TaskConfig){{"{t["name"]}", {t["id"]}, TaskTest_wrap, &{var_name}, 512, {t["priority"]}, {t["period"]}, {t["deadline"]}, {t.get("offset", 0)}}};\n')
            out.write('            break;\n')

        out.write('        default:\n')
        out.write('            break;\n')
        out.write('    }\n')
        out.write('}\n\n#endif // TEST_H\n')

if __name__ == '__main__':
    generate_h()