#include "Endpoint.h"

bool Endpoint::add(Module &m)
{
    if (count_ >= MAX_MODULES)
        return false;
    modules_[count_++] = &m;
    return true;
}

void Endpoint::poll()
{
    while (link_.available() > 0)
    {
        int b = link_.read();
        if (b < 0)
            break;

        if (parser_.feed((uint8_t)b, millis()))
        {
            const proto::Packet &p = parser_.packet();
            bool handled = false;
            for (size_t i = 0; i < count_; i++)
            {
                if (modules_[i]->subsys() == p.subsys)
                {
                    modules_[i]->handle(p, *this);
                    handled = true;
                    break;
                }
            }
            if (!handled)
                Serial.printf("[LINK] no module for subsys %u (opcode %u)\n", p.subsys, p.opcode);
        }
    }

    for (size_t i = 0; i < count_; i++)
        modules_[i]->tick();
}

bool Endpoint::send(const proto::Packet &p)
{
    uint8_t buf[proto::MAX_FRAME];
    size_t n = proto::encode(p, buf, sizeof buf);
    if (n == 0)
        return false;
    link_.write(buf, n);
    return true;
}

bool Endpoint::send(uint8_t subsys, uint8_t opcode, std::initializer_list<int32_t> args,
                    const uint8_t *body, uint16_t bodyLen)
{
    if (args.size() > proto::MAX_ARGS)
        return false;

    proto::Packet p;
    p.subsys = subsys;
    p.opcode = opcode;
    for (int32_t a : args)
        p.args[p.argc++] = a;
    p.body = body;
    p.bodyLen = bodyLen;
    return send(p);
}
