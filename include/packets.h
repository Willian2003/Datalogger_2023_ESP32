#ifndef PACKETS_H_
#define PACKETS_H_

#include <stdio.h>

typedef struct
{

    uint16_t rpm;
    uint16_t speed;    
    uint64_t timestamp;
    
} packet_t;

// Packet constantly saved
packet_t volatile_packet;

#endif