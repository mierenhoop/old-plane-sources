#include <ESP8266WiFi.h>

void setup() {
  Serial.begin(115200);
  Serial.println();
  String address = WiFi.macAddress();
  address.replace(":", ", 0x");
  Serial.println("{ 0x" + address + " }");
}

void loop() {}