#include <Arduino.h>
#include <comms.h>


/* COMMS STUFF */

#define RX1_PIN 14
#define TX1_PIN 12

// Sensitivity (s): Fraction of new state current state takes on
// state[t + 1] = (1-s) * state[t] + s*target_state[t]
#define SENSITIVITY 0.1

FishState state;

void tickState(FishState targetState) {
    if (targetState.calibrate != NO_CHANGE) {
        state.calibrate = targetState.calibrate;
    }

    if (targetState.leftFin != NO_CHANGE) {
        state.leftFin = (int8_t) roundf((1 - SENSITIVITY) * state.leftFin + targetState.leftFin * SENSITIVITY);
    }

    if (targetState.rightFin != NO_CHANGE) {
        state.rightFin = (int8_t) roundf((1 - SENSITIVITY) * state.rightFin + targetState.rightFin * SENSITIVITY);
    }

    if (targetState.speed != NO_CHANGE) {
        state.speed = (int8_t) roundf((1 - SENSITIVITY) * state.speed + targetState.speed * SENSITIVITY);
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
        Serial.print("[RX] Data Recieved (Decimal Value): ");
        Serial.println(int_message);

        FishState targetState = decode_state(int_message);
        tickState(targetState);

        print_state(state);
    }
}
