#include "FreeRTOS.h"
#include "delay.h"
#include "task.h"

// This function will block the calling task for the specified number of milliseconds
void delay_routine(unsigned int milliseconds) {

    TickType_t target = xTaskGetTickCount() + pdMS_TO_TICKS(milliseconds);

    while(xTaskGetTickCount()< target){}

}

