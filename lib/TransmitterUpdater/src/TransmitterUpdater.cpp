#include "TransmitterUpdater.h"
#include "Endpoint.h"

using namespace proto;

static const size_t CHUNK_SIZE = MAX_BODY;
static const uint32_t BEGIN_TIMEOUT = 3000;
static const uint32_t CHUNK_TIMEOUT = 2000;
static const uint32_t END_TIMEOUT = 10000;
static const int MAX_RETRIES = 5;

enum Reply { REPLY_ACK, REPLY_NACK, REPLY_TIMEOUT };

struct Result
{
    Reply kind = REPLY_TIMEOUT;
    int32_t error = 0;
    uint32_t received = 0; // bytes the receiver says it has
};

// Send one UPDATE packet and wait for the matching UPDATE_ACK / UPDATE_NACK.
static Result exchange(Stream &link, Parser &parser, const Packet &out, uint32_t timeoutMs)
{
    uint8_t buf[MAX_FRAME];
    size_t n = encode(out, buf, sizeof buf);
    link.write(buf, n);
    link.flush();

    Result r;
    uint32_t start = millis();
    while (millis() - start < timeoutMs)
    {
        while (link.available() > 0)
        {
            if (!parser.feed((uint8_t)link.read(), millis()))
                continue;

            const Packet &p = parser.packet();
            if (p.subsys != Subsys::UPDATE)
                continue;

            if (p.opcode == update::ACK && p.argc >= 2 && p.args[0] == out.opcode)
            {
                r.kind = REPLY_ACK;
                r.received = (uint32_t)p.args[1];
                return r;
            }
            if (p.opcode == update::NACK && p.argc >= 3 && p.args[0] == out.opcode)
            {
                r.kind = REPLY_NACK;
                r.error = p.args[1];
                r.received = (uint32_t)p.args[2];
                return r;
            }
        }
        delay(1);
    }
    return r; // timeout
}

bool sendReceiverFirmware(Stream &link, const uint8_t *firmware, uint32_t firmwareSize)
{
    Parser parser;

    while (link.available() > 0) // drop stale bytes
        link.read();

    Packet begin;
    begin.subsys = Subsys::UPDATE;
    begin.opcode = update::BEGIN;
    begin.argc = 1;
    begin.args[0] = (int32_t)firmwareSize;

    Serial.println("[TX] Sending UPDATE_BEGIN...");
    Result r = exchange(link, parser, begin, BEGIN_TIMEOUT);
    if (r.kind != REPLY_ACK)
    {
        Serial.printf("[ERROR] Receiver did not accept UPDATE_BEGIN (%s, error %ld)\n",
                      r.kind == REPLY_NACK ? "NACK" : "timeout", (long)r.error);
        return false;
    }

    Serial.println("[TX] Receiver ready. Sending firmware...");

    uint32_t offset = 0;
    int retries = 0;

    while (offset < firmwareSize)
    {
        size_t len = min((uint32_t)CHUNK_SIZE, firmwareSize - offset);

        Packet chunk;
        chunk.subsys = Subsys::UPDATE;
        chunk.opcode = update::CHUNK;
        chunk.argc = 1;
        chunk.args[0] = (int32_t)offset;
        chunk.body = firmware + offset;
        chunk.bodyLen = (uint16_t)len;

        r = exchange(link, parser, chunk, CHUNK_TIMEOUT);

        if (r.kind == REPLY_ACK)
        {
            offset += len;
            retries = 0;
            if (offset % 16384 < len || offset == firmwareSize)
                Serial.printf("[TX] Sent %lu / %lu bytes\n", (unsigned long)offset, (unsigned long)firmwareSize);
            continue;
        }

        // NACK or timeout: resend (resuming from where the receiver says it is, if it told us)
        if (++retries > MAX_RETRIES)
        {
            Serial.printf("[ERROR] Failed at byte %lu of %lu (error %ld)\n",
                          (unsigned long)offset, (unsigned long)firmwareSize, (long)r.error);
            Packet abort;
            abort.subsys = Subsys::UPDATE;
            abort.opcode = update::ABORT;
            exchange(link, parser, abort, 1000);
            return false;
        }
        if (r.kind == REPLY_NACK && r.error == update::ERR_BAD_OFFSET && r.received <= firmwareSize)
            offset = r.received;
        Serial.printf("[WARN] Resending chunk at %lu (attempt %d)\n", (unsigned long)offset, retries);
    }

    Serial.println("[TX] Waiting for final confirmation...");
    Packet end;
    end.subsys = Subsys::UPDATE;
    end.opcode = update::END;
    r = exchange(link, parser, end, END_TIMEOUT);
    if (r.kind != REPLY_ACK)
    {
        Serial.println("[ERROR] Final firmware confirmation failed.");
        return false;
    }
    return true;
}
