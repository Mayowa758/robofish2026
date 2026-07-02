/*
  TRANSMITTER
*/

#include <PS4USB.h>

USB Usb;
PS4USB PS4(&Usb);

const long interval = 100;

#define JOYSTICK_DEADZONE 20



// Instruction State
typedef enum Instruction_ {
  CHANGE_SPEED = 0, 
  SET_LEFT_FIN = 1,
  SET_RIGHT_FIN = 2,
  STOP = 3,
  CALIBRATE_ANGLES = 4,
  
} Instruction;

// State of the fish
struct FishState {
  int leftFin = 0;
  int rightFin = 0;
  int speed = 15; // need to tune this later
};

FishState fish;
FishState previousFish = {-1, -1, -1};


void setup() {
  Serial.begin(9600);
  Serial1.begin(4800);

  // Setting up the PS4 Controller
 if (Usb.Init() == -1)
{
    Serial.println("USB Host Shield failed.");
    while (1);
}

  Serial.print("The PS4 Controller has started!");
}

void read_controller() {
  Usb.Task();
  if (!PS4.connected()) {
    return;
  }

  // Speed
  if (PS4.getButtonClick(UP))
      fish.speed++;

  if (PS4.getButtonClick(DOWN))
      fish.speed--;
  
  fish.speed = constrain(fish.speed, 0, 31);

  // Left fin
  if (PS4.getButtonClick(L1))
      fish.leftFin++;

  if (PS4.getButtonClick(L2))
      fish.leftFin--;

  fish.leftFin = constrain(fish.leftFin, -15, 15);

  // Right fin
  if (PS4.getButtonClick(R1))
      fish.rightFin++;

  if (PS4.getButtonClick(R2))
      fish.rightFin--;

  fish.rightFin = constrain(fish.rightFin, -15, 15); 

  // For controlling up and down movement of the fish
  control_tilt(); 

  // Stop Command - stops the fish abruptly
  if (PS4.getButtonClick(CIRCLE)) {
    fish = {0, 0, 0};
  }

  // Calibrate Commands - sets the fish back to base values
  if (PS4.getButtonClick(TRIANGLE)) {
    fish = {0 ,0, 15}; // 
  }
}

void control_tilt() {
  int16_t rightY = PS4.getAnalogHat(RightHatY);

  // Shift joystick centre to zero
  int tilt = rightY - 127;

  // Remove joystick noise around centre
  if (abs(tilt) < JOYSTICK_DEADZONE) {
    tilt = 0;
  }
  else {
    // Convert joystick movement into fin angle
    tilt = map(tilt, -127, 128, -15, 15);
  }

  // Apply same tilt angle to both fins
  fish.leftFin = constrain(tilt, -15, 15);
  fish.rightFin = constrain(tilt, -15, 15);
}


// Function for sending out updates
void send_updates()
{
  if (fish.speed != previousFish.speed)
  {
      transmit(encode_message(CHANGE_SPEED, fish.speed));
      previousFish.speed = fish.speed;
  }

  if (fish.leftFin != previousFish.leftFin)
  {
      transmit(encode_message(SET_LEFT_FIN, fish.leftFin + 15));
      previousFish.leftFin = fish.leftFin;
  }

  if (fish.rightFin != previousFish.rightFin)
  {
      transmit(encode_message(SET_RIGHT_FIN, fish.rightFin + 15));
      previousFish.rightFin = fish.rightFin;
  }
}

// Encode Instruction
// INSTRUCTIONS TO BE SENT
// 1: CHANGE_SPEED {option}
// 2: TURN_LEFT_FLIPPER {Angle} 
// 3: TURN_RIGHT_FLIPPER {Angle}
// 4: STOP {any value i think}
// 5: CALIBRATE_ANGLES

// EACH STEP IS 180/32 approx 6 degrees


// 000       00000
// ^ inst   ^ paramater

// E.G TURN_LEFT 30 (2 5)
// Instruction sent: 2 5 -> 010 00101

uint8_t encode_message(Instruction inst, uint8_t parameter) {
  return (inst << 5) | (parameter & 0x1F);
}

void transmit(uint8_t packet) {
  String strNum = String(packet);
  Serial1.println(strNum);

  Serial.print("[TX] Data Sent (Decimal Value): ");
  Serial.println(packet);
}


void loop()
{
  read_controller();
  send_updates();
}

// Transmit message - For Testing/Debugging Purposes
// void transmit(String packet) {
//   digitalWrite(DE_RE, TRANSMIT_MODE);
//   rs485.println(packet);
//   delay(10);
//   digitalWrite(DE_RE, RECIEVE_MODE);
// }

// void loop() {
//   read_controller();
//   String message = "Hello";
//   transmit(message);
//   Serial.print("[TX] Sent: ");
//   Serial.println();
//   delay(1000);
// }