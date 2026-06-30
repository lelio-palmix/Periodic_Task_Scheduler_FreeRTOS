#include "ptl.h"
#include "uart.h"
#include <stdio.h>

volatile QueueHandle_t xLogQueue;
volatile QueueHandle_t xOverrunQueue;
TaskState xTaskStates[MAX_TASKS];

inline UBaseType_t uxPtlApplyKillPolicy(volatile TaskConfig *pxTaskConfig, int xTaskId)
{
    pxTaskConfig->ulOffsetMs = 0;
    TaskHandle_t xTask = xTaskStates[xTaskId].xTask;

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

    for (int i = 0; i < MAX_TASKS; i++)
    {
        const TickType_t xPeriod = xTaskStates[i].xPeriod;
        if (xTaskStates[i].xTask != NULL)
        {
            const TickType_t xDeadline = xTaskStates[i].xLastWakeUpTime + xTaskStates[i].xDeadline;

            if (!xTaskStates[i].sLastKDeadlineMiss && xTaskStates[i].eState == TASK_RUNNING && xDeadline <= xCurrentTick)
            {
                xTaskStates[i].sLastKDeadlineMiss = 1;
                LogEvent xEvent;
                xEvent.xTimestamp = xCurrentTick;
                xEvent.xTaskId = i;
                xEvent.eEventType = LOG_DEADLINE_MISS;
                xEvent.ulMissedJob = 0;
                xQueueSendFromISR(xLogQueue, &xEvent, 0);
            }

            const TickType_t xNextRelease = xTaskStates[i].xLastWakeUpTime + xPeriod;
            if (xNextRelease <= xCurrentTick && xTaskStates[i].eState == TASK_RUNNING)
            {
                int xTaskId = i;
                xQueueSendFromISR(xOverrunQueue, &xTaskId, &xHigherPriorityTaskWoken);
            }
        }
    }
}

void vPtlInterruptTask(void *pvParameters)
{
    (void)pvParameters;

    while (1)
    {

        /* Wait for overrun notifications from ISR and apply the corresponding policy */
        int xId;
        if (xQueueReceive(xOverrunQueue, &xId, portMAX_DELAY) == pdTRUE)
        {
            LogEvent xEvent;
            TickType_t xCurrentTick = xTaskGetTickCount();
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
                #if(CATCH_UP_VERSION == 1)
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
            xQueueSend(xLogQueue, &xEvent, (TickType_t)0);
        }
    }
}

/* Task function implementation */
void vPtlTaskBody(void *pvParameters)
{
    TaskConfig xTaskConfig = *((TaskConfig *)pvParameters);
    TickType_t xLastJobCompleted;
    LogEvent xEvent;

    const int xIdTask = xTaskConfig.xIdTask;
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
        xQueueSend(xLogQueue, &xEvent, (TickType_t)0);

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
        xQueueSend(xLogQueue, &xEvent, (TickType_t)0);

        /* Wait until the next release time */
        xTaskDelayUntil(&xTaskStates[xIdTask].xLastWakeUpTime, xPeriod);
    }
}

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
                #if(CATCH_UP_VERSION==1)
                    snprintf(pcBuffer, MESSAGE_LENGTH, "[WARN] t=%lu task=%s OVERRUN -> CATCH_UP job=%lu\n", (unsigned long)xEvent.xTimestamp, pcName,(unsigned long)xEvent.ulMissedJob);
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

void vPtlInit(const SchedulerConfig xSchedulerConfig)
{
    /* Check if the number of tasks exceeds the maximum */
    if (xSchedulerConfig.xNumTasks > xSchedulerConfig.xMaxTasks)
    {
        vUartPrintf("[ERROR] Number of tasks exceeds maximum.\n");
        while (1)
            ;
    }

    xLogQueue = xQueueCreate(QUEUE_LENGTH, sizeof(LogEvent));

    if (xLogQueue == NULL)
    {
        vUartPrintf("[ERROR] Failed to create log queue.\n");
        while (1)
            ;
    }

    /* Overrun queue of length MAX_TASKS because each task can have at most one overrun at a time */
    xOverrunQueue = xQueueCreate(MAX_TASKS, sizeof(int));
    if (xOverrunQueue == NULL)
    {
        vUartPrintf("[ERROR] Failed to create overrun queue.\n");
        while (1)
            ;
    }
    uint8_t ucMaxPriority = 1; // Initialize ucMaxPriority to the lowest priority (1)

    /* Create tasks based on the configuration */
    for (int i = 0; i < xSchedulerConfig.xNumTasks; i++)
    {
        TaskConfig *pxTaskConfig = (xSchedulerConfig.pxTasks + i);

        /* If the deadline is not specified (negative values or 0), the deadline is set to the same value of the period */
        if (pxTaskConfig->ulDeadline <= 0)
        {
            pxTaskConfig->ulDeadline = pxTaskConfig->ulPeriodMs;
        }

        // Priority chosen by the user can be from 1 to 5
        if(pxTaskConfig->uxPriority < BASE_USER_PRIORITY)
        {
           pxTaskConfig->uxPriority = BASE_USER_PRIORITY + 1;
        }else if(pxTaskConfig->uxPriority > MAX_USER_PRIORITY)
        {
            pxTaskConfig->uxPriority = MAX_USER_PRIORITY + 1;
        }else{
            pxTaskConfig->uxPriority = pxTaskConfig->uxPriority +1; // Add 1 to avoid priority of the logging task (1)
        }

        // Update ucMaxPriority if the current task's priority is higher
        if (pxTaskConfig->uxPriority > ucMaxPriority)
        {
            ucMaxPriority = pxTaskConfig->uxPriority;
        }

        pxTaskConfig->xIdTask = i;

        snprintf((char *)xTaskStates[i].pcName, sizeof(xTaskStates[i].pcName), "%s", pxTaskConfig->pcName);
        xTaskStates[i].ePolicy = xSchedulerConfig.ePolicy;
        xTaskStates[i].eState = TASK_NOT_RUNNING;
        xTaskStates[i].ulK = 0;
        xTaskStates[i].xPeriod = pdMS_TO_TICKS(pxTaskConfig->ulPeriodMs);
        xTaskStates[i].xDeadline = pdMS_TO_TICKS(pxTaskConfig->ulDeadline);
        xTaskStates[i].xLastWakeUpTime = pdMS_TO_TICKS(pxTaskConfig->ulOffsetMs);
        xTaskStates[i].xTaskConfig = *pxTaskConfig;
        xTaskStates[i].sLastKDeadlineMiss = 0;

        xTaskCreate(vPtlTaskBody,
                    pxTaskConfig->pcName,
                    pxTaskConfig->usStackDepth,
                    &xTaskStates[i].xTaskConfig,
                    pxTaskConfig->uxPriority,
                    &xTaskStates[i].xTask);
    }

    if(HANDLE_LOG_STARVATION)
    {
        // If HANDLE_LOG_STARVATION is enabled, set the logging task's priority to ucMaxPriority
        xTaskCreate(vPtlLoggingTask, "LoggingTask", configMINIMAL_STACK_SIZE, NULL, ucMaxPriority , NULL);
    }
    else
    {
        // If HANDLE_LOG_STARVATION is disabled, set the logging task's priority to 1
        xTaskCreate(vPtlLoggingTask, "LoggingTask", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    }


    /* Create the interrupt task with a priority higher than the maximum user task priority */
    xTaskCreate(vPtlInterruptTask, "InterruptTask", configMINIMAL_STACK_SIZE, NULL, ucMaxPriority + 1, NULL);

    /* Start the scheduler */
    vTaskStartScheduler();
}
