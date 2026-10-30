/*
 * Finds the pulse widths that sit the A0090 at 0 degrees and at 180 degrees.
 *
 * The horn starts at mid-travel (1500 us). Nudge it until it is just short of
 * each mechanical stop, then mark that pulse. Grinding the horn into a stop
 * stalls the motor, so stop one nudge early.
 *
 * Serial monitor at 115200. One letter per line:
 *   a / d   shorter / longer by 20 us
 *   A / D   shorter / longer by 100 us
 *   c       return to 1500 us
 *   z       mark the current pulse as 0 degrees
 *   m       mark the current pulse as 180 degrees
 *   t       sweep once between the two marks
 *   p       print the two lines to paste into runServo.ino
 *
 * Signal is D10. Power is the 5 V pin. Ground is GND.
 */

#include <Servo.h>

constexpr int SERVO_PIN = D10;
constexpr int PULSE_CENTER_US = 1500;
constexpr int PULSE_MIN_US = 500;
constexpr int PULSE_MAX_US = 2500;
constexpr int NUDGE_FINE_US = 20;
constexpr int NUDGE_COARSE_US = 100;
constexpr uint32_t SWEEP_STEP_MS = 20;

Servo radarServo;

int pulseUs = PULSE_CENTER_US;
int usAt0 = 0;
int usAt180 = 0;

bool sweeping = false;
int sweepUs = 0;
int sweepStep = NUDGE_FINE_US;
uint32_t lastSweepMs = 0;

void holdPulse(int us) {
  if (us < PULSE_MIN_US) {
    us = PULSE_MIN_US;
  }
  if (us > PULSE_MAX_US) {
    us = PULSE_MAX_US;
  }
  pulseUs = us;
  radarServo.writeMicroseconds(pulseUs);
  Serial.print("pulse ");
  Serial.print(pulseUs);
  Serial.println(" us");
}

void printMarks() {
  Serial.print("0 deg ");
  if (usAt0 == 0) {
    Serial.println("not marked");
  } else {
    Serial.print(usAt0);
    Serial.println(" us");
  }
  Serial.print("180 deg ");
  if (usAt180 == 0) {
    Serial.println("not marked");
  } else {
    Serial.print(usAt180);
    Serial.println(" us");
  }
}

void printPasteLines() {
  if (usAt0 == 0 || usAt180 == 0 || usAt0 == usAt180) {
    Serial.println("mark both ends before copying");
    return;
  }
  Serial.println("paste into runServo.ino:");
  Serial.print("constexpr int SERVO_US_AT_0 = ");
  Serial.print(usAt0);
  Serial.println(";");
  Serial.print("constexpr int SERVO_US_AT_180 = ");
  Serial.print(usAt180);
  Serial.println(";");
}

void startSweep() {
  if (usAt0 == 0 || usAt180 == 0 || usAt0 == usAt180) {
    Serial.println("mark both ends first");
    return;
  }
  sweeping = true;
  sweepUs = usAt0;
  sweepStep = (usAt180 > usAt0) ? NUDGE_FINE_US : -NUDGE_FINE_US;
  lastSweepMs = millis();
  radarServo.writeMicroseconds(sweepUs);
  Serial.println("sweep");
}

void handleKey(char key) {
  if (key == 'a') {
    sweeping = false;
    holdPulse(pulseUs - NUDGE_FINE_US);
  } else if (key == 'd') {
    sweeping = false;
    holdPulse(pulseUs + NUDGE_FINE_US);
  } else if (key == 'A') {
    sweeping = false;
    holdPulse(pulseUs - NUDGE_COARSE_US);
  } else if (key == 'D') {
    sweeping = false;
    holdPulse(pulseUs + NUDGE_COARSE_US);
  } else if (key == 'c') {
    sweeping = false;
    holdPulse(PULSE_CENTER_US);
  } else if (key == 'z') {
    sweeping = false;
    usAt0 = pulseUs;
    Serial.print("marked 0 deg at ");
    Serial.print(usAt0);
    Serial.println(" us");
  } else if (key == 'm') {
    sweeping = false;
    usAt180 = pulseUs;
    Serial.print("marked 180 deg at ");
    Serial.print(usAt180);
    Serial.println(" us");
  } else if (key == 't') {
    startSweep();
  } else if (key == 'p') {
    printPasteLines();
  }
}

void serviceSweep() {
  if (!sweeping) {
    return;
  }
  uint32_t now = millis();
  if (static_cast<uint32_t>(now - lastSweepMs) < SWEEP_STEP_MS) {
    return;
  }
  lastSweepMs = now;
  sweepUs += sweepStep;
  bool reached = (sweepStep > 0 && sweepUs >= usAt180) || (sweepStep < 0 && sweepUs <= usAt180);
  if (reached) {
    sweepUs = usAt180;
    sweeping = false;
  }
  radarServo.writeMicroseconds(sweepUs);
  pulseUs = sweepUs;
  if (!sweeping) {
    Serial.println("sweep done");
    printMarks();
  }
}

void setup() {
  Serial.begin(115200);
  uint32_t startMs = millis();
  while (!Serial && static_cast<uint32_t>(millis() - startMs) < 3000) {
  }

  radarServo.attach(SERVO_PIN);
  holdPulse(PULSE_CENTER_US);
  Serial.println("calibration");
  Serial.println("a/d 20 us, A/D 100 us, c center, z mark 0, m mark 180, t sweep, p print");
}

void loop() {
  while (Serial.available() > 0) {
    char key = Serial.read();
    if (key != '\r' && key != '\n') {
      handleKey(key);
    }
  }
  serviceSweep();
}
