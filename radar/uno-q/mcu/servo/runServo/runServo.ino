/*
 * Sweeps the A0090 from the calibrated 0 degree pulse to the calibrated
 * 180 degree pulse, then back, one degree at a time.
 *
 * Run servoCalibration first. Paste the two lines it prints over the
 * constants below. Until those pulses are filled in, this sketch holds
 * the signal line still.
 *
 * Signal is D10. Power is the 5 V pin. Ground is GND.
 */

#include <Servo.h>

constexpr int SERVO_PIN = D10;

// Paste from servoCalibration. Both must be between 500 and 2500, and different.
constexpr int SERVO_US_AT_0 = 0;
constexpr int SERVO_US_AT_180 = 0;

constexpr uint32_t SWEEP_STEP_MS = 15;

Servo radarServo;

int angleDeg = 0;
int angleStep = 1;
uint32_t lastStepMs = 0;
bool running = false;

bool calibrationReady() {
  bool minOk = SERVO_US_AT_0 >= 500 && SERVO_US_AT_0 <= 2500;
  bool maxOk = SERVO_US_AT_180 >= 500 && SERVO_US_AT_180 <= 2500;
  return minOk && maxOk && SERVO_US_AT_0 != SERVO_US_AT_180;
}

void setup() {
  Serial.begin(115200);
  uint32_t startMs = millis();
  while (!Serial && static_cast<uint32_t>(millis() - startMs) < 2000) {
  }

  if (!calibrationReady()) {
    Serial.println("paste SERVO_US_AT_0 and SERVO_US_AT_180 from servoCalibration");
    return;
  }

  radarServo.attach(SERVO_PIN, SERVO_US_AT_0, SERVO_US_AT_180);
  radarServo.write(0);
  running = true;
  lastStepMs = millis();
  Serial.print("sweep 0 to 180, ");
  Serial.print(SERVO_US_AT_0);
  Serial.print(" us -> ");
  Serial.print(SERVO_US_AT_180);
  Serial.println(" us");
}

void loop() {
  if (!running) {
    return;
  }

  uint32_t now = millis();
  if (static_cast<uint32_t>(now - lastStepMs) < SWEEP_STEP_MS) {
    return;
  }
  lastStepMs = now;

  angleDeg += angleStep;
  if (angleDeg >= 180) {
    angleDeg = 180;
    angleStep = -1;
  } else if (angleDeg <= 0) {
    angleDeg = 0;
    angleStep = 1;
  }

  radarServo.write(angleDeg);
  Serial.print("angle ");
  Serial.println(angleDeg);
}
