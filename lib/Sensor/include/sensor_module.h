#pragma once
#include "Endpoint.h"

// SENSOR subsystem. SENSOR_GET {sensor} -> replies SENSOR_DATA {sensor, v0, v1, ...}.
// Hook a real driver in with setGyroReader(); until then GET is logged and not answered.
class SensorModule : public Module
{
public:
    typedef bool (*GyroReader)(int32_t &x, int32_t &y, int32_t &z);

    uint8_t subsys() const override { return proto::Subsys::SENSOR; }
    void setGyroReader(GyroReader r) { gyro_ = r; }
    void handle(const proto::Packet &p, Endpoint &link) override;

private:
    GyroReader gyro_ = nullptr;
};
