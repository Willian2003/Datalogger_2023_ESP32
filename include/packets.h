#ifndef PACKETS_H_
#define PACKETS_H_

#include <stdio.h>

typedef struct
{

    uint8_t rpm;
    uint8_t speed;    
    
} packet_t;

// Packet constantly saved
packet_t volatile_packet;

#endif