#include "ptl.h"
#include "uart.h"
#include <stdio.h>

volatile QueueHandle_t logQueue;
volatile SemaphoreHandle_t xSemaphore;
TaskState taskState[MAX_TASKS];
volatile TaskHandle_t interruptTaskHandler;


inline BaseType_t PTL_IsOverrun(TickType_t xLastWakeUpTime, TickType_t xPeriod, TickType_t xNow)
{
    TickType_t xNextRelease = xLastWakeUpTime + xPeriod;
    return (xNextRelease <= xNow) ? pdTRUE : pdFALSE; // change < with <=
}

inline UBaseType_t PTL_ApplySkipPolicy()
{
    //TODO: Understand what to do here
    //UBaseType_t skippedReleases = 0U;

    //*xLastWakeUpTime += xPeriod;
    //skippedReleases++;
    //return skippedReleases;
    return 1;
}

inline UBaseType_t PTL_ApplyKillPolicy(volatile TaskConfig *taskConfig, int taskId)
{
    TaskHandle_t task = taskState[taskId].task;

    vTaskDelete(task);
    //*xLastWakeUpTime = *xLastWakeUpTime + xPeriod;
    xTaskCreate(Task_Function, taskConfig->name, taskConfig->stackDepth, taskConfig, taskConfig->uxPriority,(TaskHandle_t *) &taskState[taskId].task);
    return 1U;
}
/* ISR for tick-level precision checking of period overruns. */
void vApplicationTickHook(void)
{
    
    TickType_t currentTick = xTaskGetTickCountFromISR();

    // LogEvent ev;
    BaseType_t contextSwitch = pdFALSE;

    for (int i = 0; i < MAX_TASKS; i++)
    {
        TickType_t period = pdMS_TO_TICKS(taskState[i].period);
        if (taskState[i].task != NULL)
        {

            const TickType_t deadline = taskState[i].xLastWakeUpTime + taskState[i].deadline;
            if ( taskState[i].lastKDeadlineMiss != taskState[i].k && taskState[i].state == TASK_RUNNING && deadline > currentTick)
            {
                taskState[i].lastKDeadlineMiss = taskState[i].k;
                /* Deferred logging DEADLINE_MISS */
                LogEvent ev;
                ev.timestamp = currentTick;
                ev.taskId = i;
                ev.eventType = LOG_DEADLINE_MISS;
                ev.extraData = 0;
                xQueueSend(logQueue, &ev, 0);
            }

            TickType_t nextRelease = taskState[i].xLastWakeUpTime + period;
            if (nextRelease  <= currentTick && taskState[i].state == TASK_RUNNING)
            {
                taskState[i].xLastWakeUpTime = nextRelease;

                /* Log overrun event based on the task's policy */

                /*
                        This function notify from the isr to the task that it should be wake up and
                        send to the task one value that it can be overwrite (eSetValueWithOverwrite)
                */
                xTaskNotifyFromISR(interruptTaskHandler, i, eSetValueWithOverwrite, &contextSwitch);
            }
        }
    }
    //Force context switch
    //portYIELD_FROM_ISR(contextSwitch);
}

void Interrupt_task(void *params){
    (void)params;

    while(1){
        
        uint32_t ulReceivedValue;
        if( xTaskNotifyWait( 
                0x00,             
                0xFFFFFFFF,       
                &ulReceivedValue, 
                portMAX_DELAY     
            ) == pdTRUE )
        {
            int id = (int)ulReceivedValue;
            LogEvent ev;
            TickType_t currentTick = xTaskGetTickCount();
            ev.timestamp = currentTick;
            ev.taskId = id;
            ev.extraData = 0;
            /* Perform the correct policy */
            switch (taskState[id].policy)
            {
            case POLICY_SKIP:
                ev.eventType = LOG_OVERRUN_SKIP;
                PTL_ApplySkipPolicy();
                break;
            case POLICY_CATCH_UP:
                ev.eventType = LOG_OVERRUN_CATCHUP;
                /* Do nothing */   
                break;
            case POLICY_KILL:
                ev.eventType = LOG_OVERRUN_KILL;
                
                PTL_ApplyKillPolicy(&taskState[id].taskConfig,id);
                break;               
            default: break;    
            }

            xQueueSend(logQueue, &ev, (TickType_t)0);
        }
    }

}

