#include "ptl.h"
#include "uart.h"
#include <stdio.h>

volatile QueueHandle_t logQueue;
volatile QueueHandle_t overrunQueue;
TaskState taskState[MAX_TASKS];

inline UBaseType_t PTL_ApplyKillPolicy(volatile TaskConfig *taskConfig, int taskId)
{
    taskConfig->offset_ms = 0;
    TaskHandle_t task = taskState[taskId].task;

    vTaskDelete(task);

    taskState[taskId].state = TASK_NOT_RUNNING;
    taskState[taskId].k++;

    xTaskCreate(Task_Function,
                taskConfig->name,
                taskConfig->stackDepth,
                taskConfig,
                taskConfig->uxPriority,
                &taskState[taskId].task);
    return 1U;
}

/* ISR for tick-level precision checking of deadline misses and overruns. */
void vApplicationTickHook(void)
{

    TickType_t currentTick = xTaskGetTickCountFromISR();

    BaseType_t contextSwitch = pdFALSE;

    for (int i = 0; i < MAX_TASKS; i++)
    {
        const TickType_t period = taskState[i].period;
        if (taskState[i].task != NULL)
        {
            const TickType_t deadline = taskState[i].xLastWakeUpTime + taskState[i].deadline;

            if (!taskState[i].lastKDeadlineMiss && taskState[i].state == TASK_RUNNING && deadline <= currentTick)
            {
                taskState[i].lastKDeadlineMiss = 1;
                LogEvent ev;
                ev.timestamp = currentTick;
                ev.taskId = i;
                ev.eventType = LOG_DEADLINE_MISS;
                ev.missed_job = 0;
                xQueueSendFromISR(logQueue, &ev, 0);
            }

            const TickType_t nextRelease = taskState[i].xLastWakeUpTime + period;
            if (nextRelease <= currentTick && taskState[i].state == TASK_RUNNING)
            {
                int taskId = i;
                xQueueSendFromISR(overrunQueue, &taskId, &contextSwitch);
            }
        }
    }
}

void Interrupt_task(void *params)
{
    (void)params;

    while (1)
    {

        /* Wait for overrun notifications from ISR and apply the corresponding policy */
        int id;
        if (xQueueReceive(overrunQueue, &id, portMAX_DELAY) == pdTRUE)
        {
            LogEvent ev;
            TickType_t currentTick = xTaskGetTickCount();
            ev.timestamp = currentTick;
            ev.taskId = id;

            taskState[id].xLastWakeUpTime += taskState[id].period;
            taskState[id].lastKDeadlineMiss = 0;
            /* Perform the correct policy */
            switch (taskState[id].policy)
            {
            case POLICY_SKIP:
                ev.eventType = LOG_OVERRUN_SKIP;
                ev.missed_job = 0;
                break;
            case POLICY_CATCH_UP:
                ev.eventType = LOG_OVERRUN_CATCHUP;
                ev.missed_job = taskState[id].k;
                #if(CATCH_UP_VERSION == 1)
                    PTL_ApplyKillPolicy(&taskState[id].taskConfig, id);
                #endif
                break;
            case POLICY_KILL:
                ev.eventType = LOG_OVERRUN_KILL;
                ev.missed_job = 0;
                PTL_ApplyKillPolicy(&taskState[id].taskConfig, id);
                break;
            default:
                break;
            }
            xQueueSend(logQueue, &ev, (TickType_t)0);
        }
    }
}

