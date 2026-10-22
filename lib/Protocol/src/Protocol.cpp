#include "Protocol.h"
#include <string.h>

namespace proto
{
    uint16_t crc16(const uint8_t *data, size_t len, uint16_t crc)
    {
        for (size_t i = 0; i < len; i++)
        {
            crc ^= (uint16_t)data[i] << 8;
            for (int b = 0; b < 8; b++)
                crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
        return crc;
    }

    size_t encode(const Packet &p, uint8_t *out, size_t cap)
    {
        if (p.argc > MAX_ARGS || p.bodyLen > MAX_BODY || (p.bodyLen > 0 && p.body == nullptr))
            return 0;

        size_t total = 1 + HEADER_SIZE + (size_t)p.argc * 4 + p.bodyLen + 2;
        if (cap < total)
            return 0;

        size_t n = 0;
        out[n++] = SOF;
        out[n++] = p.subsys;
        out[n++] = p.opcode;
        out[n++] = p.argc;
        out[n++] = (uint8_t)(p.bodyLen & 0xFF);
        out[n++] = (uint8_t)(p.bodyLen >> 8);

        for (uint8_t i = 0; i < p.argc; i++)
        {
            uint32_t v = (uint32_t)p.args[i];
            out[n++] = (uint8_t)(v);
            out[n++] = (uint8_t)(v >> 8);
            out[n++] = (uint8_t)(v >> 16);
            out[n++] = (uint8_t)(v >> 24);
        }

        if (p.bodyLen)
        {
            memcpy(out + n, p.body, p.bodyLen);
            n += p.bodyLen;
        }

        uint16_t crc = crc16(out + 1, n - 1);
        out[n++] = (uint8_t)(crc & 0xFF);
        out[n++] = (uint8_t)(crc >> 8);
        return n;
    }

    bool Parser::feed(uint8_t b, uint32_t nowMs)
    {
        if (state_ != WAIT_SOF && nowMs != 0 && nowMs - lastByteMs_ > FRAME_TIMEOUT_MS)
            state_ = WAIT_SOF; // frame stalled, hunt for a new one
        lastByteMs_ = nowMs;

        switch (state_)
        {
        case WAIT_SOF:
            if (b == SOF)
            {
                idx_ = 0;
                state_ = HEADER;
            }
            return false;

        case HEADER:
            header_[idx_++] = b;
            if (idx_ == HEADER_SIZE)
            {
                size_t argc = header_[2];
                size_t bodyLen = (size_t)header_[3] | ((size_t)header_[4] << 8);
                if (argc > MAX_ARGS || bodyLen > MAX_BODY)
                {
                    state_ = WAIT_SOF; // nonsense header, resync
                    return false;
                }
                payloadLen_ = argc * 4 + bodyLen;
                idx_ = 0;
                state_ = payloadLen_ ? PAYLOAD : CRC;
            }
            return false;

        case PAYLOAD:
            payload_[idx_++] = b;
            if (idx_ == payloadLen_)
            {
                idx_ = 0;
                state_ = CRC;
            }
            return false;

        case CRC:
        {
            crcBytes_[idx_++] = b;
            if (idx_ < 2)
                return false;

            state_ = WAIT_SOF;
            uint16_t got = (uint16_t)crcBytes_[0] | ((uint16_t)crcBytes_[1] << 8);
            uint16_t calc = crc16(header_, HEADER_SIZE);
            calc = crc16(payload_, payloadLen_, calc);
            if (got != calc)
            {
                crcErrors_++;
                return false;
            }

            packet_ = Packet();
            packet_.subsys = header_[0];
            packet_.opcode = header_[1];
            packet_.argc = header_[2];
            packet_.bodyLen = (uint16_t)(header_[3] | (header_[4] << 8));
            for (uint8_t i = 0; i < packet_.argc; i++)
            {
                const uint8_t *a = payload_ + i * 4;
                packet_.args[i] = (int32_t)((uint32_t)a[0] | ((uint32_t)a[1] << 8) |
                                            ((uint32_t)a[2] << 16) | ((uint32_t)a[3] << 24));
            }
            packet_.body = packet_.bodyLen ? payload_ + packet_.argc * 4 : nullptr;
            return true;
        }
        }
        return false;
    }
}
