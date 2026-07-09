#ifndef COMMS_H
#define COMMS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


#define FIN_MIN -90
#define FIN_MAX 90

#define SPEED_MAX 20
#define SPEED_MIN 1

#define CALIBRATE 1

// Special "No change" value
#define NO_CHANGE 0



/* STRUCTS */


struct FishState {
    int8_t leftFin = 0;
    int8_t rightFin = 0;
    uint8_t speed = 1;
    uint8_t calibrate = 0;
}; 


/* COMMANDS */

FishState decode_state(uint32_t message);
uint32_t encode_state(FishState state);

#ifdef __cplusplus
}
#endif

#endif