#include "FreeRTOS.h"
#include "task.h"
#include "uart.h"
#include "delay.h"
#include "ptl.h"

void TaskTest_wrap(void *params)
{
    char *taskName = (char *)params;
    if (taskName == NULL) return;

    if (taskName[4] == 'A' || taskName[4] == 'B') {
        delay_routine(pdMS_TO_TICKS(80));
    } else {
        delay_routine(pdMS_TO_TICKS(5));
    }
}

int main(void)
{
    UART_init();
    
    /* Task configuration */
    TaskConfig task1 = {
        .name = "TaskA",
        .idTask = 0,
        .taskBody = TaskTest_wrap, 
        .params = (void* )"TaskA", 
        .stackDepth = DEFAULT_STACK_SIZE,
        .uxPriority = 2,
        .period_ms = 30,
        .deadline = 5, 
        .offset_ms = 0
    };
    
    TaskConfig task2 = {
        .name = "TaskB",
        .idTask = 1,
        .taskBody = TaskTest_wrap,  
        .params = (void* )"TaskB", 
        .stackDepth = DEFAULT_STACK_SIZE,
        .uxPriority = 2,
        .period_ms = 30,
        .deadline = 5, 
        .offset_ms = 15
    };
    
    TaskConfig task3 = {
        .name = "TaskC",
        .idTask = 2,
        .taskBody = TaskTest_wrap,  
        .params = (void* )"TaskC", 
        .stackDepth = DEFAULT_STACK_SIZE,
        .uxPriority = 2,
        .period_ms = 100,
        .deadline = 100,
        .offset_ms = 0
    };

    TaskConfig tasks[MAX_TASKS];
    tasks[0] = task1;
    tasks[1] = task2;
    tasks[2] = task3;

    /* Scheduler configuration */
    SchedulerConfig sconfig = {
         .policy = POLICY_SKIP,
         .trace_enabled = 1,
         .max_tasks = MAX_TASKS,
         .tasks = tasks,
         .num_tasks = 3
    };

    /* Call the initialization function for PTL scheduler */
    Init(sconfig);

    while (1);
    
    return 0;
}