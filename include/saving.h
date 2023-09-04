#ifndef SAVING_H_
#define SAVING_H_

#include <FS.h>

#include "hardware_definitions.h"
#include "packets.h"

#define SAVING_PERIOD 5

// if testing is needed, raising the value above may help the analisys
typedef enum {
    MOUNT_ERROR,
    FILE_ERROR,
    FILE_OK
} connectivity_states;

char file_name[20];
File root;
File dataFile;

#endif
