#include <PS4USB.h>

USB Usb;
PS4USB PS4(&Usb);

void setup() {
  // Setting up the PS4 Controller
  if (Usb.Init() == -1) {
      Serial.println("USB Host Shield failed.");
      while (1);
  }

    Serial.print("The PS4 Controller has started!");
  }

}

void loop() {
  int16_t rightY = PS4.getAnalogHat(RY);
  Serial.println(RightHatY);
  delay(500);
}
