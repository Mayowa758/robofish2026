#pragma once

#include <Arduino.h>

bool sendReceiverFirmware(
    HardwareSerial &link,
    const uint8_t *firmware,
    uint32_t firmwareSize);
