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

volatile QueueHandle_t logQueue;

volatile SemaphoreHandle_t xSemaphore;

// TODO: create dedicated hook functions to handle all errors(?)

// By default, use SKIP globally
int globalPolicy = 0;

typedef struct
{
    TaskHandle_t task;
    int state;
    int k; //keeps track of job releases
} TaskState;

volatile TaskState taskState[8];

typedef struct
{
    char *name;
    int idTask;
    void (*taskBody)(void *);
    void *params;
    configSTACK_DEPTH_TYPE stackDepth;
    UBaseType_t uxPriority;
    int period_ms;
    int deadline;
    int offset_ms;
} TaskConfig;

typedef struct
{
    int policy;
    int trace_enabled;
    int max_tasks;
    TaskConfig *tasks;
    int num_tasks;
}  SchedulerConfig;

void Task_Function(void *params)
{

    TaskConfig taskConfig = *((TaskConfig *)params);

    TickType_t xLastWakeUpTime;
    TickType_t xLastJobCompleted;
    char logMessage[MESSAGE_LENGTH];
    char taskName[configMAX_TASK_NAME_LEN];
    sprintf(taskName, "%s", taskConfig.name);
    char errorQueueSend[MESSAGE_LENGTH];
    //sprintf(errorQueueSend, "Failed to send log - Task %s", taskName);

    const TickType_t xPeriod = pdMS_TO_TICKS(taskConfig.period_ms);
    char periodMessage[MESSAGE_LENGTH];
    //sprintf(periodMessage, "Task %s period: %lu ticks \n", taskName, xPeriod);
    //UART_printf(periodMessage);
    const TickType_t xDeadline = pdMS_TO_TICKS(taskConfig.deadline);
    const TickType_t xOffset = pdMS_TO_TICKS(taskConfig.offset_ms);

    void (*functionBody)(void *) = taskConfig.taskBody;

    const int idTask = taskConfig.idTask;

    xLastWakeUpTime = 0;

    if (xOffset>0) {
        vTaskDelay(pdMS_TO_TICKS(xOffset));
    }
    while (1)
    {
        //TODO: FIX xTaskDelayUntil

        

        if( xSemaphoreTake( xSemaphore, portMAX_DELAY ) == pdTRUE )
        {
        taskState[idTask].state = TASK_RUNNING;
        xSemaphoreGive( xSemaphore );
        }

        //print start time
        sprintf(logMessage, "Task %s START:[%lu] \n", taskName, xTaskGetTickCount());
        UART_printf( logMessage);

        // Call function defined by the user
        functionBody(taskConfig.params);

        //print end time
        xLastJobCompleted = xTaskGetTickCount();
        sprintf(logMessage, "Task %s END:[%lu] \n", taskName, xLastJobCompleted);
        UART_printf( logMessage);

        // TODO: Add a overrun check here or with a timer
        if( xSemaphoreTake( xSemaphore, portMAX_DELAY ) == pdTRUE )
        {
            taskState[idTask].state = TASK_NOT_RUNNING;
            taskState[idTask].k++;
            xSemaphoreGive( xSemaphore );
        }
       
        // Check if there is a deadline miss
        if (xLastJobCompleted > xLastWakeUpTime + xDeadline)
        {
            // Log deadline miss
            sprintf(logMessage, "[%lu]Task %s deadline miss (D=%lu)", xLastJobCompleted, taskName, xLastWakeUpTime + xDeadline);
            if (xQueueSend(logQueue, logMessage, portMAX_DELAY) != pdPASS)
            {
                UART_printf(errorQueueSend);
            }
        }
        xTaskDelayUntil(&xLastWakeUpTime, xPeriod);
    }
}

void LoggingTask(void *params)
{
    // TODO: decide if it's better to compose the string of the message inside the LoggingTask or keep it as it is already
    (void)params;
    char logMessage[MESSAGE_LENGTH];
    while (1)
    {
        if (xQueueReceive(logQueue, &logMessage, portMAX_DELAY) != pdPASS)
        {
            UART_printf("Failed to receive log message!");
        }
        else
        {
            UART_printf(logMessage);
        }
    }
}

void Init( const SchedulerConfig (sconfig))
{

    globalPolicy = sconfig.policy;
    if (sconfig.num_tasks > sconfig.max_tasks)
    {
        // TODO: Raise error "Number of tasks exceeds maximum number defined"
    }

    for (int i = 0; i < sconfig.num_tasks; i++)
    {
        // TODO: add a check if sconfig.tasks is NULL
        TaskConfig *task = (sconfig.tasks + i);
        if (task->deadline <= 0)
        {
            
            task->deadline = task->period_ms;
        }
        task->idTask = i;
        // BaseType_t xTaskCreate( TaskFunction_t pvTaskCode,
        //                         const char * const pcName,
        //                         const configSTACK_DEPTH_TYPE uxStackDepth,
        //                         void *pvParameters,
        //                         UBaseType_t uxPriority,
        //                         TaskHandle_t *pxCreatedTask
        //                       );

        // TODO: Remember to add TaskHandle_t of the created task so that inside an hypothetical timer it's possible to
        //       suspend/kill if necessary
        xTaskCreate(Task_Function, task->name, task->stackDepth, task, task->uxPriority,(TaskHandle_t *)&taskState[i].task);
        taskState[i].state = TASK_NOT_RUNNING;
        taskState[i].k = 0;
    }

    xTaskCreate(LoggingTask, "LoggingTask", DEFAULT_STACK_SIZE, NULL, 1, NULL);
    /* Create a mutex type semaphore. */
    xSemaphore = xSemaphoreCreateMutex();

    if (xSemaphore == NULL)
    {
        UART_printf("Failed to create mutex!");
        while(1);
    }

    vTaskStartScheduler();
};


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