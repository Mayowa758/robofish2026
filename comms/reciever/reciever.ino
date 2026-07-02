/*
  RECIEVER
*/
#include <SoftwareSerial.h>


#define SERVO_PIN_LEFT 11
#define SERVO_PIN_RIGHT 12
#define SERVO_PIN_RIGHT 13

#define RX_PIN 10
#define TX_PIN 11

// INSTRUCTIONS TO BE SENT
// 0: CHANGE_SPEED {option}
// 1: TURN_LEFT_FLIPPER {Angle} 
// 2: TURN_RIGHT_FLIPPER {Angle}
// 3: TURN_TOP_FLIPPER {Angle}
// 4: CALIBRATE_ANGLES
typedef enum Instruction_ {
  CHANGE_SPEED = 0, 
  SET_LEFT_FIN = 1,
  SET_RIGHT_FIN = 2,
  STOP = 3,
  CALIBRATE_ANGLES = 4,
  
} Instruction;

typedef struct _Command {
  Instruction instruction;
  uint8_t paramater;
} Command;

typedef struct _FishState {
  int leftFin = 0;
  int rightFin = 0;
  int speed = 15; // need to tune this later
} FishState;

FishState fish;


SoftwareSerial Serial1(RX_PIN, TX_PIN);

void setup() {
  Serial.begin(9600);
  Serial1.begin(9600);
}


/* PROTOCOL */



// 000       00000
// ^ inst    ^ paramater

// E.G TURN_LEFT 30 (2 5)
// Instruction sent: 2 5 -> 010 00101

Command decode_command(String message) {

  uint8_t int_message = (uint8_t) message.toInt();
  Command c;
  c.instruction = (Instruction) ((int_message) >> 5);
  c.paramater = 0x1F & int_message;

  return c;
}


/*

  COMMANDS

*/
void change_speed(uint8_t speed) {
  fish.speed = speed;
}



void set_left_fin(uint8_t angle) {
  fish.leftFin = constrain(angle*6, 0, 180);
}

void set_right_fin(uint8_t angle) {
  fish.rightFin = constrain(angle*6, 0, 180);
}


// Put in neutral position first
void calibrate() {
  fish.rightFin = 90;
  fish.leftFin = 90;
}




void logMessage(String message) {
  Serial.print("[RX] Got: ");
  Serial.println(message);

}

void loop() {
  if (Serial1.available()) {
    String message = Serial1.readStringUntil('\n');
    message.trim();
    Command cmd = decode_command(message);
    Serial.print("Instruction: ");
    Serial.println(cmd.instruction);

    Serial.print("Paramater: ");
    Serial.println(cmd.paramater);
    
  }
}