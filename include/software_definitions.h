#ifndef SOFT_DEFS_H_
#define SOFT_DEFS_H_

#include <Arduino.h>

//Frequency of data acquisition in Hz
#define SAMPLE_FREQ 200

/* State Machines */
//Ocorrerá mudanças no enum
typedef enum {
    IDLE,
    WAITING,
    LOGGING
} logging_states;

typedef enum {
    IDLE_ST,
    MQTT_CONNECT
} connectivity_states;

logging_states l_state; // datalogger state
connectivity_states c_state; // Enable the mqtt publish

unsigned long timer;

#endif