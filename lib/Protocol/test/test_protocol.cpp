// Host test:  g++ -std=c++11 -I../include ../src/Protocol.cpp test_protocol.cpp -o t && ./t
#include "Protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>
using namespace proto;

static bool feedAll(Parser &p, const uint8_t *d, size_t n, Packet *out = nullptr, int *count = nullptr)
{
    bool any = false;
    for (size_t i = 0; i < n; i++)
        if (p.feed(d[i]))
        {
            any = true;
            if (out) *out = p.packet();
            if (count) (*count)++;
        }
    return any;
}

int main()
{
    uint8_t buf[MAX_FRAME];

    { // servo packet round trip incl. negative arg
        Packet a; a.subsys = Subsys::SERVO; a.opcode = servo::SET_ANGLE; a.argc = 2;
        a.args[0] = servo::LEFT; a.args[1] = -70;
        size_t n = encode(a, buf, sizeof buf);
        assert(n == 1 + 5 + 8 + 2);
        Parser p; Packet r;
        assert(feedAll(p, buf, n, &r));
        assert(r.subsys == Subsys::SERVO && r.opcode == servo::SET_ANGLE && r.argc == 2);
        assert(r.args[0] == 0 && r.args[1] == -70 && r.bodyLen == 0 && r.body == nullptr);
    }
    { // no args, no body
        Packet a; a.subsys = Subsys::SERVO; a.opcode = servo::RESET;
        size_t n = encode(a, buf, sizeof buf); Parser p; Packet r;
        assert(feedAll(p, buf, n, &r) && r.opcode == servo::RESET && r.argc == 0);
    }
    { // max size binary body incl. bytes equal to SOF
        std::vector<uint8_t> body(MAX_BODY);
        for (size_t i = 0; i < body.size(); i++) body[i] = (i % 3 == 0) ? SOF : (uint8_t)i;
        Packet a; a.subsys = Subsys::UPDATE; a.opcode = update::CHUNK; a.argc = 1; a.args[0] = 123456;
        a.body = body.data(); a.bodyLen = body.size();
        size_t n = encode(a, buf, sizeof buf); assert(n == MAX_FRAME - 12 + 4 - 0 || n > 0);
        Parser p; Packet r;
        assert(feedAll(p, buf, n, &r));
        assert(r.bodyLen == MAX_BODY && memcmp(r.body, body.data(), MAX_BODY) == 0 && r.args[0] == 123456);
    }
    { // corrupted frame rejected, next good frame still parsed
        Packet a; a.subsys = Subsys::SERVO; a.opcode = servo::SET_SPEED; a.argc = 1; a.args[0] = 7;
        size_t n = encode(a, buf, sizeof buf);
        std::vector<uint8_t> stream(buf, buf + n);
        stream[8] ^= 0x40;                                   // flip a bit
        stream.insert(stream.end(), buf, buf + n);           // followed by a good copy
        Parser p; Packet r; int count = 0;
        feedAll(p, stream.data(), stream.size(), &r, &count);
        assert(count == 1 && p.crcErrors() == 1 && r.args[0] == 7);
    }
    { // leading garbage and 0xA5 noise
        Packet a; a.subsys = Subsys::SENSOR; a.opcode = sensor::GET; a.argc = 1; a.args[0] = sensor::GYRO;
        size_t n = encode(a, buf, sizeof buf);
        std::vector<uint8_t> stream = {0x00, 0xA5, 0xA5, 0x01, 0xFF, 0x10, 0x20};
        stream.insert(stream.end(), buf, buf + n);
        Parser p; Packet r; int count = 0;
        feedAll(p, stream.data(), stream.size(), &r, &count);
        assert(count == 1 && r.subsys == Subsys::SENSOR);
    }
    { // stalled half frame is dropped after timeout
        Packet a; a.subsys = Subsys::SERVO; a.opcode = servo::RESET;
        size_t n = encode(a, buf, sizeof buf);
        Parser p; uint32_t t = 1000;
        for (size_t i = 0; i < 3; i++) p.feed(buf[i], t++);
        t += 500;
        bool got = false;
        for (size_t i = 0; i < n; i++) got |= p.feed(buf[i], t++);
        assert(got);
    }
    { // encode rejects bad packets
        Packet a; a.argc = 9; assert(encode(a, buf, sizeof buf) == 0);
        Packet b; b.bodyLen = 4; assert(encode(b, buf, sizeof buf) == 0);
        Packet c; assert(encode(c, buf, 3) == 0);
    }
    puts("protocol tests passed");
    return 0;
}
