#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "uart.h"
#include "delay.h"
#include "ptl.h"

#ifndef TEST_ID
#define TEST_ID 1
#endif


#include "test.h"
int main(void)
{
    vUartInit();
    TaskConfig xTasks[MAX_TASKS];
    SchedulerConfig xSchedulerConfig;

    vLoadTestScenario(TEST_ID, &xSchedulerConfig, xTasks);

    vPtlInit(xSchedulerConfig);

    while (1)
        ;
    return 0;
}


// void vTaskTestWrap(void *pvParameters)
// {
//     int xDuration = *((int *)pvParameters);
//     vDelayRoutine(pdMS_TO_TICKS(xDuration));
// }

// int main(void)
// {
//     vUartInit();
//     TaskConfig xTasks[MAX_TASKS];
//     SchedulerConfig xSchedulerConfig;

//     xSchedulerConfig.ePolicy = POLICY_SKIP;
//     xSchedulerConfig.xMaxTasks = MAX_TASKS;
//     xSchedulerConfig.pxTasks = xTasks;
//     xSchedulerConfig.xNumTasks = 3;
//     static int work11_TaskA = 10;
//     xTasks[0] = (TaskConfig){"TaskA", 0, vTaskTestWrap, &work11_TaskA, configMINIMAL_STACK_SIZE, 3, 60, 60, 0};
//     static int work11_TaskB = 10;
//     xTasks[1] = (TaskConfig){"TaskB", 1, vTaskTestWrap, &work11_TaskB, configMINIMAL_STACK_SIZE, 3, 60, 90, 15};
//     static int work11_TaskC = 10;
//     xTasks[2] = (TaskConfig){"TaskC", 2, vTaskTestWrap, &work11_TaskC, configMINIMAL_STACK_SIZE, 3, 60, 60, 30};
//     vPtlInit(xSchedulerConfig);

//     while (1)
//         ;
//     return 0;
// }
