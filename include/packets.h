#ifndef PACKETS_H_
#define PACKETS_H_

#include <stdio.h>

// struct to save
typedef struct
{
    int gyrox;
    int gyroy;
    int gyroz;
    int accx;
    int accy;
    int accz;
    uint8_t rpm;
    uint8_t speed;    
    uint64_t timestamp;
    
} packet_t;

// struct to send
typedef struct
{
    String gyrox;
    String gyroy;
    String gyroz;
    String accx;
    String accy;
    String accz;
    String rpm;
    String speed; 
    String timestamp;  
} strings;

#endif
