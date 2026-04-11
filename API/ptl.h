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

#define MAX_WAIT portMAX_DELAY
#define QUEUE_LENGTH 15
#define MESSAGE_LENGTH 60
#define DEFAULT_STACK_SIZE 512

#define POLICY_SKIP 0
#define POLICY_KILL 1
#define POLICY_CATCH_UP 2

#define MAX_TASKS 8

#define TASK_RUNNING 1
#define TASK_NOT_RUNNING 2

typedef struct {
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

typedef struct {
    TaskHandle_t task;
    char name[configMAX_TASK_NAME_LEN];
    int state;
    int policy;
    TickType_t startTime;
    TickType_t finishTime;
    TickType_t lastReleaseTime; // used by TickHook to track expected releases
    uint32_t period_ms;
    uint32_t deadline;
    uint32_t k;
} TaskState;

typedef struct {
    int policy;
    int trace_enabled;
    int max_tasks;
    TaskConfig *tasks;
    int num_tasks;
} SchedulerConfig;

// Global variables for logging and synchronization
extern volatile QueueHandle_t logQueue;
extern volatile SemaphoreHandle_t xSemaphore;
extern volatile TaskState taskState[MAX_TASKS];

// Initialization function to set up tasks and start scheduling
void Init(const SchedulerConfig sconfig);

// Functions for overrun detection and SKIP policy
BaseType_t PTL_IsOverrun(TickType_t xLastWakeUpTime, TickType_t xPeriod, TickType_t xNow);
UBaseType_t PTL_ApplySkipPolicy(TickType_t *xLastWakeUpTime, TickType_t xPeriod, TickType_t xNow);

#endif 