// Receiver: servo and sensor are plain functions in this file (no classes of our own).
// UPDATE (firmware over the link) lives in receiver_update.cpp. Only lib/Protocol is needed. Add a subsystem = add a case in handlePacket().
#include <Arduino.h>
#include <ESP32Servo.h>
#include "Protocol.h"
#include "receiver_update.h" // handleUpdate(), tickUpdate(), updateIsActive()

#define RX1_PIN 14
#define TX1_PIN 12
#define LED 2

#define S1_PIN 38 // tail
#define S2_PIN 39 // left fin
#define S3_PIN 40 // right fin

/* SERVO */
Servo s1, s2, s3; // tail, left fin, right fin

const int NEUTRAL = 90;
const int TAIL_TRIM_LIMIT = 40;
const float SENSITIVITY = 0.05f; // state[t+1] = (1-s)*state[t] + s*target

int leftFin = 0, rightFin = 0; // degrees relative to neutral
int tailTrim = 0;              // shifts the centre the tail waves around
int speedSetting = 1;          // tail wave speed (<=1 = no wave)
int wavePosition = 0, waveDirection = 1;
unsigned long lastWave = 0;
int lastTail = -1, lastLeft = -1, lastRight = -1; // last angle written, to avoid rewriting PWM
unsigned long lastPrint = 0;

proto::Parser parser; // reads bytes off Serial1 in loop()

const unsigned long BLINK_INTERVAL = 500;
unsigned long previousBlink = 0;
bool ledState = LOW;

// Sending replies (receiver -> transmitter)
void sendReply(uint8_t subsys, uint8_t opcode, uint8_t argc, int32_t a0 = 0, int32_t a1 = 0, int32_t a2 = 0, int32_t a3 = 0)
{
    proto::Packet p;
    p.subsys = subsys;
    p.opcode = opcode;
    p.argc = argc;
    p.args[0] = a0;
    p.args[1] = a1;
    p.args[2] = a2;
    p.args[3] = a3;

    uint8_t buf[proto::MAX_FRAME];
    size_t n = proto::encode(p, buf, sizeof buf);
    if (n > 0)
        Serial1.write(buf, n);
}

/* SERVO */
int smoothStep(int current, int target)
{
    if (current == target)
        return current;
    int next = (int)roundf((1 - SENSITIVITY) * current + target * SENSITIVITY);
    if (next == current)
        next += (target > current) ? 1 : -1;
    return next;
}

// Only touch the PWM when the angle actually changed.
void writeServo(Servo &s, int &last, int angle, bool force)
{
    angle = constrain(angle, 0, 180);
    if (force || angle != last)
    {
        s.write(angle);
        last = angle;
    }
}

void updateFins(bool force)
{
    writeServo(s2, lastLeft, NEUTRAL + leftFin, force);
    writeServo(s3, lastRight, NEUTRAL + rightFin, force);
}

void updateTail(bool force)
{
    writeServo(s1, lastTail, NEUTRAL + tailTrim + (speedSetting > 1 ? wavePosition : 0), force);
}

void printServoState() // throttled: the controller re-sends while a button is held
{
    if (millis() - lastPrint < 250)
        return;
    lastPrint = millis();
    Serial.printf("[RX] servo: left=%d right=%d speed=%d tailTrim=%d\n", leftFin, rightFin, speedSetting, tailTrim);
}

