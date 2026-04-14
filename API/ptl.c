#include "ptl.h"
#include "uart.h"
#include <stdio.h>

volatile QueueHandle_t logQueue;
volatile SemaphoreHandle_t xSemaphore;
volatile TaskState taskState[MAX_TASKS];
//dont save
volatile SchedulerConfig sconfig_global;

BaseType_t PTL_IsOverrun(TickType_t xLastWakeUpTime, TickType_t xPeriod, TickType_t xNow)
{
    TickType_t xNextRelease = xLastWakeUpTime + xPeriod;
    return (xNextRelease < xNow) ? pdTRUE : pdFALSE;
}

UBaseType_t PTL_ApplySkipPolicy(TickType_t *xLastWakeUpTime, TickType_t xPeriod, TickType_t xNow)
{
    UBaseType_t skippedReleases = 0U;

    if ((xLastWakeUpTime == NULL) || (xPeriod == 0U))
    {
        return 0U;
    }

    while (PTL_IsOverrun(*xLastWakeUpTime, xPeriod, xNow) == pdTRUE)
    {
        *xLastWakeUpTime += xPeriod;
        skippedReleases++;
    }

    return skippedReleases;
}

UBaseType_t PTL_ApplyKillPolicy(TickType_t *xLastWakeUpTime, TickType_t xPeriod, TickType_t xNow, int task_id)
{
    UBaseType_t skippedReleases = 0U;

    if ((xLastWakeUpTime == NULL) || (xPeriod == 0U))
    {
        return 0U;
    }
    TaskConfig *task = sconfig_global.tasks + task_id;
    
    if (PTL_IsOverrun(*xLastWakeUpTime, xPeriod, xNow) == pdTRUE)
    {
        vTaskDelete(task);
        xTaskCreate(Task_Function, task->name, task->stackDepth, task, task->uxPriority, (TaskHandle_t *)&taskState[i].task);

    }

    return skippedReleases;
}
/* ISR for tick-level precision checking of period overruns. */
void vApplicationTickHook(void)
{
    TickType_t currentTick = xTaskGetTickCountFromISR();
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    LogEvent ev;

    for (int i = 0; i < MAX_TASKS; i++)
    {
        if (taskState[i].task != NULL)
        {
            TickType_t nextRelease = taskState[i].lastReleaseTime + pdMS_TO_TICKS(taskState[i].period_ms);

            if ((currentTick == nextRelease) && (currentTick > 0))
            {
                taskState[i].lastReleaseTime = nextRelease;

                /* Log overrun event based on the task's policy */
                if (taskState[i].state == TASK_RUNNING)
                {
                    ev.timestamp = currentTick;
                    ev.taskId = i;
                    ev.extraData = 0;

                    if (taskState[i].policy == POLICY_SKIP)
                    {
                        ev.eventType = LOG_OVERRUN_SKIP;
                        xQueueSendFromISR(logQueue, &ev, &xHigherPriorityTaskWoken);
                    }
                    else if (taskState[i].policy == POLICY_CATCH_UP)
                    {
                        ev.eventType = LOG_OVERRUN_CATCHUP;
                        xQueueSendFromISR(logQueue, &ev, &xHigherPriorityTaskWoken);
                    }
                }
            }
        }
    }
}

/* Task function implementation */
void Task_Function(void *params)
{
    TaskConfig taskConfig = *((TaskConfig *)params);

    TickType_t xLastWakeUpTime;
    TickType_t xLastJobCompleted;
    LogEvent ev;

    const int idTask = taskConfig.idTask;
    const TickType_t xPeriod = pdMS_TO_TICKS(taskConfig.period_ms);
    const TickType_t xDeadline = pdMS_TO_TICKS(taskConfig.deadline);
    const TickType_t xOffset = pdMS_TO_TICKS(taskConfig.offset_ms);

    void (*functionBody)(void *) = taskConfig.taskBody;

    if (xOffset > 0)
    {
        vTaskDelay(xOffset);
    }

    xLastWakeUpTime = 0;

    while (1)
    {
        if (xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdTRUE)
        {
            taskState[idTask].state = TASK_RUNNING;
            taskState[idTask].startTime = xTaskGetTickCount();
            xSemaphoreGive(xSemaphore);

            /* Deferred logging START */
            ev.timestamp = taskState[idTask].startTime;
            ev.taskId = idTask;
            ev.eventType = LOG_START;
            ev.extraData = 0;
            xQueueSend(logQueue, &ev, (TickType_t)0);
        }

        functionBody(taskConfig.params);

        xLastJobCompleted = xTaskGetTickCount();

        /* Deferred logging END */
        ev.timestamp = xLastJobCompleted;
        ev.taskId = idTask;
        ev.eventType = LOG_END;
        ev.extraData = 0;
        xQueueSend(logQueue, &ev, (TickType_t)0);

        if (xSemaphoreTake(xSemaphore, portMAX_DELAY) == pdTRUE)
        {
            taskState[idTask].finishTime = xLastJobCompleted;
            taskState[idTask].state = TASK_NOT_RUNNING;
            taskState[idTask].k++;
            xSemaphoreGive(xSemaphore);
        }

        if (xLastJobCompleted > xLastWakeUpTime + xDeadline)
        {
            /* Deferred logging DEADLINE_MISS */
            ev.timestamp = xLastJobCompleted;
            ev.taskId = idTask;
            ev.eventType = LOG_DEADLINE_MISS;
            ev.extraData = xLastWakeUpTime + xDeadline;
            xQueueSend(logQueue, &ev, (TickType_t)0);
        }

        /* Apply scheduling policy */
        switch (taskState[idTask].policy)
        {
        case POLICY_SKIP:
            PTL_ApplySkipPolicy(&xLastWakeUpTime, xPeriod, xLastJobCompleted);
            break;

        case POLICY_CATCH_UP:
            /* No action needed, task will attempt to catch up in the next iteration. */
            break;

        case POLICY_KILL:
            /* Not implemented yet. */
            break;
        default:
            break;
        }

        /* Delay task until next release time */
        xTaskDelayUntil(&xLastWakeUpTime, xPeriod);
    }
}

