#include "FreeRTOS.h"
#include "delay.h"
#include "task.h"

// This function simulates a busy-waiting loop for a specified duration in milliseconds
void delay_routine(unsigned int milliseconds) 
{
    TickType_t startTick = xTaskGetTickCount();
    TickType_t delayTicks = pdMS_TO_TICKS(milliseconds);

    // Busy-wait until the specified time has expired
    while ((xTaskGetTickCount() - startTick) < delayTicks) 
    {
        // We use NOP to prevent compiler optimizations that might remove the empty loop
        __asm volatile("nop"); 
    }
}