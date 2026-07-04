#include "ptl.h"
#include "uart.h"
#include <stdio.h>

volatile QueueHandle_t xLogQueue;
volatile QueueHandle_t xOverrunQueue;
TaskState xTaskStates[MAX_TASKS];

UBaseType_t uxPtlApplyKillPolicy(volatile TaskConfig *pxTaskConfig, BaseType_t xTaskId)
{
    TaskHandle_t xTask = xTaskStates[xTaskId].xTask;

    pxTaskConfig->ulOffsetMs = 0;

    vTaskDelete(xTask);

    xTaskStates[xTaskId].eState = TASK_NOT_RUNNING;
    xTaskStates[xTaskId].ulK++;

    xTaskCreate(vPtlTaskBody,
                pxTaskConfig->pcName,
                pxTaskConfig->usStackDepth,
                pxTaskConfig,
                pxTaskConfig->uxPriority,
                &xTaskStates[xTaskId].xTask);
    return 1U;
}

/* ISR for tick-level precision checking of deadline misses and overruns. */
void vApplicationTickHook(void)
{
    TickType_t xCurrentTick = xTaskGetTickCountFromISR();
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    BaseType_t x;
    LogEvent xEvent;

    for (x = 0; x < MAX_TASKS; x++)
    {
        const TickType_t xPeriod = xTaskStates[x].xPeriod;

        if (xTaskStates[x].xTask != NULL)
        {
            const TickType_t xDeadline = xTaskStates[x].xLastWakeUpTime + xTaskStates[x].xDeadline;
            const TickType_t xNextRelease = xTaskStates[x].xLastWakeUpTime + xPeriod;

            if (!xTaskStates[x].sLastKDeadlineMiss && xTaskStates[x].eState == TASK_RUNNING && xDeadline <= xCurrentTick)
            {
                xTaskStates[x].sLastKDeadlineMiss = 1;
                xEvent.xTimestamp = xCurrentTick;
                xEvent.xTaskId = x;
                xEvent.eEventType = LOG_DEADLINE_MISS;
                xEvent.ulMissedJob = 0;
                ptlTRACE_EVENT_FROM_ISR(&xEvent, 0);
            }

            if (xNextRelease <= xCurrentTick && xTaskStates[x].eState == TASK_RUNNING)
            {
                xQueueSendFromISR(xOverrunQueue, &x, &xHigherPriorityTaskWoken);
            }
        }
    }
}

void vPtlInterruptTask(void *pvParameters)
{
    BaseType_t xId;
    TickType_t xCurrentTick;
    LogEvent xEvent;

    (void)pvParameters;

    while (1)
    {
        /* Wait for overrun notifications from ISR and apply the corresponding policy */
        if (xQueueReceive(xOverrunQueue, &xId, portMAX_DELAY) == pdTRUE)
        {
            xCurrentTick = xTaskGetTickCount();
            xEvent.xTimestamp = xCurrentTick;
            xEvent.xTaskId = xId;

            xTaskStates[xId].xLastWakeUpTime += xTaskStates[xId].xPeriod;
            xTaskStates[xId].sLastKDeadlineMiss = 0;
            /* Perform the correct policy */
            switch (xTaskStates[xId].ePolicy)
            {
            case POLICY_SKIP:
                xEvent.eEventType = LOG_OVERRUN_SKIP;
                xEvent.ulMissedJob = 0;
                break;
            case POLICY_CATCH_UP:
                xEvent.eEventType = LOG_OVERRUN_CATCHUP;
                xEvent.ulMissedJob = xTaskStates[xId].ulK;
#if (CATCH_UP_VERSION == 1)
                uxPtlApplyKillPolicy(&xTaskStates[xId].xTaskConfig, xId);
#endif
                break;
            case POLICY_KILL:
                xEvent.eEventType = LOG_OVERRUN_KILL;
                xEvent.ulMissedJob = 0;
                uxPtlApplyKillPolicy(&xTaskStates[xId].xTaskConfig, xId);
                break;
            default:
                break;
            }
            ptlTRACE_EVENT(&xEvent);
        }
    }
}

