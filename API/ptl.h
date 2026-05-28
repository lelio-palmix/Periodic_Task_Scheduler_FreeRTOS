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
#define DEFAULT_STACK_SIZE 512
#define MAX_TASKS 8


// 0 = just log and change last wake-up time(pretty much like skip)
// 1 = kill the current job and release the new one and log the previous job as missed(pretty much like kill with different log)
#define CATCH_UP_VERSION 0

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
    TickType_t timestamp;   /* Tick at which the event occurred */
    int taskId;             /* Index of the task in taskState[] to which the event relates */
    LogEventType eventType; /* Type of the event (START, END, DEADLINE_MISS, etc.) */
    int missed_job;    /* Missed job to log when CATCH-UP policy is enabled */
} LogEvent;

/* Task configuration structure */
typedef struct
{
    char name[configMAX_TASK_NAME_LEN]; /* Human-readable task name */
    int idTask;                         /* Task index, assigned by the PTL during Init */
    void (*taskBody)(void *);           /* User job body, executed once per release */
    void *params;                       /* Argument passed to taskBody on each invocation */
    uint16_t stackDepth;                /* Stack depth (in words) for the task */
    UBaseType_t uxPriority;             /* FreeRTOS priority of the task */
    uint32_t period_ms;                 /* Period T, in milliseconds */
    uint32_t deadline;                  /* Relative deadline D, in milliseconds.
                                           If <= 0, the PTL sets D = period_ms */
    uint32_t offset_ms;                 /* Initial phase/offset before the first release */
} TaskConfig;

/* Task state structure. One instance per task, held in the global taskState[] array. */
typedef struct
{
    TaskHandle_t task;                  /* FreeRTOS handle of the running task */
    char name[configMAX_TASK_NAME_LEN]; /* Cached task name */
    TaskConfig taskConfig;              /* Copy of the task's configuration */
    TaskRunningState state;             /* Whether a job is currently running */
    TaskPolicy policy;                  /* Overrun policy applied to this task */
    TickType_t startTime;               /* Tick at which the current job started */
    TickType_t finishTime;              /* Tick at which the last job finished */
    TickType_t xLastWakeUpTime;         /* Reference release time for xTaskDelayUntil */
    TickType_t period;                  /* Period T, in ticks */
    TickType_t deadline;                /* Relative deadline D, in ticks */
    uint32_t k;                         /* Index of the current job (release counter) */
    uint32_t lastKDeadlineMiss;         /* Job index for which a deadline miss was
                                           last logged, to avoid duplicate miss logs */
} TaskState;

/* Scheduler configuration structure: the top-level configuration passed to
   Init, listing all periodic tasks and global scheduler settings. */
typedef struct
{
    TaskPolicy policy; /* Global overrun policy applied to all tasks */
    int trace_enabled; /* Non-zero to enable trace/logging output */
    int max_tasks;     /* Maximum number of tasks allowed */
    TaskConfig *tasks; /* Array of task configurations */
    int num_tasks;     /* Number of valid entries in tasks[] */
} SchedulerConfig;

/* Global variables for logging and task state management */
extern volatile QueueHandle_t logQueue;     /* Queue carrying LogEvent records */
extern volatile QueueHandle_t overrunQueue; /* Queue carrying overrun task IDs */
extern TaskState taskState[MAX_TASKS];      /* Per-task runtime state */

/*
 * Initialize the PTL scheduler with the given configuration.
 * Parameters:
 * - sconfig: A SchedulerConfig structure containing the scheduling policy,
 *            task configurations, and other settings.
 * This function creates the necessary FreeRTOS tasks (periodic tasks, the
 * logging task and the interrupt task) and starts the scheduler. It does not
 * return under normal operation.
 */
void Init(const SchedulerConfig sconfig);

/*
 * Apply the SKIP policy when an overrun is detected.
 * Parameters:
 * - xLastWakeUpTime: Pointer to the last wake-up time of the task, which will be updated.
 * - xPeriod: The period of the task in ticks.
 * - xNow: The current tick count.
 * Returns the number of releases that were skipped due to the overrun.
 */
UBaseType_t PTL_ApplySkipPolicy(volatile TickType_t *xLastWakeUpTime, TickType_t xPeriod, TickType_t xNow);

/*
 * Apply the KILL policy when an overrun is detected.
 * Parameters:
 * - taskConfig: Pointer to the configuration of the overrunning task; the task
 *               is deleted and re-created from this configuration.
 * - taskId: Index of the task in the taskState[] array.
 * Returns 1 (the number of jobs discarded by the kill).
 */
UBaseType_t PTL_ApplyKillPolicy(volatile TaskConfig *taskConfig, int taskId);

/*
 * Wrapper executed as the body of every periodic task. It handles release
 * timing, START/END logging, deadline-miss detection and the overrun policy.
 * Parameters:
 * - params: Pointer to the task's TaskConfig.
 */
void Task_Function(void *params);

#endif /* PTL_H */