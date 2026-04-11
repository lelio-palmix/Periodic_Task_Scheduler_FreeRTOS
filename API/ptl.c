#include "ptl.h"
#include "uart.h"
#include <stdio.h>

volatile QueueHandle_t logQueue;
volatile SemaphoreHandle_t xSemaphore;
volatile TaskState taskState[MAX_TASKS];

/* Helper function to check if a task has overrun */
BaseType_t PTL_IsOverrun(TickType_t xLastWakeUpTime, TickType_t xPeriod, TickType_t xNow)
{
    TickType_t xNextRelease = xLastWakeUpTime + xPeriod;
    return (xNextRelease < xNow) ? pdTRUE : pdFALSE;
}

/* This function applies the SKIP policy when an overrun is detected */
UBaseType_t PTL_ApplySkipPolicy(TickType_t *xLastWakeUpTime, TickType_t xPeriod, TickType_t xNow)
{
    UBaseType_t skippedReleases = 0U;

    if ((xLastWakeUpTime == NULL) || (xPeriod == 0U))
    {
        return 0U;
    }

    /*
     * Advance the release reference while releases are already in the past.
     * xTaskDelayUntil() will then wait for the first valid future release.
     */
    while (PTL_IsOverrun(*xLastWakeUpTime, xPeriod, xNow) == pdTRUE)
    {
        *xLastWakeUpTime += xPeriod;
        skippedReleases++;
    }

    return skippedReleases;
}

/* * This ISR runs at every system tick. It is used to achieve tick-level precision 
 * for checking period overruns.
 */
void vApplicationTickHook(void)
{
    TickType_t currentTick = xTaskGetTickCountFromISR();
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    char hookLogMessage[MESSAGE_LENGTH];

    for (int i = 0; i < MAX_TASKS; i++)
    {
        if (taskState[i].task != NULL)
        {
            // Calculate the exact tick of the next expected release
            TickType_t nextRelease = taskState[i].lastReleaseTime + pdMS_TO_TICKS(taskState[i].period_ms);

            // If we hit the release tick (and it's not the initial tick 0)
            if ((currentTick == nextRelease) && (currentTick > 0))
            {
                // Advance the tracked release time for the next cycle
                taskState[i].lastReleaseTime = nextRelease;

                // Check if the task from the previous cycle is still running
                if (taskState[i].state == TASK_RUNNING)
                {
                    if (taskState[i].policy == POLICY_SKIP)
                    {
                        // Log the overrun exactly at the tick it happens
                        snprintf(hookLogMessage, MESSAGE_LENGTH,
                                 "[WARN] t=%lu task=%s OVERRUN -> SKIP\n",
                                 (unsigned long)currentTick, taskState[i].name);

                        xQueueSendFromISR(logQueue, hookLogMessage, &xHigherPriorityTaskWoken);
                    }
                }
            }
        }
    }
}