/* Task function implementation */
void vPtlTaskBody(void *pvParameters)
{
    TaskConfig xTaskConfig = *((TaskConfig *)pvParameters);
    TickType_t xLastJobCompleted;
    LogEvent xEvent;

    const BaseType_t xIdTask = xTaskConfig.xIdTask;
    const TickType_t xPeriod = pdMS_TO_TICKS(xTaskConfig.ulPeriodMs);
    const TickType_t xDeadline = pdMS_TO_TICKS(xTaskConfig.ulDeadline);
    const TickType_t xOffset = pdMS_TO_TICKS(xTaskConfig.ulOffsetMs);

    void (*pxTaskBody)(void *) = xTaskConfig.pxTaskBody;

    /* Delay the task if an offset is specified */
    if (xOffset > 0)
        vTaskDelay(xOffset);

    while (1)
    {
        xTaskStates[xIdTask].eState = TASK_RUNNING;
        xTaskStates[xIdTask].xStartTime = xTaskGetTickCount();
        xEvent.xTimestamp = xTaskStates[xIdTask].xStartTime;
        xEvent.xTaskId = xIdTask;
        xEvent.eEventType = LOG_START;
        xEvent.ulMissedJob = 0;
        ptlTRACE_EVENT(&xEvent);

        pxTaskBody(xTaskConfig.pvParams);

        xLastJobCompleted = xTaskGetTickCount();

        /* Update task state for the next period */
        taskENTER_CRITICAL();
        xTaskStates[xIdTask].ulK++;
        xTaskStates[xIdTask].eState = TASK_NOT_RUNNING;
        xTaskStates[xIdTask].xFinishTime = xLastJobCompleted;
        taskEXIT_CRITICAL();
        xEvent.xTimestamp = xLastJobCompleted;
        xEvent.xTaskId = xIdTask;
        xEvent.eEventType = LOG_END;
        xEvent.ulMissedJob = 0;
        ptlTRACE_EVENT(&xEvent);

        /* Wait until the next release time */
        xTaskDelayUntil(&xTaskStates[xIdTask].xLastWakeUpTime, xPeriod);
    }
}

#if (TRACE_ENABLED == 1)
void vPtlLoggingTask(void *pvParameters)
{
    (void)pvParameters;
    LogEvent xEvent;
    char pcBuffer[MESSAGE_LENGTH];

    while (1)
    {
        /* Wait for log events and print them */
        if (xQueueReceive(xLogQueue, &xEvent, portMAX_DELAY) == pdPASS)
        {
            const char *pcName = xTaskStates[xEvent.xTaskId].pcName;

            switch (xEvent.eEventType)
            {
            case LOG_START:
                snprintf(pcBuffer, MESSAGE_LENGTH, "[INFO] t=%lu task=%s START\n", (unsigned long)xEvent.xTimestamp, pcName);
                break;
            case LOG_END:
                snprintf(pcBuffer, MESSAGE_LENGTH, "[INFO] t=%lu task=%s END\n", (unsigned long)xEvent.xTimestamp, pcName);
                break;
            case LOG_DEADLINE_MISS:
                snprintf(pcBuffer, MESSAGE_LENGTH, "[WARN] t=%lu task=%s DEADLINE_MISS\n", (unsigned long)xEvent.xTimestamp, pcName);
                break;
            case LOG_OVERRUN_SKIP:
                snprintf(pcBuffer, MESSAGE_LENGTH, "[WARN] t=%lu task=%s OVERRUN -> SKIP\n",
                         (unsigned long)xEvent.xTimestamp, pcName);
                break;
            case LOG_OVERRUN_CATCHUP:
#if (CATCH_UP_VERSION == 1)
                snprintf(pcBuffer, MESSAGE_LENGTH, "[WARN] t=%lu task=%s OVERRUN -> CATCH_UP job=%lu\n", (unsigned long)xEvent.xTimestamp, pcName, (unsigned long)xEvent.ulMissedJob);
#else
                snprintf(pcBuffer, MESSAGE_LENGTH, "[WARN] t=%lu task=%s OVERRUN -> CATCH_UP\n", (unsigned long)xEvent.xTimestamp, pcName);
#endif

                break;
            case LOG_OVERRUN_KILL:
                snprintf(pcBuffer, MESSAGE_LENGTH, "[WARN] t=%lu task=%s OVERRUN -> KILL\n", (unsigned long)xEvent.xTimestamp, pcName);
                break;
            default:
                continue;
            }

            vUartPrintf(pcBuffer);
        }
    }
}
#endif /* TRACE_ENABLED == 1 */

