#include <Servo.h>

#define MAIN_SERVO = 12
#define LEFT_SERVO = 13
#define RIGHT_SERVO = 14
#define TOP_SERVO = 15

// angle for the 2 side fins
fin_angle = 0

void setup() {
  // seting up servos
  myServo.attach(MAIN_SERVO)
  myServo.attach(LEFT_SERVO)
  myServo.attach(RIGHT_SERVO)
  myServo.attach(TOP_SERVO)
}

void loop() {
  // nothing yet

}

// starter function for fin control
void fin_control(left, right, angle) {
  left.write(angle)
  right.write(180 - angle)
}
