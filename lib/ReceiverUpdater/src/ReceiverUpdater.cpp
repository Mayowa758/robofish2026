
#include "ReceiverUpdater.h"
#include "FirmwareUpdateProtocol.h"
#include <Update.h>

using namespace FirmwareUpdateProtocol;

bool receiverUpdateRequested(HardwareSerial &link)
{
    return link.available() > 0 && link.peek() == UPDATE_REQUEST;
}

void handleReceiverUpdate(HardwareSerial &link)
{
    link.read(); // Consume 'U'

    Serial.println("[RX] Update request received");

    uint32_t firmwareSize = 0;

    if (link.readBytes(
            (uint8_t *)&firmwareSize,
            sizeof(firmwareSize)) != sizeof(firmwareSize))
    {
        Serial.println("[RX] Failed to read firmware size");
        link.write(NACK);
        return;
    }

    Serial.printf(
        "[RX] Firmware size: %lu bytes\n",
        (unsigned long)firmwareSize);

    if (firmwareSize == 0 || !Update.begin(firmwareSize))
    {
        Serial.printf(
            "[RX] Update.begin failed: %d\n",
            Update.getError());

        link.write(NACK);
        return;
    }

    link.write(ACK);
    link.flush();

    uint8_t buffer[CHUNK_SIZE];
    uint32_t received = 0;

    while (received < firmwareSize)
    {
        size_t wanted = min(
            (uint32_t)sizeof(buffer),
            firmwareSize - received);

        size_t count = link.readBytes(buffer, wanted);

        if (count != wanted)
        {
            Serial.println("[RX] Transfer timed out");
            Update.abort();
            link.write(NACK);
            return;
        }

        if (Update.write(buffer, count) != count)
        {
            Serial.println("[RX] Flash write failed");
            Update.abort();
            link.write(NACK);
            return;
        }

        received += count;

        link.write(ACK);
        link.flush();
    }

    if (Update.end(true))
    {
        Serial.println("[RX] Update successful. Restarting...");

        link.write(ACK);
        link.flush();

        delay(200);
        ESP.restart();
    }
    else
    {
        Serial.printf(
            "[RX] Update finalisation failed: %d\n",
            Update.getError());

        link.write(NACK);
    }
}
