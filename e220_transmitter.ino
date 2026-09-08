#include <SoftwareSerial.h>

SoftwareSerial e220Serial(2, 3); // RX, TX

void setup() {
  Serial.begin(9600);
  e220Serial.begin(9600);
  Serial.println("E220 ready");
}

void loop() {
  e220Serial.print("HELLO");
  Serial.println("Sent: HELLO");
  delay(5000);
}
