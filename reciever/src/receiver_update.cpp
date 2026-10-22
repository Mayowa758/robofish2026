// Firmware update over the link (receiver side). Self-contained: shares no variables with receiver.cpp.
#include <Arduino.h>
#include <Update.h>
#include "Protocol.h"
#include "receiver_update.h"

static bool updateActive = false;
static uint32_t updateSize = 0, updateReceived = 0, lastChunkLen = 0;
static unsigned long lastUpdateActivity = 0;
static const unsigned long UPDATE_IDLE_ABORT_MS = 15000;

// ============================================================================================
// Sending replies (receiver -> transmitter)
// ============================================================================================
static void sendReply(uint8_t subsys, uint8_t opcode, uint8_t argc, int32_t a0 = 0, int32_t a1 = 0, int32_t a2 = 0, int32_t a3 = 0)
{
    proto::Packet p;
    p.subsys = subsys;
    p.opcode = opcode;
    p.argc = argc;
    p.args[0] = a0;
    p.args[1] = a1;
    p.args[2] = a2;
    p.args[3] = a3;

    uint8_t buf[proto::MAX_FRAME];
    size_t n = proto::encode(p, buf, sizeof buf);
    if (n > 0)
        Serial1.write(buf, n);
}

// ============================================================================================
// UPDATE   firmware over the link
// ============================================================================================
static void updateAck(uint8_t op)
{
    sendReply(proto::Subsys::UPDATE, proto::update::ACK, 2, op, (int32_t)updateReceived);
}

static void updateNack(uint8_t op, int32_t err)
{
    sendReply(proto::Subsys::UPDATE, proto::update::NACK, 3, op, err, (int32_t)updateReceived);
}

static void updateAbort()
{
    if (updateActive)
        Update.abort();
    updateActive = false;
    updateSize = updateReceived = lastChunkLen = 0;
}

void handleUpdate(const proto::Packet &p)
{
    lastUpdateActivity = millis();

    switch (p.opcode)
    {
    case proto::update::BEGIN:
        updateAbort();
        if (p.argc < 1 || p.args[0] <= 0)
        {
            updateNack(p.opcode, proto::update::ERR_BAD_ARGS);
            return;
        }
        updateSize = (uint32_t)p.args[0];
        Serial.printf("[RX] Update request: %lu bytes\n", (unsigned long)updateSize);
        if (!Update.begin(updateSize))
        {
            Serial.printf("[RX] Update.begin failed: %d\n", Update.getError());
            updateSize = 0;
            updateNack(p.opcode, proto::update::ERR_BEGIN_FAILED);
            return;
        }
        updateActive = true;
        updateReceived = 0;
        lastChunkLen = 0;
        updateAck(p.opcode);
        return;

    case proto::update::CHUNK:
    {
        if (!updateActive)
        {
            updateNack(p.opcode, proto::update::ERR_NOT_ACTIVE);
            return;
        }
        if (p.argc < 1 || p.bodyLen == 0)
        {
            updateNack(p.opcode, proto::update::ERR_BAD_ARGS);
            return;
        }
        uint32_t offset = (uint32_t)p.args[0];

        // Our ACK got lost and the chunk was resent: just ACK again.
        if (lastChunkLen && offset + lastChunkLen == updateReceived && p.bodyLen == lastChunkLen)
        {
            updateAck(p.opcode);
            return;
        }
        // Out of order: tell the transmitter where we actually are.
        if (offset != updateReceived || updateReceived + p.bodyLen > updateSize)
        {
            updateNack(p.opcode, proto::update::ERR_BAD_OFFSET);
            return;
        }
        if (Update.write((uint8_t *)p.body, p.bodyLen) != p.bodyLen)
        {
            Serial.println("[RX] Flash write failed");
            updateAbort();
            updateNack(p.opcode, proto::update::ERR_WRITE_FAILED);
            return;
        }
        updateReceived += p.bodyLen;
        lastChunkLen = p.bodyLen;
        updateAck(p.opcode);
        return;
    }

    case proto::update::END:
        if (!updateActive)
        {
            updateNack(p.opcode, proto::update::ERR_NOT_ACTIVE);
            return;
        }
        if (updateReceived != updateSize)
        {
            updateNack(p.opcode, proto::update::ERR_INCOMPLETE);
            return;
        }
        if (Update.end(true))
        {
            Serial.println("[RX] Update successful. Restarting...");
            updateActive = false;
            updateAck(p.opcode);
            Serial1.flush();
            delay(200);
            ESP.restart();
        }
        else
        {
            Serial.printf("[RX] Update finalisation failed: %d\n", Update.getError());
            updateAbort();
            updateNack(p.opcode, proto::update::ERR_END_FAILED);
        }
        return;

    case proto::update::ABORT:
        updateAbort();
        updateAck(p.opcode);
        return;

    default:
        return;
    }
}

void tickUpdate()
{
    if (updateActive && millis() - lastUpdateActivity > UPDATE_IDLE_ABORT_MS)
    {
        Serial.println("[RX] Update timed out, aborting");
        updateAbort();
    }
}

bool updateIsActive()
{
    return updateActive;
}