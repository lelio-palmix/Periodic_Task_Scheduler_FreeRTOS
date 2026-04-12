/* Periodic Task Library (PTL) API header file

   This header defines the interface for the Periodic Task Library (PTL), which provides
   a framework for creating and managing periodic tasks in a FreeRTOS environment. It includes
   definitions for task configuration, state management, and scheduling policies.

   The PTL allows users to specify task parameters such as name, priority, period, and deadline,
   and supports different scheduling policies (SKIP, KILL, CATCH_UP) to handle task overruns.

   The library also includes a logging mechanism using FreeRTOS queues and semaphores to ensure
   thread-safe access to shared resources.
*/

#ifndef PTL_H
#define PTL_H

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/* Configuration constants */
#define QUEUE_LENGTH 15
#define MESSAGE_LENGTH 60
#define DEFAULT_STACK_SIZE 512
#define MAX_TASKS 8

/* Task running state */
typedef enum
{
    TASK_RUNNING = 1,
    TASK_NOT_RUNNING = 2
} TaskRunningState;

/* Task scheduling policy */
typedef enum
{
    POLICY_SKIP = 0,
    POLICY_KILL,
    POLICY_CATCH_UP
} TaskPolicy;

/* Log event types */
typedef enum
{
    LOG_START,
    LOG_END,
    LOG_DEADLINE_MISS,
    LOG_OVERRUN_SKIP,
    LOG_OVERRUN_CATCHUP,
    LOG_OVERRUN_KILL
} LogEventType;

/* Log event structure */
typedef struct
{
    TickType_t timestamp;
    int taskId;
    LogEventType eventType;
    TickType_t extraData; // Used to store deadline miss time or number of skipped releases
} LogEvent;

/* Task configuration structure */
typedef struct
{
    char name[configMAX_TASK_NAME_LEN];
    int idTask;
    void (*taskBody)(void *);
    void *params;
    uint16_t stackDepth;
    UBaseType_t uxPriority;
    uint32_t period_ms;
    uint32_t deadline;
    uint32_t offset_ms;
} TaskConfig;

/* Task state structure */
typedef struct
{
    TaskHandle_t task;
    char name[configMAX_TASK_NAME_LEN];
    TaskRunningState state;
    TaskPolicy policy;
    TickType_t startTime;
    TickType_t finishTime;
    TickType_t lastReleaseTime; // used by TickHook to track expected releases
    uint32_t period_ms;
    uint32_t deadline;
    uint32_t k;
} TaskState;

/* Scheduler configuration structure */
typedef struct
{
    TaskPolicy policy;
    int trace_enabled;
    int max_tasks;
    TaskConfig *tasks;
    int num_tasks;
} SchedulerConfig;

/* Global variables for logging and task state management */
extern volatile QueueHandle_t logQueue;
extern volatile SemaphoreHandle_t xSemaphore;
extern volatile TaskState taskState[MAX_TASKS];

/*
 * Function to initialize the PTL scheduler with the given configuration.
 * Parameters:
 * - sconfig: A SchedulerConfig structure containing the scheduling policy, task configurations, and other settings.
 * This function creates the necessary FreeRTOS tasks and starts the scheduler.
 */
void Init(const SchedulerConfig sconfig);

/*
 * Function to check if a task has overrun its next release time.
 * Parameters:
 * - xLastWakeUpTime: The last time the task was released.
 * - xPeriod: The period of the task in ticks.
 * - xNow: The current tick count.
 * Returns pdTRUE if an overrun has occurred, otherwise pdFALSE.
 */
BaseType_t PTL_IsOverrun(TickType_t xLastWakeUpTime, TickType_t xPeriod, TickType_t xNow);

/*
 * Function to apply the SKIP policy when an overrun is detected.
 * Parameters:
 * - xLastWakeUpTime: Pointer to the last wake-up time of the task, which will be updated.
 * - xPeriod: The period of the task in ticks.
 * - xNow: The current tick count.
 * Returns the number of releases that were skipped due to the overrun.
 */
UBaseType_t PTL_ApplySkipPolicy(TickType_t *xLastWakeUpTime, TickType_t xPeriod, TickType_t xNow);

#endif