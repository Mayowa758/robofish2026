#include <SoftwareSerial.h>

#define ENABLE_PIN 8
#define RX_PIN 10
#define TX_PIN 11

SoftwareSerial mySerial(RX_PIN, TX_PIN);
void setup() {

  Serial.begin(115200);
  mySerial.begin(9600);

  
  pinMode(ENABLE_PIN, OUTPUT);

  digitalWrite(ENABLE_PIN, HIGH);


}

void loop() {
  int data = random(0, 100);
  mySerial.write(data);

  Serial.print("Data sent: ");
  Serial.println(data);

  delay(2000);

}