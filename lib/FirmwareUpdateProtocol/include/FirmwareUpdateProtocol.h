#pragma once

#include <Arduino.h>

namespace FirmwareUpdateProtocol
{
    constexpr uint8_t UPDATE_REQUEST = 'U';
    constexpr uint8_t ACK = 'A';
    constexpr uint8_t NACK = 'N';

    constexpr size_t CHUNK_SIZE = 512;

    constexpr uint32_t INITIAL_ACK_TIMEOUT = 3000;
    constexpr uint32_t CHUNK_ACK_TIMEOUT = 5000;
    constexpr uint32_t FINAL_ACK_TIMEOUT = 10000;
}
