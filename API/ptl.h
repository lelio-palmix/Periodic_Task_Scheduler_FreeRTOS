/* Periodic Task Library (PTL) API header file
   This header defines the interface for the Periodic Task Library (PTL), which provides
   a framework for creating and managing periodic tasks in a FreeRTOS environment. It includes
   definitions for task configuration, state management, and scheduling policies.
   The PTL allows users to specify task parameters such as name, priority, period, and deadline,
   and supports different scheduling policies (SKIP, KILL, CATCH_UP) to handle task overruns.
   The library also includes a logging mechanism using a FreeRTOS queue to ensure
   thread-safe delivery of trace events.
*/
#ifndef PTL_H
#define PTL_H

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* Configuration constants */
#define QUEUE_LENGTH 30
#define MESSAGE_LENGTH 60
#define MAX_TASKS 8

/* User-defined constants for task priorities */
#define BASE_USER_PRIORITY 1
#define MAX_USER_PRIORITY 5

/* 0 = Do not handle log starvation
** 1 = Handle log starvation
** Can be overridden at compile time via -DHANDLE_LOG_STARVATION=0/1 */
#ifndef HANDLE_LOG_STARVATION
#define HANDLE_LOG_STARVATION 1
#endif

/* 0 = Just log and change xLastWakeUpTime (pretty much like SKIP)
** 1 = Kill the current job and release the new one, then log the previous job as missed (similar to KILL with different log)
** Can be overridden at compile time via -DCATCH_UP_VERSION=0/1 */
#ifndef CATCH_UP_VERSION
#define CATCH_UP_VERSION 0
#endif

/* 0 = Tracing disabled: no trace events are queued or printed on the UART,
**     and the log queue and logging task are not created.
** 1 = Tracing enabled
** Can be overridden at compile time via -DTRACE_ENABLED=0/1 */
#ifndef TRACE_ENABLED
#define TRACE_ENABLED 1
#endif

/* Trace macros: forward a LogEvent to the log queue when tracing is enabled;
   compile to no-ops when disabled. */
#if (TRACE_ENABLED == 1)
#define ptlTRACE_EVENT(pxEvent) xQueueSend(xLogQueue, (pxEvent), (TickType_t)0)
#define ptlTRACE_EVENT_FROM_ISR(pxEvent, pxWoken) xQueueSendFromISR(xLogQueue, (pxEvent), (pxWoken))
#else
#define ptlTRACE_EVENT(pxEvent) ((void)(pxEvent))
#define ptlTRACE_EVENT_FROM_ISR(pxEvent, pxWoken) ((void)(pxEvent), (void)(pxWoken))
#endif

/* Task running state: whether a job is currently executing or not. */
typedef enum
{
    TASK_RUNNING = 1,
    TASK_NOT_RUNNING = 2
} TaskRunningState;

/* Task scheduling policy */
typedef enum
{
    POLICY_SKIP = 0, /* Skip the pending release; the late job runs to completion */
    POLICY_KILL,     /* Terminate the late job immediately and release a fresh one */
    POLICY_CATCH_UP  /* Let the late job finish, then release the next one immediately */
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
    TickType_t xTimestamp;   /* Tick at which the event occurred */
    int xTaskId;             /* Index of the task in xTaskStates[] to which the event relates */
    LogEventType eEventType; /* Type of the event (START, END, DEADLINE_MISS, etc.) */
    uint32_t ulMissedJob;    /* Missed job to log when CATCH-UP policy is enabled */
} LogEvent;

/* Task configuration structure */
typedef struct
{
    char pcName[configMAX_TASK_NAME_LEN]; /* Human-readable task name */
    int xIdTask;                          /* Task index, assigned by the PTL during vPtlInit */
    void (*pxTaskBody)(void *);           /* User job body, executed once per release */
    void *pvParams;                       /* Argument passed to pxTaskBody on each invocation */
    uint16_t usStackDepth;                /* Stack depth (in words) for the task */
    UBaseType_t uxPriority;               /* FreeRTOS priority of the task */
    uint32_t ulPeriodMs;                  /* Period T, in milliseconds */
    uint32_t ulDeadline;                  /* Relative deadline D, in milliseconds.
                                             If <= 0, the PTL sets D = ulPeriodMs */
    uint32_t ulOffsetMs;                  /* Initial phase/offset before the first release */
} TaskConfig;

/* Task state structure. One instance per task, held in the global xTaskStates[] array. */
typedef struct
{
    TaskHandle_t xTask;                   /* FreeRTOS handle of the running task */
    char pcName[configMAX_TASK_NAME_LEN]; /* Cached task name */
    TaskConfig xTaskConfig;               /* Copy of the task's configuration */
    TaskRunningState eState;              /* Whether a job is currently running */
    TaskPolicy ePolicy;                   /* Overrun policy applied to this task */
    TickType_t xStartTime;                /* Tick at which the current job started */
    TickType_t xFinishTime;               /* Tick at which the last job finished */
    TickType_t xLastWakeUpTime;           /* Reference release time for xTaskDelayUntil */
    TickType_t xPeriod;                   /* Period T, in ticks */
    TickType_t xDeadline;                 /* Relative deadline D, in ticks */
    uint32_t ulK;                         /* Index of the current job (release counter) */
    short sLastKDeadlineMiss;             /* Job index for which a deadline miss was
                                             last logged, to avoid duplicate miss logs */
} TaskState;

/* Scheduler configuration structure: the top-level configuration passed to
   vPtlInit, listing all periodic tasks and global scheduler settings. */
typedef struct
{
    TaskPolicy ePolicy;  /* Global overrun policy applied to all tasks */
    int xMaxTasks;       /* Maximum number of tasks allowed */
    TaskConfig *pxTasks; /* Array of task configurations */
    int xNumTasks;       /* Number of valid entries in pxTasks[] */
} SchedulerConfig;

/* Global variables for logging and task state management */
extern volatile QueueHandle_t xLogQueue;     /* Queue carrying LogEvent records */
extern volatile QueueHandle_t xOverrunQueue; /* Queue carrying overrun task IDs */
extern TaskState xTaskStates[MAX_TASKS];     /* Per-task runtime state */

/*
 * Initialize the PTL scheduler with the given configuration.
 * Parameters:
 * - xSchedulerConfig: A SchedulerConfig structure containing the scheduling policy,
 *            task configurations, and other settings.
 * This function creates the necessary FreeRTOS tasks (periodic tasks, the
 * logging task and the interrupt task) and starts the scheduler. It does not
 * return under normal operation.
 */
void vPtlInit(const SchedulerConfig xSchedulerConfig);

/*
 * Apply the KILL policy when an overrun is detected.
 * Parameters:
 * - pxTaskConfig: Pointer to the configuration of the overrunning task; the task
 *               is deleted and re-created from this configuration.
 * - xTaskId: Index of the task in the xTaskStates[] array.
 * Returns 1 (the number of jobs discarded by the kill).
 */
UBaseType_t uxPtlApplyKillPolicy(volatile TaskConfig *pxTaskConfig, int xTaskId);

/*
 * Wrapper executed as the body of every periodic task. It handles release
 * timing, START/END logging, deadline-miss detection and the overrun policy.
 * Parameters:
 * - pvParameters: Pointer to the task's TaskConfig.
 */
void vPtlTaskBody(void *pvParameters);

#endif /* PTL_H */
