#pragma once
/*
 * Robofish wire protocol (pure C++, no Arduino dependency so it can be unit tested on a PC).
 *
 *   [SUBSYS] [OPCODE] {...args} {body}
 *
 * On the wire every packet is one frame:
 *
 *   0xA5 | subsys u8 | opcode u8 | argc u8 | bodyLen u16 LE | args (argc x int32 LE) | body | crc16 LE
 *
 * crc16 = CRC-16/CCITT-FALSE over everything between SOF and the crc.
 * Frames are binary safe (the firmware update sends raw bytes in `body`) and a receiver can
 * resynchronise after line noise by hunting for the next 0xA5.
 *
 * TO ADD A NEW SUBSYSTEM: add an id to `Subsys`, a namespace of opcodes below, then write a Module
 * (see Endpoint.h) and register it. Nothing else in the protocol layer needs to change.
 */

#include <stddef.h>
#include <stdint.h>

namespace proto
{
    constexpr uint8_t SOF = 0xA5;
    constexpr size_t MAX_ARGS = 4;
    constexpr size_t MAX_BODY = 512; // = firmware chunk size
    constexpr size_t HEADER_SIZE = 5; // subsys, opcode, argc, bodyLen(2)
    constexpr size_t MAX_FRAME = 1 + HEADER_SIZE + MAX_ARGS * 4 + MAX_BODY + 2;
    constexpr uint32_t FRAME_TIMEOUT_MS = 100; // abandon a half received frame after this long

    /* ---------- Subsystems ---------- */
    namespace Subsys
    {
        constexpr uint8_t SERVO = 1;
        constexpr uint8_t SENSOR = 2;
        constexpr uint8_t UPDATE = 3;
    }

    /* ---------- SERVO ----------
     * SET_ANGLE   {fin, angle}   move fin toward angle (see ServoModule: eased, one step per message)
     * DELTA_ANGLE {fin, angle}   add angle to the fin's current angle
     * RESET                      calibrate: everything back to neutral
     * SET_SPEED   {speed}        tail wave speed
     * fin: LEFT / RIGHT / BACK(tail trim). angle in degrees relative to neutral. */
    namespace servo
    {
        constexpr uint8_t SET_ANGLE = 1;
        constexpr uint8_t DELTA_ANGLE = 2;
        constexpr uint8_t RESET = 3;
        constexpr uint8_t SET_SPEED = 4;

        constexpr int32_t LEFT = 0;
        constexpr int32_t RIGHT = 1;
        constexpr int32_t BACK = 2;

        constexpr int FIN_MIN = -70;
        constexpr int FIN_MAX = 70;
        constexpr int SPEED_MIN = 1;
        constexpr int SPEED_MAX = 20;
    }

    /* ---------- SENSOR ----------
     * GET  {sensor}                       request a reading           (transmitter -> receiver)
     * DATA {sensor, v0, v1, ...}          reading                     (receiver -> transmitter) */
    namespace sensor
    {
        constexpr uint8_t GET = 1;
        constexpr uint8_t DATA = 2;

        constexpr int32_t GYRO = 0; // DATA args: {GYRO, x, y, z}
    }

    /* ---------- UPDATE (firmware over the link) ----------
     * BEGIN {size}            start an update of `size` bytes
     * CHUNK {offset} + body   next piece of firmware, `offset` = bytes already sent
     * END                     all bytes sent, finalise and reboot
     * ABORT                   cancel
     * ACK   {opcode, bytesReceived}                 (receiver -> transmitter)
     * NACK  {opcode, errorCode, bytesReceived}      (receiver -> transmitter) transmitter resends */
    namespace update
    {
        constexpr uint8_t BEGIN = 1;
        constexpr uint8_t CHUNK = 2;
        constexpr uint8_t END = 3;
        constexpr uint8_t ABORT = 4;
        constexpr uint8_t ACK = 5;
        constexpr uint8_t NACK = 6;

        constexpr int32_t ERR_BAD_ARGS = 1;
        constexpr int32_t ERR_BEGIN_FAILED = 2;
        constexpr int32_t ERR_NOT_ACTIVE = 3;
        constexpr int32_t ERR_BAD_OFFSET = 4; // bytesReceived tells the sender where to resume
        constexpr int32_t ERR_WRITE_FAILED = 5;
        constexpr int32_t ERR_INCOMPLETE = 6;
        constexpr int32_t ERR_END_FAILED = 7;
    }

    /* ---------- Packet ---------- */
    struct Packet
    {
        uint8_t subsys = 0;
        uint8_t opcode = 0;
        uint8_t argc = 0;
        int32_t args[MAX_ARGS] = {0, 0, 0, 0};
        uint16_t bodyLen = 0;
        const uint8_t *body = nullptr; // not owned; for parsed packets valid until the next feed()
    };

    uint16_t crc16(const uint8_t *data, size_t len, uint16_t crc = 0xFFFF);

    // Serialise into out (capacity cap, MAX_FRAME is always enough). Returns bytes written, 0 on error.
    size_t encode(const Packet &p, uint8_t *out, size_t cap);

    // Byte-at-a-time frame parser. Feed it everything that arrives on the link.
    class Parser
    {
    public:
        // Returns true when a complete, CRC-valid packet is available via packet().
        // nowMs is optional; pass millis() to get stalled-frame recovery.
        bool feed(uint8_t byte, uint32_t nowMs = 0);
        const Packet &packet() const { return packet_; }
        void reset() { state_ = WAIT_SOF; }
        uint32_t crcErrors() const { return crcErrors_; }

    private:
        enum State : uint8_t { WAIT_SOF, HEADER, PAYLOAD, CRC };
        State state_ = WAIT_SOF;
        uint8_t header_[HEADER_SIZE];
        uint8_t payload_[MAX_ARGS * 4 + MAX_BODY];
        uint8_t crcBytes_[2];
        size_t idx_ = 0;
        size_t payloadLen_ = 0;
        uint32_t lastByteMs_ = 0;
        uint32_t crcErrors_ = 0;
        Packet packet_;
    };
}
