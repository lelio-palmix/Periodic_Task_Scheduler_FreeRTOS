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

QueueHandle_t logQueue;

SemaphoreHandle_t xSemaphore;

// TODO: create dedicated hook functions to handle all errors(?)

// By default, use SKIP globally
int globalPolicy = 0;

typedef struct
{
    TaskHandle_t task;
    int state;
    int k; //keeps track of job releases
} TaskState;

TaskState taskState[8];

typedef struct
{
    char *name;
    int idTask;
    void (*taskBody)(void *);
    void *params;
    configSTACK_DEPTH_TYPE stackDepth;
    UBaseType_t uxPriority;
    uint64_t period_ms;
    uint64_t deadline;
    uint64_t offset_ms;
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

    const TaskConfig taskConfig = *((TaskConfig *)params);

    TickType_t xLastWakeUpTime;
    TickType_t xLastJobCompleted;

    char logMessage[MESSAGE_LENGTH];
    char taskName[configMAX_TASK_NAME_LEN];
    sprintf(taskName, "%s", taskConfig.name);
    char errorQueueSend[MESSAGE_LENGTH];
    sprintf(errorQueueSend, "Failed to send log - Task %s", taskName);

    const TickType_t xPeriod = pdMS_TO_TICKS(taskConfig.period_ms);
    const TickType_t xDeadline = pdMS_TO_TICKS(taskConfig.deadline);
    const TickType_t xOffset = pdMS_TO_TICKS(taskConfig.offset_ms);

    void (*functionBody)(void *) = taskConfig.taskBody;

    const int idTask = taskConfig.idTask;

    xLastWakeUpTime = xTaskGetTickCount();

    if (xOffset>0) {
        vTaskDelay(pdMS_TO_TICKS(xOffset));
    }
    while (1)
    {

        xTaskDelayUntil(&xLastWakeUpTime, xPeriod);

        if( xSemaphoreTake( xSemaphore, portMAX_DELAY ) == pdTRUE )
        {
        taskState[idTask].state = TASK_RUNNING;
        xSemaphoreGive( xSemaphore );
        }

        // Call function defined by the user
        functionBody(taskConfig.params);

        // TODO: Add a overrun check here or with a timer
        if( xSemaphoreTake( xSemaphore, portMAX_DELAY ) == pdTRUE )
        {
            taskState[idTask].state = TASK_NOT_RUNNING;
            taskState[idTask].k++;
            xSemaphoreGive( xSemaphore );
        }
        // Check if there is a deadline miss
        xLastJobCompleted = xTaskGetTickCount();
        if (xLastJobCompleted > xLastWakeUpTime + xDeadline)
        {
            // Log deadline miss
            sprintf(logMessage, "[%lu]Task %s deadline miss (D=%lu)", xLastJobCompleted, taskName, xLastWakeUpTime + xDeadline);
            if (xQueueSend(logQueue, logMessage, portMAX_DELAY) != pdPASS)
            {
                UART_printf(errorQueueSend);
            }
        }
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

void Init(const SchedulerConfig sconfig)
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
        xTaskCreate(Task_Function, task->name, task->stackDepth, task, task->uxPriority, &taskState[i].task);
        taskState[i].state = TASK_NOT_RUNNING;
        taskState[i].k = 0;
    }

    /* Create a mutex type semaphore. */
    xSemaphore = xSemaphoreCreateMutex();

    if (xSemaphore == NULL)
    {
        UART_printf("Failed to create mutex!");
        while(1);
    }

    vTaskStartScheduler();
};

int main(void)
{

    UART_init();
    // create queue for storing pointers of logging messages
    logQueue = xQueueCreate(QUEUE_LENGTH, sizeof(char) * MESSAGE_LENGTH);

    SchedulerConfig sconfig =
        {.policy = POLICY_SKIP,
         .trace_enabled = 1,
         .max_tasks = MAX_TASKS,
         .tasks = NULL,
         .num_tasks = 0};

    Init(sconfig);

    while (1);
}