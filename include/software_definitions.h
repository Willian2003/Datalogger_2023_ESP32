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

//Debounce
#define DEBOUNCETIME 100
uint64_t saveDebounceTimeout=0;
//volatile bool lastState; 
bool saveLastState;
bool save = false;

/* State Machines */
//Ocorrerá mudanças no enum
typedef enum {
    IDLE,
    WAITING,
    LOGGING
} logging_states;

logging_states l_state; // datalogger state

unsigned long timer;

#endif
