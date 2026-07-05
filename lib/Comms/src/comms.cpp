#include "comms.h"


/* FUNCTIONS */
uint32_t encode_state(FishState state) {
  uint32_t encodedData = 0;
  encodedData |= ((uint32_t) state.leftFin & 0xFF) << 24;
  encodedData |= ((uint32_t) state.rightFin  & 0xFF) << 16;
  encodedData |= ((uint32_t) state.speed  & 0xFF) << 8;
  encodedData |= ((uint32_t) state.calibrate  & 0xFF);

  return encodedData;
}

FishState decode_state(uint32_t message) {
  FishState fs;

  fs.leftFin = (int8_t) ((message & 0xFF000000) >> 24);
  fs.rightFin = (int8_t) ((message & 0x00FF0000) >> 16);
  fs.speed = (uint8_t) ((message & 0x0000FF00) >> 8);
  fs.calibrate = (uint8_t) (message & 0x000000FF);

  return fs;
}