void vPtlInit(const SchedulerConfig xSchedulerConfig)
{
    UBaseType_t uxMaxPriority = 1; /* Highest priority seen so far; starts at the lowest (1) */
    BaseType_t x;

    /* Check if the number of tasks exceeds the maximum */
    if (xSchedulerConfig.xNumTasks > xSchedulerConfig.xMaxTasks)
    {
        vUartPrintf("[ERROR] Number of tasks exceeds maximum.\n");
        while (1)
            ;
    }

#if (TRACE_ENABLED == 1)
    xLogQueue = xQueueCreate(QUEUE_LENGTH, sizeof(LogEvent));

    if (xLogQueue == NULL)
    {
        vUartPrintf("[ERROR] Failed to create log queue.\n");
        while (1)
            ;
    }
#endif

    /* Overrun queue of length MAX_TASKS because each task can have at most one overrun at a time */
    xOverrunQueue = xQueueCreate(MAX_TASKS, sizeof(BaseType_t));
    if (xOverrunQueue == NULL)
    {
        vUartPrintf("[ERROR] Failed to create overrun queue.\n");
        while (1)
            ;
    }

    /* Create tasks based on the configuration */
    for (x = 0; x < xSchedulerConfig.xNumTasks; x++)
    {
        TaskConfig *pxTaskConfig = (xSchedulerConfig.pxTasks + x);

        /* Check if the task period is valid */
        if (pxTaskConfig->ulPeriodMs <= 0)
        {
            vUartPrintf("[ERROR] Task period must be greater than 0.\n");
            while (1)
                ;
        }

        /* If the deadline is less than 0 or greater than period, it is set to the same value of the period */
        if (pxTaskConfig->ulDeadline <= 0 || pxTaskConfig->ulDeadline > pxTaskConfig->ulPeriodMs)
        {
            pxTaskConfig->ulDeadline = pxTaskConfig->ulPeriodMs;
        }

        /* Priority chosen by the user can be from 1 to 5 */
        if (pxTaskConfig->uxPriority < BASE_USER_PRIORITY)
        {
            pxTaskConfig->uxPriority = BASE_USER_PRIORITY + 1;
        }
        else if (pxTaskConfig->uxPriority > MAX_USER_PRIORITY)
        {
            pxTaskConfig->uxPriority = MAX_USER_PRIORITY + 1;
        }
        else
        {
            pxTaskConfig->uxPriority = pxTaskConfig->uxPriority + 1; /* Add 1 to avoid priority of the logging task (1) */
        }

        /* Update uxMaxPriority if the current task's priority is higher */
        if (pxTaskConfig->uxPriority > uxMaxPriority)
        {
            uxMaxPriority = pxTaskConfig->uxPriority;
        }

        pxTaskConfig->xIdTask = x;

        snprintf((char *)xTaskStates[x].pcName, sizeof(xTaskStates[x].pcName), "%s", pxTaskConfig->pcName);
        xTaskStates[x].ePolicy = xSchedulerConfig.ePolicy;
        xTaskStates[x].eState = TASK_NOT_RUNNING;
        xTaskStates[x].ulK = 0;
        xTaskStates[x].xPeriod = pdMS_TO_TICKS(pxTaskConfig->ulPeriodMs);
        xTaskStates[x].xDeadline = pdMS_TO_TICKS(pxTaskConfig->ulDeadline);
        xTaskStates[x].xLastWakeUpTime = pdMS_TO_TICKS(pxTaskConfig->ulOffsetMs);
        xTaskStates[x].xTaskConfig = *pxTaskConfig;
        xTaskStates[x].sLastKDeadlineMiss = 0;

        xTaskCreate(vPtlTaskBody,
                    pxTaskConfig->pcName,
                    pxTaskConfig->usStackDepth,
                    &xTaskStates[x].xTaskConfig,
                    pxTaskConfig->uxPriority,
                    &xTaskStates[x].xTask);
    }

#if (TRACE_ENABLED == 1)
    if (HANDLE_LOG_STARVATION)
    {
        /* If HANDLE_LOG_STARVATION is enabled, set the logging task's priority to uxMaxPriority */
        xTaskCreate(vPtlLoggingTask, "LoggingTask", configMINIMAL_STACK_SIZE, NULL, uxMaxPriority, NULL);
    }
    else
    {
        /* If HANDLE_LOG_STARVATION is disabled, set the logging task's priority to 1 */
        xTaskCreate(vPtlLoggingTask, "LoggingTask", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    }
#endif

    /* Create the interrupt task with a priority higher than the maximum user task priority */
    xTaskCreate(vPtlInterruptTask, "InterruptTask", configMINIMAL_STACK_SIZE, NULL, uxMaxPriority + 1, NULL);

    /* Start the scheduler */
    vTaskStartScheduler();
}
