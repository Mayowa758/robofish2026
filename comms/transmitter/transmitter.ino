#include <SoftwareSerial.h>
#define DE_RE 9
#define RS485_RX 10
#define RS485_TX 11

#define TRANSMIT_MODE HIGH
#define RECIEVE_MODE LOW


SoftwareSerial rs485(RS485_RX, RS485_TX);


void setup() {
  pinMode(DE_RE, OUTPUT);
  digitalWrite(DE_RE, TRANSMIT_MODE);
  Serial.begin(9600);
  rs485.begin(4800);
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