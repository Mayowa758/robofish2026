#include <Arduino.h>
#include <Bluepad32.h>
#include <comms.h>


/* COMMS STUFF */

#define RX1_PIN 14
#define TX1_PIN 12


FishState state;

/* GAMEPAD STUFF */
GamepadPtr gamepad;

#define L1 0x0010
#define L2 0x0040

#define R1 0x0020
#define R2 0x0080

#define DPAD_UP 0x01
#define DPAD_DOWN 0x02

#define SQUARE 0x0004


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

void onConnectedController(GamepadPtr gp) {
  Serial.println("Controller connected!");
  gamepad = gp;
}

void onDisconnectedController(GamepadPtr gp) {
  Serial.println("Controller disconnected!");
  gamepad = nullptr;
}

FishState generateTargetState() {
  FishState fs;
  uint16_t buttons = gamepad->buttons();
  uint16_t dpad = gamepad->dpad();

  if (buttons & L1) fs.leftFin = FIN_MAX;
  else if (buttons & L2) fs.leftFin = FIN_MIN;
  else fs.leftFin = NO_CHANGE;

  if (buttons & R1) fs.rightFin = FIN_MAX;
  else if (buttons & R2) fs.rightFin = FIN_MIN;
  else fs.rightFin = NO_CHANGE;

  if (dpad & DPAD_UP) fs.speed = SPEED_MAX;
  else if (dpad & DPAD_DOWN) fs.speed = SPEED_MIN;
  else fs.speed = NO_CHANGE;

  if (buttons & SQUARE) fs.calibrate = CALIBRATE;
  else fs.calibrate = NO_CHANGE;

  return fs;
}

void sendState(FishState fs) {
  uint32_t message_int = encode_state(fs);
  String str_num = String(message_int);
  Serial1.println(str_num);

  Serial.print("[TX] Data Sent (Decimal Value): ");
  Serial.println(message_int);

}


void setup() {
  Serial.begin(115200);
  Serial1.begin(115200, SERIAL_8N1, RX1_PIN, TX1_PIN);
  BP32.setup(&onConnectedController, &onDisconnectedController);
  BP32.forgetBluetoothKeys();
}


void loop() {
  BP32.update();
  if (gamepad && gamepad->isConnected()) {

    state = generateTargetState();
    sendState(state);

  delay(200);
}