#pragma once

#include <Arduino.h>

bool receiverUpdateRequested(HardwareSerial &link);
void handleReceiverUpdate(HardwareSerial &link);
