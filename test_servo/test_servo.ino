#include <Servo.h>

Servo s1;
Servo s2;
Servo s3;

const int S1_PIN = 9;
const int S2_PIN = 10;
const int S3_PIN = 7;

int s1_neutral = 90;
int s2_neutral = 90;
int s3_neutral = 90;


void setup() {
  s1.attach(S1_PIN);
  s2.attach(S2_PIN);
  s3.attach(S3_PIN);

  s1.write(s1_neutral);
  s2.write(s2_neutral);
  s3.write(s3_neutral);

  delay(1000);
}

void loop() {
  fishWave();
}

void fishWave () {

  int amp = 25;
  int speedDelay = 25;

  for (int i= -amp; i <= amp; i++) {
    s1.write(s1_neutral + i);
    s2.write(s2_neutral - i * 0.5);
    s3.write(s2_neutral + i * 0.5);

    delay(speedDelay);

  }

  for (int i= amp; i >= -amp; i--) {
    s1.write(s1_neutral + i);
    s2.write(s2_neutral - i * 0.5);
    s3.write(s2_neutral + i * 0.5);

    delay(speedDelay);

  }

}