#include "delay.h"
#include "FreeRTOSConfig.h"

void delay_routine(unsigned int milliseconds) {
  // Delay routine in assembly

  unsigned int counter = milliseconds * (configCPU_CLOCK_HZ/1000); // compute number of cycles based on CPU clock

  __asm__ volatile("push {r1} \n"
      "mov r1, %[counter] \n"
      "delay: \n"
          "subs r1, #1 \n"
          "bne delay \n"
      "pop {r1} \n"
      :                          // no outputs
      : [counter] "r" (counter)  // inputs
      : "r1", "cc", "memory"    // clobbers
    ); 
}

