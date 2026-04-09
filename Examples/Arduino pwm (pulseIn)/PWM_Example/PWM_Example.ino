
#define PWM_PIN 8

//The maximmum distance that is measured, the same that is set in the sensor (default 500 mm)
#define MAX_DISTANCE_MM 500

//The maximum, minimum expected pusle in microseconds, in theory it should be 2000, 1000 but since there are inacuracies we can to tune,
#define MAX_PUSLE_VALUE_US 2000
#define MIN_PUSLE_VALUE_US 1000
#define PULSE_RANGE (MAX_PUSLE_VALUE_US - MIN_PUSLE_VALUE_US)

void setup() {
  // put your setup code here, to run once:
  pinMode(PWM_PIN, INPUT);
  Serial.begin(9600);
}

void loop() {
  unsigned long pulseWidth = pulseIn(PWM_PIN, HIGH);
  int distance_mm;
  if (pulseWidth < MIN_PUSLE_VALUE_US) distance_mm = 0;
  else if (pulseWidth > MAX_PUSLE_VALUE_US) distance_mm = MAX_DISTANCE_MM;
  else distance_mm = ((pulseWidth - MIN_PUSLE_VALUE_US) * MAX_DISTANCE_MM) / PULSE_RANGE; //convert to mm
  Serial.print("distance: ");
  Serial.print(distance_mm);
  Serial.println(" mm");
  delay(200);
}
