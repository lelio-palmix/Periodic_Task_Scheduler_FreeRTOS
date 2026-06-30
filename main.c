#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "uart.h"
#include "delay.h"
#include "ptl.h"
#include "test.h"

#ifndef TEST_ID
#define TEST_ID 1
#endif

int main(void)
{
    vUartInit();
    TaskConfig xTasks[MAX_TASKS];
    SchedulerConfig xSchedulerConfig;

    vLoadTestScenario(TEST_ID, &xSchedulerConfig, xTasks);

    vPtlInit(xSchedulerConfig);

    while (1);
    return 0;
}
