#ifndef PACKETS_H_
#define PACKETS_H_

#include <stdio.h>

typedef struct
{
    uint8_t rpm;
    uint8_t speed;    
    float accx;
    float accy;
    float accz;
    float dpsx;
    float dpsy;
    float dpsz;
    uint64_t timestamp;
    
} packet_t;

#endif