/* Task function implementation */
void Task_Function(void *params)
{
    TaskConfig taskConfig = *((TaskConfig *)params);
    TickType_t xLastJobCompleted;
    LogEvent ev;

    const int idTask = taskConfig.idTask;
    const TickType_t xPeriod = pdMS_TO_TICKS(taskConfig.period_ms);
    const TickType_t xDeadline = pdMS_TO_TICKS(taskConfig.deadline);
    const TickType_t xOffset = pdMS_TO_TICKS(taskConfig.offset_ms);

    void (*functionBody)(void *) = taskConfig.taskBody;

    /* Delay the task if an offset is specified */
    if (xOffset > 0)
        vTaskDelay(xOffset);

    while (1)
    {
        taskState[idTask].state = TASK_RUNNING;
        taskState[idTask].startTime = xTaskGetTickCount();
        ev.timestamp = taskState[idTask].startTime;
        ev.taskId = idTask;
        ev.eventType = LOG_START;
        ev.missed_job = 0;
        xQueueSend(logQueue, &ev, (TickType_t)0);

        functionBody(taskConfig.params);

        xLastJobCompleted = xTaskGetTickCount();



        /* Update task state for the next period */
        taskENTER_CRITICAL();
        taskState[idTask].k++;
        taskState[idTask].state = TASK_NOT_RUNNING;
        taskState[idTask].finishTime = xLastJobCompleted;
        taskEXIT_CRITICAL();
        ev.timestamp = xLastJobCompleted;
        ev.taskId = idTask;
        ev.eventType = LOG_END;
        ev.missed_job = 0;
        xQueueSend(logQueue, &ev, (TickType_t)0);

        /* Wait until the next release time */
        xTaskDelayUntil(&taskState[idTask].xLastWakeUpTime, xPeriod);
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
                snprintf(buffer, MESSAGE_LENGTH, "[WARN] t=%lu task=%s DEADLINE_MISS\n", (unsigned long)ev.timestamp, name);
                break;
            case LOG_OVERRUN_SKIP:
                snprintf(buffer, MESSAGE_LENGTH, "[WARN] t=%lu task=%s OVERRUN -> SKIP\n",
                         (unsigned long)ev.timestamp, name);
                break;
            case LOG_OVERRUN_CATCHUP:
                #if(CATCH_UP_VERSION==1)
                    snprintf(buffer, MESSAGE_LENGTH, "[WARN] t=%lu task=%s OVERRUN -> CATCH_UP job=%lu\n", (unsigned long)ev.timestamp, name,(unsigned long)ev.missed_job);
                #else
                    snprintf(buffer, MESSAGE_LENGTH, "[WARN] t=%lu task=%s OVERRUN -> CATCH_UP\n", (unsigned long)ev.timestamp, name);
                #endif

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
    /* Check if the number of tasks exceeds the maximum */
    if (sconfig.num_tasks > sconfig.max_tasks)
    {
        UART_printf("[ERROR] Number of tasks exceeds maximum.\n");
        while (1)
            ;
    }

    logQueue = xQueueCreate(QUEUE_LENGTH, sizeof(LogEvent));

    if (logQueue == NULL)
    {
        UART_printf("[ERROR] Failed to create log queue.\n");
        while (1)
            ;
    }

    /* Overrun queue of length MAX_TASKS because each task can have at most one overrun at a time */
    overrunQueue = xQueueCreate(MAX_TASKS, sizeof(int));
    if (overrunQueue == NULL)
    {
        UART_printf("[ERROR] Failed to create overrun queue.\n");
        while (1)
            ;
    }

    /* Create tasks based on the configuration */
    for (int i = 0; i < sconfig.num_tasks; i++)
    {
        TaskConfig *task = (sconfig.tasks + i);

        /* If the deadline is not specified (negative values or 0), the deadline is set to the same value of the period */
        if (task->deadline <= 0)
        {
            task->deadline = task->period_ms;
        }

        task->idTask = i;

        snprintf((char *)taskState[i].name, sizeof(taskState[i].name), "%s", task->name);
        taskState[i].policy = sconfig.policy;
        taskState[i].state = TASK_NOT_RUNNING;
        taskState[i].k = 0;
        taskState[i].period = pdMS_TO_TICKS(task->period_ms);
        taskState[i].deadline = pdMS_TO_TICKS(task->deadline);
        taskState[i].xLastWakeUpTime = pdMS_TO_TICKS(task->offset_ms);
        taskState[i].taskConfig = *task;
        taskState[i].lastKDeadlineMiss = 0;

        xTaskCreate(Task_Function,
                    task->name,
                    task->stackDepth,
                    task,
                    task->uxPriority,
                    &taskState[i].task);
    }

    /* Create logging task */
    xTaskCreate(LoggingTask, "LoggingTask", DEFAULT_STACK_SIZE, NULL, 2, NULL);

    /* Create interrupt task */
    xTaskCreate(Interrupt_task, "InterruptTask", DEFAULT_STACK_SIZE, NULL, 4, NULL);

    /* Start the scheduler */
    vTaskStartScheduler();
}