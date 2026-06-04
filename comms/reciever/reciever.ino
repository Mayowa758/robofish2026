#include <SoftwareSerial.h>


#define DE_RE 9

#define TRANSMIT_MODE HIGH
#define RECIEVE_MODE LOW



void setup() {

  pinMode(DE_RE, OUTPUT);
  digitalWrite(DE_RE, RECIEVE_MODE);    

  Serial.begin(9600);
  Serial1.begin(4800);

}


// Where message arrives 
void onMessageArrived(String message) {
  logMessage(message);

}

void logMessage(String message) {
  Serial.print("[RX] Got: ");
  Serial.println(message);

}

void loop() {
  if (Serial1.available()) {
    String received = Serial1.readStringUntil('\n');
    received.trim();
    onMessageArrived(received);
  }
}