#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "uart.h"
#include "delay.h"
#include "ptl.h"

/* If not passed from make, run Test 1 */
#ifndef TEST_ID
#define TEST_ID 1
#endif

/* Generic wrapper function: reads workload time from parameters */
void TaskTest_wrap(void *params)
{
    int duration = *((int *)params);
    delay_routine(duration);
}

int main(void)
{
    UART_init();
    TaskConfig tasks[MAX_TASKS];
    SchedulerConfig sconfig;

#if TEST_ID == 1
    /* TEST 1: Preemption and timing consistency checks
     * TaskA: 30ms period, execution time of 10ms -> should meet deadlines but saturate CPU
     * TaskB: 50ms period, 25ms execution time -> should meet deadlines
     */
    static int work1A = 10;
    static int work1B = 25;
    tasks[0] = (TaskConfig){"TaskA", 0, TaskTest_wrap, &work1A, 512, 3, 30, 30, 0};
    tasks[1] = (TaskConfig){"TaskB", 1, TaskTest_wrap, &work1B, 512, 2, 50, 50, 0};
    sconfig = (SchedulerConfig){POLICY_SKIP, 1, MAX_TASKS, tasks, 2};

#elif TEST_ID == 2
    /* TEST 2: Stress tests (Overlapping HRT tasks) -> POLICY SKIP
     * TaskA: 30ms period, 40ms execution time -> will miss deadlines and trigger policy
     * TaskB: 50ms period, 10ms execution time -> should meet deadlines but will be affected by TaskA's overruns
     */
    static int work2A = 40;
    static int work2B = 10;
    tasks[0] = (TaskConfig){"TaskA", 0, TaskTest_wrap, &work2A, 512, 3, 30, 15, 0};
    tasks[1] = (TaskConfig){"TaskB", 1, TaskTest_wrap, &work2B, 512, 2, 50, 50, 0};
    sconfig = (SchedulerConfig){POLICY_SKIP, 1, MAX_TASKS, tasks, 2};

#elif TEST_ID == 3
    /* TEST 3: Stress tests (Overlapping HRT tasks) -> POLICY CATCH_UP
     * TaskA: 30ms period, 40ms execution time -> will miss deadlines and trigger policy
     * TaskB: 50ms period, 10ms execution time -> should meet deadlines but will be affected by TaskA's overruns
     */
    static int work3A = 40;
    static int work3B = 10;
    tasks[0] = (TaskConfig){"TaskA", 0, TaskTest_wrap, &work3A, 512, 3, 30, 15, 0};
    tasks[1] = (TaskConfig){"TaskB", 1, TaskTest_wrap, &work3B, 512, 2, 50, 50, 0};
    sconfig = (SchedulerConfig){POLICY_CATCH_UP, 1, MAX_TASKS, tasks, 2};

#elif TEST_ID == 4
    /* TEST 4: Edge-case tests (Minimal time gaps)
     * TaskA: 15ms period, 15ms execution time -> should meet deadlines
     * TaskB: 20ms period, 20ms execution time -> should meet deadlines
     * TaskC: 30ms period, 30ms execution time -> should meet deadlines
     */
    static int work4 = 10;
    tasks[0] = (TaskConfig){"TaskA", 0, TaskTest_wrap, &work4, 512, 3, 15, 15, 0};
    tasks[1] = (TaskConfig){"TaskB", 1, TaskTest_wrap, &work4, 512, 2, 20, 20, 0};
    tasks[2] = (TaskConfig){"TaskC", 2, TaskTest_wrap, &work4, 512, 1, 30, 30, 0};
    sconfig = (SchedulerConfig){POLICY_CATCH_UP, 1, MAX_TASKS, tasks, 3};
#endif

    Init(sconfig);
    while (1)
        ;
    return 0;
}