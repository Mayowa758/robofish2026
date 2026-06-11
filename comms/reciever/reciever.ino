/*
  RECIEVER
*/
#include <SoftwareSerial.h>


#define DE_RE 9


#define TRANSMIT_MODE HIGH
#define RECIEVE_MODE LOW


typedef enum Instruction_ {
  CHANGE_SPEED, 
  TURN_LEFT_FLIPPER,
  TURN_RIGHT_FLIPPER,
  TURN_TOP_FLIPPER,
  CALIBRATE_ANGLES
} Instruction;




void setup() {

  pinMode(DE_RE, OUTPUT);
  digitalWrite(DE_RE, RECIEVE_MODE);    

  Serial.begin(9600);
  Serial1.begin(4800);

}


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

void decode_instruction(uint8_t message) {
  
}





//
// Servo
// Instructions: Speed
// MAIN_FLIPPER: SPEED
// 



// PROTOCOL
// 000 000 000 000
// ^   ^   ^   ^
// S1  S2  S3  S4
// Each servo
// 0       0     0
// ^.      ^
// On/Off   

// 000 000 000 000 
// Where message arrives 
void onMessageArrived(String message) {
  logMessage(message);

}

void logMessage(String message) {
  Serial.print("[RX] Got: ");
  Serial.println(message);

}

void loop() {
  if (Serial1.available()) {
    String received = Serial1.readStringUntil('\n');
    received.trim();
    onMessageArrived(received);
  }
}