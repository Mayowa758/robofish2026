
#include "TransmitterUpdater.h"
#include "FirmwareUpdateProtocol.h"

using namespace FirmwareUpdateProtocol;

static bool waitForAck(HardwareSerial &link, uint32_t timeoutMs)
{
    uint32_t start = millis();

    while (millis() - start < timeoutMs)
    {
        if (link.available())
        {
            int response = link.read();

            if (response == ACK)
                return true;

            if (response == NACK)
            {
                Serial.println("[ERROR] Receiver rejected the update.");
                return false;
            }

            Serial.printf("[WARN] Unexpected response: 0x%02X\n", response);
        }

        delay(1);
    }

    Serial.println("[ERROR] ACK timeout.");
    return false;
}

bool sendReceiverFirmware(
    HardwareSerial &link,
    const uint8_t *firmware,
    uint32_t firmwareSize)
{
    Serial.println("[TX] Sending update request...");

    link.write(UPDATE_REQUEST);
    link.write((uint8_t *)&firmwareSize, sizeof(firmwareSize));
    link.flush();

    if (!waitForAck(link, INITIAL_ACK_TIMEOUT))
    {
        Serial.println("[ERROR] Receiver did not acknowledge update request.");
        return false;
    }

    Serial.println("[TX] Receiver ready. Sending firmware...");

    uint32_t offset = 0;

    while (offset < firmwareSize)
    {
        size_t chunk = min(
            (uint32_t)CHUNK_SIZE,
            firmwareSize - offset);

        if (link.write(firmware + offset, chunk) != chunk)
        {
            Serial.println("[ERROR] UART write failed.");
            return false;
        }

        link.flush();

        if (!waitForAck(link, CHUNK_ACK_TIMEOUT))
        {
            Serial.printf(
                "[ERROR] Failed at byte %lu of %lu\n",
                (unsigned long)offset,
                (unsigned long)firmwareSize);

            return false;
        }

        offset += chunk;

        if (offset % 16384 == 0 || offset == firmwareSize)
        {
            Serial.printf(
                "[TX] Sent %lu / %lu bytes\n",
                (unsigned long)offset,
                (unsigned long)firmwareSize);
        }
    }

    Serial.println("[TX] Waiting for final confirmation...");

    if (!waitForAck(link, FINAL_ACK_TIMEOUT))
    {
        Serial.println("[ERROR] Final firmware confirmation failed.");
        return false;
    }

    return true;
}
