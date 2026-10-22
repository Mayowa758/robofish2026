#include <Arduino.h>
#include <ESP32Servo.h>
#include "Endpoint.h"
#include "ReceiverUpdater.h"
#include "servo_module.h"
#include "sensor_module.h"

#define RX1_PIN 14
#define TX1_PIN 12
#define LED 2

#define S1_PIN 38 // tail
#define S2_PIN 39 // left fin
#define S3_PIN 40 // right fin

const unsigned long BLINK_INTERVAL = 500;
unsigned long previousBlink = 0;
bool ledState = LOW;

// Servos are global in this file, exactly like the original receiver code.
Servo s1, s2, s3;

Endpoint uplink(Serial1);

// One module per subsystem. To add a subsystem: write a Module, add it to proto::Subsys, uplink.add() it here.
ServoModule servos;
SensorModule sensors;
UpdateModule updater;

static bool attachServo(Servo &servo, int pin, const char *name)
{
    Serial.printf("[RX] attaching %s on GPIO %d\n", name, pin);
    const int channel = servo.attach(pin, 1000, 2000);
    if (channel == 0 || !servo.attached())
    {
        Serial.printf("[RX] ERROR: %s attach failed (channel %d)\n", name, channel);
        return false;
    }

    servo.write(ServoModule::NEUTRAL);
    Serial.printf("[RX] %s attached (channel %d)\n", name, channel);
    return true;
}

void setup()
{
    Serial.begin(115200);
    Serial.println("[RX] boot");

    Serial1.setRxBufferSize(1024); // a firmware chunk frame is ~540 bytes; must be set before begin()
    Serial1.begin(115200, SERIAL_8N1, RX1_PIN, TX1_PIN);

    pinMode(LED, OUTPUT);
    digitalWrite(LED, LOW);

    Serial.println("[RX] Attaching servos...");

    s1.attach(S1_PIN);
    s2.attach(S2_PIN);
    s3.attach(S3_PIN);

    Serial.println("[RX] Servos attached");

    s1.write(90);
    s2.write(90);
    s3.write(90);

    Serial.println("[RX] Servos set to neutral");

    const bool tailOk = attachServo(s1, S1_PIN, "tail");
    const bool leftOk = attachServo(s2, S2_PIN, "left fin");
    const bool rightOk = attachServo(s3, S3_PIN, "right fin");
    Serial.printf("[RX] servo init: tail=%s left=%s right=%s\n",
                  tailOk ? "ok" : "FAILED",
                  leftOk ? "ok" : "FAILED",
                  rightOk ? "ok" : "FAILED");

    uplink.add(servos);
    uplink.add(sensors);
    uplink.add(updater);

    Serial.println("[RX] Ready");
}

void loop()
{
    uplink.poll(); // reads the UART, dispatches packets, advances the tail wave / update timeout

    s1.write(servos.tailAngle());
    s2.write(servos.leftAngle());
    s3.write(servos.rightAngle());

    unsigned long now = millis();

    if (now - previousBlink >= BLINK_INTERVAL)
    {
        previousBlink = now;
        ledState = !ledState;
        digitalWrite(LED, ledState);
    }
}
