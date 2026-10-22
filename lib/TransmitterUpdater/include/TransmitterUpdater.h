#pragma once
// Transmitter side of UPDATE_*: pushes a firmware image to the receiver.
// Blocking; returns true when the receiver confirmed UPDATE_END.
#include <Arduino.h>

bool sendReceiverFirmware(
    Stream &link,
    const uint8_t *firmware,
    uint32_t firmwareSize);
