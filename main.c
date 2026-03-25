//
// Created by danielecanu on 02/02/2026.
//

#include <math.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "uart.h"
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include "delay.h"

#include "API/ptl.h"

#define MAX_WAIT portMAX_DELAY
#define QUEUE_LENGTH 15
#define MESSAGE_LENGTH 60

#define DEFAULT_STACK_SIZE 512

#define POLICY_SKIP 0
#define POLICY_KILL 1
#define POLICY_CATCH_UP 2
#define MAX_TASKS 8
#define TASK_RUNNING 1
#define TASK_NOT_RUNNING 2

// TODO: create dedicated hook functions to handle all errors(?)

// By default, use SKIP globally

volatile QueueHandle_t logQueue;
volatile SemaphoreHandle_t xSemaphore;
volatile TaskState taskState[MAX_TASKS];

void TaskTest_wrap(void *params)
{
    char *taskName = (char *)params;

   
    if (taskName == NULL)
    {
        UART_printf("Hello from Unknown Task!\n");
        return;
    }
    delay_routine(100);
    
    
    char buffer[MESSAGE_LENGTH]; 
    

    snprintf(buffer, MESSAGE_LENGTH, "\nHello from %s!\n", taskName);


    UART_printf(buffer);
}
// Global array to store task configurations, garantee that the pointer to the task configuration passed to the
// task function is valid for the entire execution of the program

int main(void)
{
    UART_init();
    // create queue for storing pointers of logging messages
    logQueue = xQueueCreate(QUEUE_LENGTH, sizeof(char) * MESSAGE_LENGTH);
    TaskConfig task1 = {
        .name = "Task1",
        .idTask = 0,
        .taskBody = TaskTest_wrap, 
        .params = (void* )"Task1", // TODO: add params if necessary
        .stackDepth = DEFAULT_STACK_SIZE,
        .uxPriority = 1,
        .period_ms = 30,
        .deadline = 10,
        .offset_ms = 0
    };
    TaskConfig task2 = {
        .name = "Task2",
        .idTask = 1,
        .taskBody = TaskTest_wrap,  
        .params = (void* )"Task2", // TODO: add params if necessary
        .stackDepth = DEFAULT_STACK_SIZE,
        .uxPriority = 1,
        .period_ms = 50,
        .deadline = 10,
        .offset_ms = 0
    };
    TaskConfig task3 = {
        .name = "Task3",
        .idTask = 2,
        .taskBody = TaskTest_wrap,  
        .params = (void* )"Task3", // TODO: add params if necessary
        .stackDepth = DEFAULT_STACK_SIZE,
        .uxPriority = 1,
        .period_ms = 60,
        .deadline = 30,
        .offset_ms = 0
    };
    TaskConfig tasks[MAX_TASKS];
    tasks[0] = task1;
    tasks[1] = task2;
    tasks[2] = task3;

    //TaskConfig tasks[] = {task1, task2, task3};
        
    SchedulerConfig sconfig =
        {.policy = POLICY_SKIP,
         .trace_enabled = 1,
         .max_tasks = MAX_TASKS,
         .tasks =  tasks,
         .num_tasks = 3};

    Init(sconfig);

    while (1);
}