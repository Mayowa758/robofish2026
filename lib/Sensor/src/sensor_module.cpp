#include "sensor_module.h"

using namespace proto;

void SensorModule::handle(const Packet &p, Endpoint &link)
{
    if (p.opcode != sensor::GET || p.argc < 1)
        return;

    if (p.args[0] == sensor::GYRO)
    {
        int32_t x, y, z;
        if (gyro_ && gyro_(x, y, z))
            link.send(Subsys::SENSOR, sensor::DATA, {sensor::GYRO, x, y, z});
        else
            Serial.println("[RX] SENSOR GET gyro: no gyro reader registered");
    }
}