/* Task function implementation */
void Task_Function(void *params)
{
    TaskConfig taskConfig = *((TaskConfig *)params);

    //TickType_t xLastWakeUpTime;
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

    //xLastWakeUpTime = 0;

    while (1)
    {
        //LA nuova task non prende il semaforo
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

        if (xLastJobCompleted > taskState[idTask].xLastWakeUpTime + xDeadline)
        {
            /* Deferred logging DEADLINE_MISS */
            ev.timestamp = xLastJobCompleted;
            ev.taskId = idTask;
            ev.eventType = LOG_DEADLINE_MISS;
            ev.extraData = taskState[idTask].xLastWakeUpTime + xDeadline;
            xQueueSend(logQueue, &ev, (TickType_t)0);
        }
        /* Delay task until next release time */
        xTaskDelayUntil(&taskState[idTask].xLastWakeUpTime, xPeriod);
    }
}

/* Task function implementation */
void Task_Function_critical_section(void *params)
{
    TaskConfig taskConfig = *((TaskConfig *)params);

    //TickType_t xLastWakeUpTime;
    TickType_t xLastJobCompleted;
    LogEvent ev;

    const int idTask = taskConfig.idTask;
    const TickType_t xPeriod = pdMS_TO_TICKS(taskConfig.period_ms);
    const TickType_t xOffset = pdMS_TO_TICKS(taskConfig.offset_ms);

    void (*functionBody)(void *) = taskConfig.taskBody;

    if (xOffset > 0)
    {
        vTaskDelay(xOffset);
    }

    //xLastWakeUpTime = 0;

    while (1)
    {
        //Modifica per utilizzare la critical section invece del semaforo
        
        taskENTER_CRITICAL();

        taskState[idTask].state = TASK_RUNNING;
        taskState[idTask].startTime = xTaskGetTickCount();

        /* Deferred logging START */
        ev.timestamp = taskState[idTask].startTime;
        ev.taskId = idTask;
        ev.eventType = LOG_START;
        ev.extraData = 0;
        xQueueSend(logQueue, &ev, (TickType_t)0);
        taskEXIT_CRITICAL();
    

        functionBody(taskConfig.params);

        xLastJobCompleted = xTaskGetTickCount();

        /* Deferred logging END */
        taskENTER_CRITICAL();
        ev.timestamp = xLastJobCompleted;
        ev.taskId = idTask;
        ev.eventType = LOG_END;
        ev.extraData = 0;
        xQueueSend(logQueue, &ev, (TickType_t)0);
        
        taskState[idTask].finishTime = xLastJobCompleted;
        taskState[idTask].state = TASK_NOT_RUNNING;
        taskState[idTask].k++;
        taskEXIT_CRITICAL();

        /* Delay task until next release time */
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
                snprintf(buffer, MESSAGE_LENGTH, "[WARN] t=%lu task=%s DEADLINE_MISS", (unsigned long)ev.timestamp, name);
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


        //if the deadline is not specified(negative values or 0), the deadline is set to the same value of the period
        if (task->deadline <= 0)
        {
            task->deadline = task->period_ms;
        }

        task->idTask = i;

        snprintf((char *)taskState[i].name, sizeof(taskState[i].name), "%s", task->name);
        taskState[i].policy = globalPolicy;
        taskState[i].state = TASK_NOT_RUNNING;
        taskState[i].k = 0;
        taskState[i].period = pdMS_TO_TICKS(task->period_ms);
        taskState[i].deadline = pdMS_TO_TICKS(task->deadline);
        taskState[i].xLastWakeUpTime = pdMS_TO_TICKS(task->offset_ms);
        taskState[i].xLastWakeUpTime = pdMS_TO_TICKS(task->offset_ms);
        taskState[i].taskConfig = *task;
        taskState[i].lastKDeadlineMiss = -1;

        xTaskCreate(Task_Function, task->name, task->stackDepth, task, task->uxPriority, (TaskHandle_t *)&taskState[i].task);
    }

    /* Create logging task */
    xTaskCreate(LoggingTask, "LoggingTask", DEFAULT_STACK_SIZE, NULL, 3, NULL);
    
    xTaskCreate(Interrupt_task, "InterruptTask", DEFAULT_STACK_SIZE, NULL, 4, (TaskHandle_t *)&interruptTaskHandler);

    /* Start the scheduler */
    vTaskStartScheduler();
}