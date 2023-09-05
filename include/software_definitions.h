#ifndef SOFT_DEFS_H_
#define SOFT_DEFS_H_

#include <Arduino.h>

#ifndef ESPRESSIF
    #define ESPRESSIF
    #include <FS.h>
    #include <Wire.h>
    #include <SPI.h>
    #include <I2S.h>
#endif

//Frequency of data acquisition in Hz
#define SAMPLE_FREQ 200

//volatile bool lastState; 
bool running = false;

/* State Machines */
/*typedef enum {
    IDLE,
    WAITING,
    LOGGING
} logging_states;

logging_states l_state;*/ // datalogger state

unsigned long timer;

#endif
