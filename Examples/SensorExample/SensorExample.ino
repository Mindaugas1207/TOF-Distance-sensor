
#include <SoftwareSerial.h>
#include "DistanceSensor.h"

#define SENSOR_PIN 3
OneWireSerial oneWire(SENSOR_PIN);
DistanceSensor sensor0(&oneWire, 0);
//Sensor sensor1(&oneWire, 1);
//Sensor sensor2(&oneWire, 2);
//Sensor sensor3(&oneWire, 3);

void setup() {
  Serial.begin(9600);
  oneWire.begin(9600);
}

void loop() {
  unsigned int distance0 = sensor0.readDistance_mm();
  //unsigned int distance1 = sensor1.readDistance_mm();
  //unsigned int distance2 = sensor2.readDistance_mm();
  //unsigned int distance3 = sensor3.readDistance_mm();

  Serial.print("distance0 = ");
  Serial.print(distance0);
  //Serial.print("distance1 = ");
  //Serial.print(distance1);
  //Serial.print("distance2 = ");
  //Serial.print(distance2);
  //Serial.print("distance3 = ");
  //Serial.print(distance3);
  Serial.println("");
  delay(100);
}

