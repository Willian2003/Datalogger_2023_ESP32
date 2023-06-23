#ifndef SOFT_DEFS_H_
#define SOFT_DEFS_H_

#include <Arduino.h>

//Frequency of data acquisition in Hz
#define SAMPLE_FREQ 200

/* State Machines */
//Ocorrerá mudanças no enum
typedef enum{
    IDLE,
    SAVE,
    CAN_STATE,
} logging_states;

typedef enum{} connectivity_states;

logging_states l_state; // datalogger state

unsigned long timer;

#endif