void LoggingTask(void *params)
{
    (void)params;
    LogEvent ev;
    char buffer[MESSAGE_LENGTH];

    while (1)
    {
        /* Wait for log events and print them */
        if (xQueueReceive(logQueue, &ev, portMAX_DELAY) == pdPASS)
        {
            const char *name = taskState[ev.taskId].name;

            switch (ev.eventType)
            {
            case LOG_START:
                snprintf(buffer, MESSAGE_LENGTH, "[INFO] t=%lu task=%s START\n", (unsigned long)ev.timestamp, name);
                break;
            case LOG_END:
                snprintf(buffer, MESSAGE_LENGTH, "[INFO] t=%lu task=%s END\n", (unsigned long)ev.timestamp, name);
                break;
            case LOG_DEADLINE_MISS:
                snprintf(buffer, MESSAGE_LENGTH, "[WARN] t=%lu task=%s DEADLINE_MISS dl=%lu\n", (unsigned long)ev.timestamp, name, (unsigned long)ev.extraData);
                break;
            case LOG_OVERRUN_SKIP:
                snprintf(buffer, MESSAGE_LENGTH, "[WARN] t=%lu task=%s OVERRUN -> SKIP\n", (unsigned long)ev.timestamp, name);
                break;
            case LOG_OVERRUN_CATCHUP:
                snprintf(buffer, MESSAGE_LENGTH, "[WARN] t=%lu task=%s OVERRUN -> CATCH_UP\n", (unsigned long)ev.timestamp, name);
                break;
            case LOG_OVERRUN_KILL:
                snprintf(buffer, MESSAGE_LENGTH, "[WARN] t=%lu task=%s OVERRUN -> KILL\n", (unsigned long)ev.timestamp, name);
                break;
            default:
                continue;
            }

            UART_printf(buffer);
        }
    }
}

void Init(const SchedulerConfig sconfig)
{
    int globalPolicy = sconfig.policy;
    //dont save
    sconfig_global = sconfig;

    /* Check if the number of tasks exceeds the maximum */
    if (sconfig.num_tasks > sconfig.max_tasks)
    {
        UART_printf("[ERROR] Number of tasks exceeds maximum.\n");
        while (1)
            ;
    }

    logQueue = xQueueCreate(QUEUE_LENGTH, sizeof(LogEvent));
    xSemaphore = xSemaphoreCreateMutex();

    /* Check if semaphore and queue were created successfully */
    if (xSemaphore == NULL || logQueue == NULL)
    {
        UART_printf("Failed to create RTOS primitives!");
        while (1)
            ;
    }

    /* Create tasks based on the configuration */
    for (int i = 0; i < sconfig.num_tasks; i++)
    {
        TaskConfig *task = (sconfig.tasks + i);

        if (task->deadline <= 0)
        {
            task->deadline = task->period_ms;
        }

        task->idTask = i;

        snprintf((char *)taskState[i].name, sizeof(taskState[i].name), "%s", task->name);
        taskState[i].policy = globalPolicy;
        taskState[i].state = TASK_NOT_RUNNING;
        taskState[i].k = 0;
        taskState[i].period_ms = task->period_ms;
        taskState[i].deadline = task->deadline;
        taskState[i].lastReleaseTime = 0;

        xTaskCreate(Task_Function, task->name, task->stackDepth, task, task->uxPriority, (TaskHandle_t *)&taskState[i].task);
    }

    /* Create logging task */
    xTaskCreate(LoggingTask, "LoggingTask", DEFAULT_STACK_SIZE, NULL, 3, NULL);

    /* Start the scheduler */
    vTaskStartScheduler();
}