// #include <Arduino.h>
// #include <comms.h>
// #include <ESP32Servo.h>

// /* Servo STUFF */

// Servo s1;
// Servo s2;
// Servo s3;

// const int S1_PIN = 38;
// const int S2_PIN = 39;
// const int S3_PIN = 40;

// int s1_neutral = 90;
// int s2_neutral = 90;
// int s3_neutral = 90;

// int wavePosition = -25;
// int waveDirection = 1;
// unsigned long lastWaveUpdate = 0;

// void calibrateServos();
// /* COMMS STUFF */

// #define RX1_PIN 14
// #define TX1_PIN 12

// // Sensitivity (s): Fraction of new state current state takes on
// // state[t + 1] = (1-s) * state[t] + s*target_state[t]
// #define SENSITIVITY 0.05

// FishState state;

// int8_t smoothStep(int8_t current, int8_t target)
// {
//     if (current == target)
//         return current;

//     float weighted_sum = (1 - SENSITIVITY) * current + target * SENSITIVITY;
//     int8_t next = (int8_t)roundf(weighted_sum);

//     if (next == current)
//     {
//         next = current + (target > current ? 1 : -1);
//     }

//     return next;
// }

// void tickState(FishState targetState)
// {
//     if (targetState.calibrate == CALIBRATE)
//     {
//         calibrateServos();
//         state.calibrate = NO_CHANGE;
//     }

//     if (targetState.leftFin != NO_CHANGE)
//     {
//         state.leftFin = smoothStep(state.leftFin, targetState.leftFin);
//     }

//     if (targetState.rightFin != NO_CHANGE)
//     {
//         state.rightFin = smoothStep(state.rightFin, targetState.rightFin);
//     }

//     if (targetState.speed != NO_CHANGE)
//     {
//         state.speed = smoothStep(state.speed, targetState.speed);
//     }
// }
// void print_state(FishState state)
// {
//     Serial.println("--- Fish State ---");

//     Serial.print("Left Fin:  ");
//     Serial.println(state.leftFin);

//     Serial.print("Right Fin: ");
//     Serial.println(state.rightFin);

//     Serial.print("Speed:     ");
//     Serial.println(state.speed);

//     Serial.print("Calibrate: ");
//     Serial.println(state.calibrate);

//     Serial.println("------------------");
// }

// void fishWave()
// {

//     int amp = map(state.speed, 0, 20, 10, 30);
//     int speedDelay = map(state.speed, 0, 20, 20, 5);
//     if (state.speed <= 1)
//         return;

//     if (state.speed > 1 && (millis() - lastWaveUpdate >= speedDelay))
//     {

//         lastWaveUpdate = millis();

//         wavePosition += waveDirection;

//         wavePosition = constrain(wavePosition, -amp, amp);

//         if (wavePosition >= amp)
//         {
//             waveDirection = -1;
//         }

//         if (wavePosition <= -amp)
//         {
//             waveDirection = 1;
//         }

//         int targetAngle = constrain(s1_neutral + wavePosition, 0, 180);

//         // Serial.print("S1 Write:");
//         // Serial.println(targetAngle);

//         s1.write(targetAngle);
//     }
// }

// void updateSideFins()
// {
//     s2.write(s2_neutral + constrain(state.leftFin, FIN_MIN, FIN_MAX));
//     s3.write(s3_neutral + constrain(state.rightFin, FIN_MIN, FIN_MAX));
// }

// void calibrateServos()
// {

//     s1_neutral = 90;
//     s2_neutral = 90;
//     s3_neutral = 90;

//     s1.write(s1_neutral);
//     s2.write(s2_neutral);
//     s3.write(s3_neutral);

//     wavePosition = 0;

//     state.leftFin = 0;
//     state.rightFin = 0;
// }

// void setup()
// {
//     Serial.begin(115200);

//     Serial1.begin(115200, SERIAL_8N1, RX1_PIN, TX1_PIN);

//     s1.attach(S1_PIN);
//     s2.attach(S2_PIN);
//     s3.attach(S3_PIN);

//     // Neutral positions
//     s1.write(s1_neutral);
//     s2.write(s2_neutral);
//     s3.write(s3_neutral);

//     delay(1000);
// }

// void loop()
// {
//     if (Serial1.available())
//     {
//         String message = Serial1.readStringUntil('\n');
//         message.trim();

//         uint32_t int_message = (uint32_t)strtoul(message.c_str(), NULL, 10);

//         FishState targetState = decode_state(int_message);
//         tickState(targetState);

//         print_state(state);
//     }

//     // Keep swimming
//     fishWave();

//     // Apply fin angles
//     updateSideFins();
// }

#include <Arduino.h>
#include <Update.h>

#define RX1_PIN 14
#define TX1_PIN 12
#define LED 2

// CHANGE THIS VALUE TO TEST A DIFFERENT BLINK RATE
const unsigned long BLINK_INTERVAL = 500;

unsigned long previousBlink = 0;
bool ledState = LOW;

void handleUpdate()
{
    Serial1.read(); // Consume 'U'

    Serial.println("[RX] Update request received");

    uint32_t firmwareSize = 0;

    if (Serial1.readBytes(
            (uint8_t *)&firmwareSize,
            sizeof(firmwareSize)) != sizeof(firmwareSize))
    {
        Serial.println("[RX] Failed to read firmware size");
        Serial1.write('N');
        return;
    }

    Serial.printf("[RX] Firmware size: %lu bytes\n",
                  (unsigned long)firmwareSize);

    if (firmwareSize == 0 || !Update.begin(firmwareSize))
    {
        Serial.printf("[RX] Update.begin failed: %d\n",
                      Update.getError());
        Serial1.write('N');
        return;
    }

    Serial1.write('A');
    Serial1.flush();

    uint8_t buffer[512];
    uint32_t received = 0;

    while (received < firmwareSize)
    {
        size_t wanted = min(
            (uint32_t)sizeof(buffer),
            firmwareSize - received);

        size_t count = Serial1.readBytes(buffer, wanted);

        if (count != wanted)
        {
            Serial.println("[RX] Transfer timed out");
            Update.abort();
            Serial1.write('N');
            return;
        }

        if (Update.write(buffer, count) != count)
        {
            Serial.println("[RX] Flash write failed");
            Update.abort();
            Serial1.write('N');
            return;
        }

        received += count;

        Serial1.write('A');
        Serial1.flush();
    }

    if (Update.end(true))
    {
        Serial.println("[RX] Update successful. Restarting...");

        Serial1.write('A');
        Serial1.flush();

        delay(200);
        ESP.restart();
    }
    else
    {
        Serial.printf("[RX] Update finalisation failed: %d\n",
                      Update.getError());
        Serial1.write('N');
    }
}

void setup()
{
    Serial.begin(115200);
    Serial1.begin(115200, SERIAL_8N1, RX1_PIN, TX1_PIN);
    Serial1.setTimeout(3000);

    pinMode(LED, OUTPUT);
    digitalWrite(LED, LOW);

    Serial.println("[RX] Blink test ready");
}

void loop()
{
    // Prioritise firmware updates over normal operation.
    if (Serial1.available() > 0 && Serial1.peek() == 'U')
    {
        handleUpdate();
        return;
    }

    // Blink continuously without blocking the update handler.
    unsigned long currentTime = millis();

    if (currentTime - previousBlink >= BLINK_INTERVAL)
    {
        previousBlink = currentTime;
        ledState = !ledState;
        digitalWrite(LED, ledState);
    }
}
