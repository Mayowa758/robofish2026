/*
  TRANSMITTER
*/

#include <SoftwareSerial.h>
#include <XBOXONE.h>
#include <Servo.h>


// Servo Pins (probs should be in receiver)

Servo s1;
Servo s2;
Servo s3;
Servo s4;

const int S1_PIN = 9;
const int S2_PIN = 10;
const int S3_PIN = 7;
const int S4_PIN = 6;

int neutral_pos = 90

USB usb;
XBOXONE Xbox(&usb);

int LHX, LHY, RHX, RHY;
const long interval = 100;


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


  s1.attach(S1_PIN);
  s2.attach(S2_PIN);
  s3.attach(S3_PIN);

  //Neutral positions
  s1.write(s1_neutral);
  s2.write(s2_neutral);
  s3.write(s3_neutral);

  // Setting up the Xbox Controller
  if (Usb.Init() == -1) {
    Serial.println("The Xbox Controller has not been connected");
    while (1);
  }

  Serial.print("The Xbox Controller has started!");
}

xbox_controller() {
  Usb.Task();
  if (Xbox.XboxOneConnected) {
    int16_t LHX = Xbox.getAnalogHat(LeftHatX);
    int16_t LHY = Xbox.getAnalogHat(LeftHatY);
    int16_t RHX = Xbox.getAnalogHat(RightHatX);
    int16_t RHY = Xbox.getAnalogHat(RightHatX);

    bool increase_speed = Xbox.getButtonClick(LT);
    bool decrease_speed = Xbox.getButtonClick(RT);

    // I need to do some mapping of values some maths 

  }  
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