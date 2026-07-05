#include <Arduino.h>
#include <comms.h>


/* COMMS STUFF */

#define RX1_PIN 14
#define TX1_PIN 12

// Sensitivity (s): Fraction of new state current state takes on
// state[t + 1] = (1-s) * state[t] + s*target_state[t]
#define SENSITIVITY 0.05

FishState state;


int8_t smoothStep(int8_t current, int8_t target) {
    if (current == target) return current;

    float weighted_sum = (1 - SENSITIVITY) * current + target * SENSITIVITY;
    int8_t next = (int8_t) roundf(weighted_sum);

    if (next == current) {
        next = current + (target > current ? 1 : -1);
    }

    return next;
}

void tickState(FishState targetState) {
    if (targetState.calibrate != NO_CHANGE) {
        state.calibrate = targetState.calibrate;
    }

    if (targetState.leftFin != NO_CHANGE) {
        state.leftFin = smoothStep(state.leftFin, targetState.leftFin);
    }

    if (targetState.rightFin != NO_CHANGE) {
        state.rightFin = smoothStep(state.rightFin, targetState.rightFin);
    }

    if (targetState.speed != NO_CHANGE) {
        state.speed = smoothStep(state.speed, targetState.speed);
    }
}
void print_state(FishState state) {
  Serial.println("--- Fish State ---");
  
  Serial.print("Left Fin:  ");
  Serial.println(state.leftFin);
  
  Serial.print("Right Fin: ");
  Serial.println(state.rightFin);
  
  Serial.print("Speed:     ");
  Serial.println(state.speed);
  
  Serial.print("Calibrate: ");
  Serial.println(state.calibrate);
  
  Serial.println("------------------");
}

void setup() {
    Serial.begin(115200);

    Serial1.begin(115200, SERIAL_8N1, RX1_PIN, TX1_PIN);
}

void loop() {
    if (Serial1.available()) {
        String message = Serial1.readStringUntil('\n');
        message.trim();
        
        uint32_t int_message = (uint32_t) strtoul(message.c_str(), NULL, 10);

        FishState targetState = decode_state(int_message);
        tickState(targetState);

        print_state(state);
    }
}
