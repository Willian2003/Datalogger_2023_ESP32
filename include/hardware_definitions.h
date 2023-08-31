#ifndef HARD_DEFS_H_
#define HARD_DEFS_H_

/* pinout definitions */
// LEDS
#define EMBEDDED_LED 2  // Turned on on failiure
#define WAIT_LED     14 // Turned on when the button is presses of
#define LOG_LED      12 // Turned on if the logging run

//SD
#define SD_CS 5

//Debounce
#define DEBOUNCETIME 100

//Sensors
//Pinos imaginarios, também ocorrerá mudanças
#define freq_pin 21
#define speed_pin 22

//Button ISR
#define Button 4

//GPRS
#define MODEM_RST 

#endif
