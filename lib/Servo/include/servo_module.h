#pragma once
#include "Endpoint.h"

// SERVO subsystem logic. No hardware in here: it only works out the angles. receiver.cpp owns the
// Servo objects and writes tailAngle()/leftAngle()/rightAngle() to them every loop.
class ServoModule : public Module
{
public:
    static constexpr int NEUTRAL = 90;

    uint8_t subsys() const override { return proto::Subsys::SERVO; }
    void handle(const proto::Packet &p, Endpoint &link) override;
    void tick() override; // advances the tail wave

    int tailAngle() const { return tailAngle_; }
    int leftAngle() const;
    int rightAngle() const;

private:
    void calibrate();
    void printState();
    void fishWave();

    int8_t leftFin_ = 0;
    int8_t rightFin_ = 0;
    uint8_t speed_ = 1;
    int8_t tailTrim_ = 0; // shifts the centre the tail waves around

    int tailAngle_ = NEUTRAL;
    int wavePosition_ = -25;
    int waveDirection_ = 1;
    unsigned long lastWaveUpdate_ = 0;
};
