/*
  TRANSMITTER
*/

#include <SoftwareSerial.h>
#define DE_RE 9
#define RS485_RX 10
#define RS485_TX 11

#define TRANSMIT_MODE HIGH
#define RECIEVE_MODE LOW


SoftwareSerial rs485(RS485_RX, RS485_TX);


typedef enum Instruction_ {
  CHANGE_SPEED, 
  TURN_LEFT_FLIPPER,
  TURN_RIGHT_FLIPPER,
  TURN_TOP_FLIPPER,
  CALIBRATE_ANGLES
} Instruction;

void setup() {
  pinMode(DE_RE, OUTPUT);
  digitalWrite(DE_RE, TRANSMIT_MODE);
  Serial.begin(9600);
  rs485.begin(4800);
}


// Encode Instruction
// INSTRUCTIONS TO BE SENT
// 1: CHANGE_SPEED {option}
// 2: TURN_LEFT_FLIPPER {Angle} 
// 3: TURN_RIGHT_FLIPPER {Angle}
// 4: TURN_TOP_FLIPPER {Angle}
// 5: CALIBRATE_ANGLES

// EACH STEP IS 180/32 approx 6 degrees


// 000       00000
// ^ inst   ^ paramater

// E.G TURN_LEFT 30 (2 5)
// Instruction sent: 2 5 -> 010 00101

uint8_t encode_message(Instruction inst, uint8_t paramater) {
  return 0;
}

// Transmit message
void transmit(String message) {
  digitalWrite(DE_RE, TRANSMIT_MODE);
  rs485.println(message);
  delay(10);
  digitalWrite(DE_RE, RECIEVE_MODE);
}

void loop() {
  String message = "Hello";
  transmit(message);
  Serial.print("[TX] Sent: ");
  Serial.println();
  delay(1000);
}