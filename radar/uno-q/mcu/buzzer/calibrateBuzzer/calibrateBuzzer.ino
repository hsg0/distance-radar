/*
 * Finds a pitch the buzzer on D6 can actually sound.
 *
 * Power is the 3.3 V pin. Ground is GND. Signal is D6.
 * The pin starts low, so the buzzer is quiet until a key arrives.
 *
 * Serial monitor at 115200. One letter per line:
 *   a / d   lower / higher by 50 Hz, then a short beep
 *   A / D   lower / higher by 400 Hz, then a short beep
 *   b       beep at the current pitch
 *   h       hold the pin high (for a buzzer that sounds on DC)
 *   s       silence
 *   z       mark this pitch
 *   p       print the line to paste into runBuzzer.ino
 */

constexpr int BUZZER_PIN = D6;
constexpr int FREQ_MIN_HZ = 100;
constexpr int FREQ_MAX_HZ = 5000;
constexpr int FREQ_START_HZ = 2000;
constexpr int NUDGE_FINE_HZ = 50;
constexpr int NUDGE_COARSE_HZ = 400;
constexpr uint32_t BEEP_MS = 180;
constexpr uint32_t HOLD_MS = 250;

int freqHz = FREQ_START_HZ;
int markedHz = 0;

void silence() {
  noTone(BUZZER_PIN);
  digitalWrite(BUZZER_PIN, LOW);
}

void clampFreq(int hz) {
  if (hz < FREQ_MIN_HZ) {
    hz = FREQ_MIN_HZ;
  }
  if (hz > FREQ_MAX_HZ) {
    hz = FREQ_MAX_HZ;
  }
  freqHz = hz;
}

void beep() {
  silence();
  tone(BUZZER_PIN, freqHz, BEEP_MS);
  Serial.print("beep ");
  Serial.print(freqHz);
  Serial.println(" Hz");
}

void holdHigh() {
  silence();
  digitalWrite(BUZZER_PIN, HIGH);
  delay(HOLD_MS);
  digitalWrite(BUZZER_PIN, LOW);
  Serial.println("held pin high");
}

void printPasteLine() {
  if (markedHz < FREQ_MIN_HZ || markedHz > FREQ_MAX_HZ) {
    Serial.println("mark a pitch with z before copying");
    return;
  }
  Serial.println("paste into runBuzzer.ino:");
  Serial.print("constexpr int BUZZER_HZ = ");
  Serial.print(markedHz);
  Serial.println(";");
}

void handleKey(char key) {
  if (key == 'a') {
    clampFreq(freqHz - NUDGE_FINE_HZ);
    beep();
  } else if (key == 'd') {
    clampFreq(freqHz + NUDGE_FINE_HZ);
    beep();
  } else if (key == 'A') {
    clampFreq(freqHz - NUDGE_COARSE_HZ);
    beep();
  } else if (key == 'D') {
    clampFreq(freqHz + NUDGE_COARSE_HZ);
    beep();
  } else if (key == 'b') {
    beep();
  } else if (key == 'h') {
    holdHigh();
  } else if (key == 's') {
    silence();
    Serial.println("silent");
  } else if (key == 'z') {
    markedHz = freqHz;
    Serial.print("marked ");
    Serial.print(markedHz);
    Serial.println(" Hz");
  } else if (key == 'p') {
    printPasteLine();
  }
}

void setup() {
  pinMode(BUZZER_PIN, OUTPUT);
  silence();

  Serial.begin(115200);
  uint32_t startMs = millis();
  while (!Serial && static_cast<uint32_t>(millis() - startMs) < 3000) {
  }

  Serial.println("buzzer calibration");
  Serial.print("start ");
  Serial.print(freqHz);
  Serial.println(" Hz");
  Serial.println("a/d 50 Hz, A/D 400 Hz, b beep, h hold high, s silence, z mark, p print");
  beep();
}

void loop() {
  while (Serial.available() > 0) {
    char key = Serial.read();
    if (key != '\r' && key != '\n') {
      handleKey(key);
    }
  }
}
