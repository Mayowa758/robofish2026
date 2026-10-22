#include "ReceiverUpdater.h"
#include <Update.h>

using namespace proto;

static const uint32_t IDLE_ABORT_MS = 15000;

void UpdateModule::ack(Endpoint &link, uint8_t op)
{
    link.send(Subsys::UPDATE, update::ACK, {(int32_t)op, (int32_t)received_});
}

void UpdateModule::nack(Endpoint &link, uint8_t op, int32_t err)
{
    link.send(Subsys::UPDATE, update::NACK, {(int32_t)op, err, (int32_t)received_});
}

void UpdateModule::abortUpdate()
{
    if (active_)
        ::Update.abort();
    active_ = false;
    size_ = received_ = lastChunkLen_ = 0;
}

void UpdateModule::tick()
{
    if (active_ && millis() - lastActivityMs_ > IDLE_ABORT_MS)
    {
        Serial.println("[RX] Update timed out, aborting");
        abortUpdate();
    }
}

void UpdateModule::handle(const Packet &p, Endpoint &link)
{
    lastActivityMs_ = millis();

    switch (p.opcode)
    {
    case update::BEGIN:
    {
        abortUpdate();
        if (p.argc < 1 || p.args[0] <= 0)
        {
            nack(link, p.opcode, update::ERR_BAD_ARGS);
            return;
        }
        size_ = (uint32_t)p.args[0];
        Serial.printf("[RX] Update request: %lu bytes\n", (unsigned long)size_);

        if (!::Update.begin(size_))
        {
            Serial.printf("[RX] Update.begin failed: %d\n", ::Update.getError());
            size_ = 0;
            nack(link, p.opcode, update::ERR_BEGIN_FAILED);
            return;
        }
        active_ = true;
        received_ = 0;
        lastChunkLen_ = 0;
        ack(link, p.opcode);
        return;
    }

    case update::CHUNK:
    {
        if (!active_)
        {
            nack(link, p.opcode, update::ERR_NOT_ACTIVE);
            return;
        }
        if (p.argc < 1 || p.bodyLen == 0)
        {
            nack(link, p.opcode, update::ERR_BAD_ARGS);
            return;
        }

        uint32_t offset = (uint32_t)p.args[0];

        // Our ACK got lost and the transmitter resent the chunk we already wrote: just ACK again.
        if (lastChunkLen_ && offset + lastChunkLen_ == received_ && p.bodyLen == lastChunkLen_)
        {
            ack(link, p.opcode);
            return;
        }
        // Anything else out of order: tell the transmitter where we actually are.
        if (offset != received_ || received_ + p.bodyLen > size_)
        {
            nack(link, p.opcode, update::ERR_BAD_OFFSET);
            return;
        }

        if (::Update.write((uint8_t *)p.body, p.bodyLen) != p.bodyLen)
        {
            Serial.println("[RX] Flash write failed");
            abortUpdate();
            nack(link, p.opcode, update::ERR_WRITE_FAILED);
            return;
        }
        received_ += p.bodyLen;
        lastChunkLen_ = p.bodyLen;
        ack(link, p.opcode);
        return;
    }

    case update::END:
    {
        if (!active_)
        {
            nack(link, p.opcode, update::ERR_NOT_ACTIVE);
            return;
        }
        if (received_ != size_)
        {
            nack(link, p.opcode, update::ERR_INCOMPLETE);
            return;
        }
        if (::Update.end(true))
        {
            Serial.println("[RX] Update successful. Restarting...");
            active_ = false;
            ack(link, p.opcode);
            link.stream().flush();
            delay(200);
            ESP.restart();
        }
        else
        {
            Serial.printf("[RX] Update finalisation failed: %d\n", ::Update.getError());
            abortUpdate();
            nack(link, p.opcode, update::ERR_END_FAILED);
        }
        return;
    }

    case update::ABORT:
        abortUpdate();
        ack(link, p.opcode);
        return;

    default:
        break; // ACK/NACK are receiver -> transmitter only
    }
}
