/**
 * This file introduce a  Periodic Task Layer (PTL)
 * 
 */

#include "ptl.h"


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
    
    int globalPolicy = sconfig.policy;
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
        taskState[i].policy = globalPolicy;
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