void handleServo(const proto::Packet &p)
{
    switch (p.opcode)
    {
    case proto::servo::SET_ANGLE:
    case proto::servo::DELTA_ANGLE:
    {
        if (p.argc < 2)
            return;
        bool delta = (p.opcode == proto::servo::DELTA_ANGLE);
        int fin = p.args[0];
        int angle = p.args[1];

        if (fin == proto::servo::LEFT || fin == proto::servo::RIGHT)
        {
            int &cur = (fin == proto::servo::LEFT) ? leftFin : rightFin;
            int target = constrain(delta ? cur + angle : angle, proto::servo::FIN_MIN, proto::servo::FIN_MAX);
            cur = delta ? target : smoothStep(cur, target); // SET_ANGLE eases, one step per message
        }
        else if (fin == proto::servo::BACK)
        {
            tailTrim = constrain(delta ? tailTrim + angle : angle, -TAIL_TRIM_LIMIT, TAIL_TRIM_LIMIT);
        }
        else
            return;
        break;
    }

    case proto::servo::SET_SPEED:
        if (p.argc < 1)
            return;
        speedSetting = smoothStep(speedSetting, constrain((int)p.args[0], 0, proto::servo::SPEED_MAX));
        break;

    case proto::servo::RESET:
        leftFin = rightFin = tailTrim = 0;
        wavePosition = 0;
        break;

    default:
        return;
    }

    updateFins(false);
    updateTail(false);
    printServoState();
}

void tickTailWave()
{
    if (updateIsActive() || speedSetting <= 1) // servos stay still during a firmware update
        return;

    int amp = map(speedSetting, 0, 20, 10, 30);
    int speedDelay = map(speedSetting, 0, 20, 20, 5);

    if (millis() - lastWave >= (unsigned long)speedDelay)
    {
        lastWave = millis();
        wavePosition = constrain(wavePosition + waveDirection, -amp, amp);
        if (wavePosition >= amp)
            waveDirection = -1;
        if (wavePosition <= -amp)
            waveDirection = 1;
        updateTail(false);
    }
}

// SENSOR   GET {sensor} -> reply DATA {sensor, v0, v1, ...}
bool readGyro(int32_t &x, int32_t &y, int32_t &z)
{
    (void)x;
    (void)y;
    (void)z;
    return false; // no gyro wired yet: put the real sensor read here
}

void handleSensor(const proto::Packet &p)
{
    if (p.opcode != proto::sensor::GET || p.argc < 1)
        return;

    if (p.args[0] == proto::sensor::GYRO)
    {
        int32_t x = 0, y = 0, z = 0;
        if (readGyro(x, y, z))
            sendReply(proto::Subsys::SENSOR, proto::sensor::DATA, 4, proto::sensor::GYRO, x, y, z);
        else
            Serial.println("[RX] SENSOR GET gyro: no gyro connected");
    }
}

// Subsystem
void handlePacket(const proto::Packet &p)
{
    switch (p.subsys)
    {
    case proto::Subsys::SERVO:
        handleServo(p);
        break;
    case proto::Subsys::SENSOR:
        handleSensor(p);
        break;
    case proto::Subsys::UPDATE:
        handleUpdate(p);
        break;
    default:
        Serial.printf("[RX] unknown subsys %u (opcode %u)\n", p.subsys, p.opcode);
        break;
    }
}

void setup()
{
    Serial.begin(115200);
    Serial.println("[RX] boot");

    Serial1.setRxBufferSize(1024); // a firmware chunk frame is ~540 bytes; must be set before begin()
    Serial1.begin(115200, SERIAL_8N1, RX1_PIN, TX1_PIN);

    pinMode(LED, OUTPUT);
    digitalWrite(LED, LOW);

    s1.attach(S1_PIN);
    s2.attach(S2_PIN);
    s3.attach(S3_PIN);
    Serial.printf("[RX] servos attached: tail=%d left=%d right=%d\n", s1.attached(), s2.attached(), s3.attached());

    updateFins(true); // all to neutral
    updateTail(true);

    Serial.println("[RX] Ready");
}

void loop()
{
    Serial.println("Hello");
    delay(1000);
    while (Serial1.available() > 0)
    {
        int b = Serial1.read();
        if (b < 0)
            break;
        if (parser.feed((uint8_t)b, millis()))
            handlePacket(parser.packet());
    }

    tickTailWave();
    tickUpdate();

    unsigned long now = millis();
    if (now - previousBlink >= BLINK_INTERVAL)
    {
        previousBlink = now;
        ledState = !ledState;
        digitalWrite(LED, ledState);
    }
}