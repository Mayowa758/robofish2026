#include "servo_module.h"

using namespace proto;

static const int TAIL_TRIM_LIMIT = 40;

// Sensitivity (s): fraction of the way to the target that one step moves
// state[t + 1] = (1-s) * state[t] + s * target
static const float SENSITIVITY = 0.05f;

static int8_t smoothStep(int8_t current, int8_t target)
{
    if (current == target)
        return current;

    int8_t next = (int8_t)roundf((1 - SENSITIVITY) * current + target * SENSITIVITY);
    if (next == current)
        next = current + (target > current ? 1 : -1);
    return next;
}

void ServoModule::handle(const Packet &p, Endpoint &)
{
    switch (p.opcode)
    {
    case servo::SET_ANGLE:
    case servo::DELTA_ANGLE:
    {
        if (p.argc < 2)
            return;
        bool delta = (p.opcode == servo::DELTA_ANGLE);
        int32_t fin = p.args[0];
        int32_t angle = p.args[1];

        if (fin == servo::LEFT || fin == servo::RIGHT)
        {
            int8_t &cur = (fin == servo::LEFT) ? leftFin_ : rightFin_;
            int8_t target = (int8_t)constrain(delta ? cur + angle : angle, servo::FIN_MIN, servo::FIN_MAX);
            // SET_ANGLE eases toward the target one step per message (same as the old controller, which
            // re-sent while the button was held). DELTA_ANGLE applies directly.
            cur = delta ? target : smoothStep(cur, target);
        }
        else if (fin == servo::BACK)
        {
            tailTrim_ = (int8_t)constrain(delta ? tailTrim_ + angle : angle, -TAIL_TRIM_LIMIT, TAIL_TRIM_LIMIT);
        }
        else
            return;
        break;
    }

    case servo::SET_SPEED:
        if (p.argc < 1)
            return;
        speed_ = (uint8_t)smoothStep(speed_, (int8_t)constrain(p.args[0], 0, servo::SPEED_MAX));
        break;

    case servo::RESET:
        calibrate();
        break;

    default:
        return;
    }

    printState();
}

void ServoModule::tick()
{
    fishWave();
}

void ServoModule::fishWave()
{
    if (speed_ <= 1)
        return;

    int amp = map(speed_, 0, 20, 10, 30);
    int speedDelay = map(speed_, 0, 20, 20, 5);

    if (millis() - lastWaveUpdate_ >= (unsigned long)speedDelay)
    {
        lastWaveUpdate_ = millis();

        wavePosition_ = constrain(wavePosition_ + waveDirection_, -amp, amp);
        if (wavePosition_ >= amp)
            waveDirection_ = -1;
        if (wavePosition_ <= -amp)
            waveDirection_ = 1;

        tailAngle_ = constrain(NEUTRAL + tailTrim_ + wavePosition_, 0, 180);
    }
}

int ServoModule::leftAngle() const
{
    return NEUTRAL + constrain(leftFin_, servo::FIN_MIN, servo::FIN_MAX);
}

int ServoModule::rightAngle() const
{
    return NEUTRAL + constrain(rightFin_, servo::FIN_MIN, servo::FIN_MAX);
}

void ServoModule::calibrate()
{
    tailTrim_ = 0;
    tailAngle_ = NEUTRAL;
    wavePosition_ = 0;
    leftFin_ = 0;
    rightFin_ = 0;
}

void ServoModule::printState()
{
    Serial.printf("[RX] servo: left=%d right=%d speed=%u tailTrim=%d\n", leftFin_, rightFin_, speed_, tailTrim_);
}
