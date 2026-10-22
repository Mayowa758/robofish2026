#include <Arduino.h>
#include <Bluepad32.h>
#include "Endpoint.h"
#include "receiver_firmware.h"
#include "TransmitterUpdater.h"

#define RX1_PIN 14
#define TX1_PIN 12

using namespace proto;

Endpoint uplink(Serial1);

/* GAMEPAD STUFF */
GamepadPtr gamepad;

#define L1 0x0010
#define L2 0x0040
#define R1 0x0020
#define R2 0x0080

#define DPAD_UP 0x01
#define DPAD_DOWN 0x02

#define SQUARE 0x0004

const unsigned long SEND_INTERVAL = 200; // same rate as the old controller loop
unsigned long lastSend = 0;

void onConnectedController(GamepadPtr gp)
{
  Serial.println("Controller connected!");
  gamepad = gp;
}

void onDisconnectedController(GamepadPtr gp)
{
  Serial.println("Controller disconnected!");
  gamepad = nullptr;
}

// Turn the held buttons into protocol packets. Nothing held = nothing sent.
void sendControllerCommands()
{
  uint16_t buttons = gamepad->buttons();
  uint16_t dpad = gamepad->dpad();

  if (buttons & L1)
    uplink.send(Subsys::SERVO, servo::SET_ANGLE, {servo::LEFT, servo::FIN_MAX});
  else if (buttons & L2)
    uplink.send(Subsys::SERVO, servo::SET_ANGLE, {servo::LEFT, servo::FIN_MIN});

  if (buttons & R1)
    uplink.send(Subsys::SERVO, servo::SET_ANGLE, {servo::RIGHT, servo::FIN_MAX});
  else if (buttons & R2)
    uplink.send(Subsys::SERVO, servo::SET_ANGLE, {servo::RIGHT, servo::FIN_MIN});

  if (dpad & DPAD_UP)
    uplink.send(Subsys::SERVO, servo::SET_SPEED, {servo::SPEED_MAX});
  else if (dpad & DPAD_DOWN)
    uplink.send(Subsys::SERVO, servo::SET_SPEED, {servo::SPEED_MIN});

  if (buttons & SQUARE)
    uplink.send(Subsys::SERVO, servo::RESET);
}

void updateReceiver()
{
  Serial.printf("Sending receiver firmware: %lu bytes\n", (unsigned long)receiverFirmwareSize);
  bool success = sendReceiverFirmware(Serial1, receiverFirmware, receiverFirmwareSize);
  Serial.println(success ? "Update successful" : "Update failed"); // update_receiver.py looks for these
}

void setup()
{
  Serial.begin(115200);
  Serial1.setRxBufferSize(1024); // must be set before begin()
  Serial1.begin(115200, SERIAL_8N1, RX1_PIN, TX1_PIN);
  BP32.setup(&onConnectedController, &onDisconnectedController);
  BP32.forgetBluetoothKeys();

  // Replies from the receiver (e.g. SENSOR_DATA) arrive via uplink.poll(); add a Module per subsystem here.

  Serial.println("Ready. Send 'u' in Serial Monitor to update receiver.");
}

void loop()
{
  BP32.update();
  uplink.poll();

  if (Serial.available() > 0 && Serial.read() == 'u')
    updateReceiver();

  if (gamepad && gamepad->isConnected() && millis() - lastSend >= SEND_INTERVAL)
  {
    lastSend = millis();
    sendControllerCommands();
  }
}