void Task_Function(void *params)
{
    TaskConfig taskConfig = *((TaskConfig *)params);

    TickType_t xLastWakeUpTime;
    TickType_t xLastJobCompleted;
    char logMessage[MESSAGE_LENGTH];
    
    const int idTask = taskConfig.idTask;
    const TickType_t xPeriod = pdMS_TO_TICKS(taskConfig.period_ms);
    const TickType_t xDeadline = pdMS_TO_TICKS(taskConfig.deadline);
    const TickType_t xOffset = pdMS_TO_TICKS(taskConfig.offset_ms);

    void (*functionBody)(void *) = taskConfig.taskBody;

    if (xOffset > 0)
    {
        vTaskDelay(xOffset);
    }

    // All tasks consider tick 0 as the starting point (t0)
    xLastWakeUpTime = 0;

    while (1)
    {
        if (xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdTRUE)
        {
            taskState[idTask].state = TASK_RUNNING;
            taskState[idTask].startTime = xTaskGetTickCount();
            xSemaphoreGive(xSemaphore);

            snprintf(logMessage, MESSAGE_LENGTH,
                     "[INFO] t=%lu task=%s START\n",
                     (unsigned long)taskState[idTask].startTime,
                     taskState[idTask].name);
            
            // Non-blocking send to avoid jitter (Timeout = 0)
            xQueueSend(logQueue, logMessage, (TickType_t)0);
        }

        // Call the user-defined periodic workload
        functionBody(taskConfig.params);

        xLastJobCompleted = xTaskGetTickCount();

        snprintf(logMessage, MESSAGE_LENGTH,
                 "[INFO] t=%lu task=%s END\n",
                 (unsigned long)xLastJobCompleted,
                 taskState[idTask].name);
        xQueueSend(logQueue, logMessage, (TickType_t)0);

        if (xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdTRUE)
        {
            taskState[idTask].finishTime = xLastJobCompleted;
            taskState[idTask].state = TASK_NOT_RUNNING;
            taskState[idTask].k++;
            xSemaphoreGive(xSemaphore);
        }

        // Check for deadline miss (only if the job completed after the next release time)
        if (xLastJobCompleted > xLastWakeUpTime + xDeadline)
        {
            snprintf(logMessage, MESSAGE_LENGTH,
                     "[WARN] t=%lu task=%s DEADLINE_MISS dl=%lu\n",
                     (unsigned long)xLastJobCompleted,
                     taskState[idTask].name,
                     (unsigned long)(xLastWakeUpTime + xDeadline));
            xQueueSend(logQueue, logMessage, (TickType_t)0);
        }

        // Apply skip policy if overrun detected.
        if (taskState[idTask].policy == POLICY_SKIP)
        {
            // Here we update the last wake-up time to the next valid release, effectively skipping missed releases.
            PTL_ApplySkipPolicy(&xLastWakeUpTime, xPeriod, xLastJobCompleted);
        }

        xTaskDelayUntil(&xLastWakeUpTime, xPeriod);
    }
}

void LoggingTask(void *params)
{
    (void)params;
    char logMessage[MESSAGE_LENGTH];
    
    while (1)
    {
        // Block indefinitely waiting for logs to print
        if (xQueueReceive(logQueue, &logMessage, portMAX_DELAY) == pdPASS)
        {
            UART_printf(logMessage);
        }
    }
}

void Init(const SchedulerConfig sconfig)
{
    int globalPolicy = sconfig.policy;

    if (sconfig.num_tasks > sconfig.max_tasks)
    {
        UART_printf("[ERROR] Number of tasks exceeds maximum.\n");
        while(1);
    }

    logQueue = xQueueCreate(QUEUE_LENGTH, sizeof(char) * MESSAGE_LENGTH);
    xSemaphore = xSemaphoreCreateMutex();

    if (xSemaphore == NULL || logQueue == NULL)
    {
        UART_printf("Failed to create RTOS primitives!");
        while(1);
    }

    // Initialize tasks
    for (int i = 0; i < sconfig.num_tasks; i++)
    {
        TaskConfig *task = (sconfig.tasks + i);
        
        if (task->deadline <= 0)
        {
            task->deadline = task->period_ms; // Implicit deadline
        }
        
        task->idTask = i;

        snprintf((char*)taskState[i].name, sizeof(taskState[i].name), "%s", task->name);
        taskState[i].policy = globalPolicy;
        taskState[i].state = TASK_NOT_RUNNING;
        taskState[i].k = 0;
        taskState[i].period_ms = task->period_ms;
        taskState[i].deadline = task->deadline;
        taskState[i].lastReleaseTime = 0;

        xTaskCreate(Task_Function, task->name, task->stackDepth, task, task->uxPriority, (TaskHandle_t *)&taskState[i].task);
    }

    // A dedicated logging task running at low priority
    xTaskCreate(LoggingTask, "LoggingTask", DEFAULT_STACK_SIZE, NULL, 1, NULL);

    vTaskStartScheduler();
}