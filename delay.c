#include "FreeRTOS.h"
#include "delay.h"
#include "task.h"

// This function simulates a busy-waiting loop for a specified duration in milliseconds
void delay_routine(unsigned int milliseconds) 
{
    volatile unsigned long counter = milliseconds * (configCPU_CLOCK_HZ/1000); // compute number of cycles based on CPU clock
    while (counter--) __asm volatile("nop");

}