/*
 * Beeps the buzzer at the pitch chosen in calibrateBuzzer.
 *
 * Run calibrateBuzzer first. Paste the line it prints over BUZZER_HZ.
 * Until that pitch is filled in, this sketch keeps the pin low.
 *
 * Power is the 3.3 V pin. Ground is GND. Signal is D6.
 * Serial monitor at 115200: b one beep, r repeat, s silence.
 */

constexpr int BUZZER_PIN = D6;
constexpr int FREQ_MIN_HZ = 100;
constexpr int FREQ_MAX_HZ = 5000;

// Paste from calibrateBuzzer. 0 means the pitch is not chosen yet.
constexpr int BUZZER_HZ = 0;

constexpr uint32_t BEEP_MS = 80;
constexpr uint32_t GAP_MS = 120;
constexpr uint32_t REST_MS = 700;
constexpr int BEEPS_IN_GROUP = 3;

enum Phase { PHASE_BEEP, PHASE_GAP, PHASE_REST };

Phase phase = PHASE_REST;
int beepsLeft = 0;
uint32_t phaseMs = 0;
bool repeating = false;

bool pitchReady() {
  return BUZZER_HZ >= FREQ_MIN_HZ && BUZZER_HZ <= FREQ_MAX_HZ;
}

void silence() {
  noTone(BUZZER_PIN);
  digitalWrite(BUZZER_PIN, LOW);
}

void startGroup() {
  beepsLeft = BEEPS_IN_GROUP;
  phase = PHASE_BEEP;
  phaseMs = millis();
  tone(BUZZER_PIN, BUZZER_HZ, BEEP_MS);
}

void servicePattern() {
  if (!repeating) {
    return;
  }
  uint32_t now = millis();
  if (phase == PHASE_BEEP) {
    if (static_cast<uint32_t>(now - phaseMs) < BEEP_MS) {
      return;
    }
    silence();
    beepsLeft--;
    phaseMs = now;
    phase = (beepsLeft > 0) ? PHASE_GAP : PHASE_REST;
  } else if (phase == PHASE_GAP) {
    if (static_cast<uint32_t>(now - phaseMs) < GAP_MS) {
      return;
    }
    phase = PHASE_BEEP;
    phaseMs = now;
    tone(BUZZER_PIN, BUZZER_HZ, BEEP_MS);
  } else if (static_cast<uint32_t>(now - phaseMs) >= REST_MS) {
    startGroup();
  }
}

void setup() {
  pinMode(BUZZER_PIN, OUTPUT);
  silence();

  Serial.begin(115200);
  uint32_t startMs = millis();
  while (!Serial && static_cast<uint32_t>(millis() - startMs) < 2000) {
  }

  if (!pitchReady()) {
    Serial.println("paste BUZZER_HZ from calibrateBuzzer before running");
    return;
  }

  Serial.print("buzzer ");
  Serial.print(BUZZER_HZ);
  Serial.println(" Hz");
  Serial.println("b beep, r repeat, s silence");
  repeating = true;
  startGroup();
}

void loop() {
  if (!pitchReady()) {
    return;
  }

  while (Serial.available() > 0) {
    char key = Serial.read();
    if (key == 's') {
      repeating = false;
      silence();
      Serial.println("silent");
    } else if (key == 'b') {
      repeating = false;
      silence();
      tone(BUZZER_PIN, BUZZER_HZ, BEEP_MS);
      Serial.println("beep");
    } else if (key == 'r') {
      repeating = true;
      startGroup();
      Serial.println("repeat");
    }
  }
  servicePattern();
}
