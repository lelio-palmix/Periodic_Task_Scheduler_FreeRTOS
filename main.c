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
    UART_init();
    TaskConfig tasks[MAX_TASKS];
    SchedulerConfig sconfig;

    load_test_scenario(TEST_ID, &sconfig, tasks);

    Init(sconfig);

    while (1);
    return 0;
}