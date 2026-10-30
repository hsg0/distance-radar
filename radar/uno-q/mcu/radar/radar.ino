/*
 * One sketch for the radar page. The Linux UI sends a line at a time:
 *
 *   mode idle
 *   mode servo-cal
 *   mode servo-run
 *   mode luna-cal
 *   mode luna-run
 *   mode radar
 *   mode buzzer-cal
 *   mode buzzer-run
 *   zero <us>          this pulse is 0 degrees, the center
 *   cal luna <offset cm>
 *   buzz <hz>          pitch for the buzzer, 100 to 5000
 *   buzz hold          pin high for a moment
 *   key a|d|A|D|c|z|t
 *   tape <cm>
 *
 * Servo signal is D10. TF-Luna is I2C on D20/D21, address 0x10.
 * Sensor pin 5 stays tied to GND.
 * Buzzer signal is D6. Power is 3.3 V. Ground is GND.
 */

#include <Servo.h>
#include <Wire.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

constexpr int SERVO_PIN = D10;
constexpr int PULSE_ZERO_US = 1300;
// Full 180 degree throw of the A0090. 0 degrees stays at PULSE_ZERO_US.
constexpr int PULSE_LEFT_US = 500;
constexpr int PULSE_RIGHT_US = 2400;
constexpr int ANGLE_MIN = -90;
constexpr int ANGLE_MAX = 90;
constexpr int PULSE_MIN_US = 500;
constexpr int PULSE_MAX_US = 2500;
constexpr int NUDGE_FINE_US = 20;
constexpr int NUDGE_COARSE_US = 100;
constexpr uint32_t SERVO_STEP_MS = 15;
constexpr uint32_t SWEEP_STEP_MS = 20;

constexpr uint8_t TFLUNA_ADDR = 0x10;
constexpr uint8_t REG_DIST_LOW = 0x00;
constexpr uint8_t REG_MODE = 0x23;
constexpr uint8_t REG_TRIGGER = 0x24;
constexpr int DIST_MIN_CM = 20;
constexpr int DIST_MAX_CM = 800;
constexpr int AMP_MIN = 100;
constexpr int AMP_OVEREXPOSED = 65535;
constexpr int OFFSET_LIMIT_CM = 100;
constexpr int CAPTURE_COUNT = 16;
constexpr int CAPTURE_MIN_GOOD = 8;
constexpr uint32_t TRIGGER_WAIT_MS = 20;
constexpr uint32_t SAMPLE_GAP_MS = 200;

constexpr int BUZZER_PIN = D6;
constexpr int FREQ_MIN_HZ = 100;
constexpr int FREQ_MAX_HZ = 5000;
constexpr int FREQ_START_HZ = 2000;
constexpr uint32_t BUZZ_BEEP_MS = 80;
constexpr uint32_t BUZZ_GAP_MS = 120;
constexpr uint32_t BUZZ_REST_MS = 700;
constexpr uint32_t BUZZ_HOLD_MS = 250;
constexpr uint32_t SOUND_INTERVAL_MS = 200;
constexpr int BUZZ_GROUP = 3;

enum Mode {
  MODE_IDLE,
  MODE_SERVO_CAL,
  MODE_SERVO_RUN,
  MODE_LUNA_CAL,
  MODE_LUNA_RUN,
  MODE_RADAR,
  MODE_BUZZER_CAL,
  MODE_BUZZER_RUN
};

enum BuzzPhase { BUZZ_BEEP, BUZZ_GAP, BUZZ_REST };

enum Phase { PHASE_MOVE, PHASE_WAIT, PHASE_READ };

struct LunaSample {
  int distCm;
  int amp;
  int tempC;
};

Servo radarServo;

Mode mode = MODE_IDLE;
Phase phase = PHASE_MOVE;
uint32_t phaseMs = 0;

int pulseUs = PULSE_ZERO_US;
int zeroUs = PULSE_ZERO_US;
bool servoReady = true;

int offsetCm = 0;
bool lunaReady = false;
int measuredCm = 0;
bool haveMeasured = false;
int lastRawCm = 0;
bool haveRaw = false;

bool useTrigger = false;
bool lidarReady = false;

bool sweeping = false;
int sweepUs = 0;
int sweepStep = NUDGE_FINE_US;
uint32_t lastSweepMs = 0;

int angleDeg = 0;
int angleStep = 1;

int freqHz = FREQ_START_HZ;
bool buzzerReady = false;
BuzzPhase buzzPhase = BUZZ_REST;
int beepsLeft = 0;
uint32_t buzzMs = 0;
int soundingHz = 0;
uint32_t lastSoundMs = 0;

char lineBuf[72];
uint8_t lineLen = 0;

bool writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(TFLUNA_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool readRegs(uint8_t reg, uint8_t *buf, uint8_t len) {
  Wire.beginTransmission(TFLUNA_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) {
    return false;
  }
  if (Wire.requestFrom(static_cast<int>(TFLUNA_ADDR), static_cast<int>(len)) != len) {
    return false;
  }
  for (uint8_t i = 0; i < len; i++) {
    buf[i] = static_cast<uint8_t>(Wire.read());
  }
  return true;
}

bool pingLidar() {
  Wire.beginTransmission(TFLUNA_ADDR);
  return Wire.endTransmission() == 0;
}

bool readSample(LunaSample *sample) {
  uint8_t raw[6];
  if (!readRegs(REG_DIST_LOW, raw, 6)) {
    return false;
  }
  sample->distCm = raw[0] | (raw[1] << 8);
  sample->amp = raw[2] | (raw[3] << 8);
  int tempRaw = raw[4] | (raw[5] << 8);
  sample->tempC = tempRaw / 8 - 256;
  return true;
}

bool reliable(const LunaSample *sample) {
  if (sample->amp < AMP_MIN || sample->amp == AMP_OVEREXPOSED) {
    return false;
  }
  return sample->distCm >= DIST_MIN_CM && sample->distCm <= DIST_MAX_CM;
}

bool triggerRead(LunaSample *sample) {
  if (useTrigger && !writeReg(REG_TRIGGER, 0x01)) {
    return false;
  }
  if (useTrigger) {
    delay(TRIGGER_WAIT_MS);
  }
  return readSample(sample);
}

void holdPulse(int us) {
  if (us < PULSE_MIN_US) {
    us = PULSE_MIN_US;
  }
  if (us > PULSE_MAX_US) {
    us = PULSE_MAX_US;
  }
  pulseUs = us;
  radarServo.attach(SERVO_PIN);
  radarServo.writeMicroseconds(pulseUs);
  Serial.print("pulse ");
  Serial.println(pulseUs);
}

int pulseForAngle(int angle) {
  if (angle > ANGLE_MAX) {
    angle = ANGLE_MAX;
  }
  if (angle < ANGLE_MIN) {
    angle = ANGLE_MIN;
  }
  if (angle >= 0) {
    return zeroUs + (angle * (PULSE_RIGHT_US - zeroUs)) / ANGLE_MAX;
  }
  return zeroUs + (angle * (zeroUs - PULSE_LEFT_US)) / ANGLE_MAX;
}

void writeAngle(int angle) {
  pulseUs = pulseForAngle(angle);
  radarServo.writeMicroseconds(pulseUs);
}

const char *modeName(Mode value) {
  switch (value) {
    case MODE_SERVO_CAL: return "servo-cal";
    case MODE_SERVO_RUN: return "servo-run";
    case MODE_LUNA_CAL: return "luna-cal";
    case MODE_LUNA_RUN: return "luna-run";
    case MODE_RADAR: return "radar";
    case MODE_BUZZER_CAL: return "buzzer-cal";
    case MODE_BUZZER_RUN: return "buzzer-run";
    default: return "idle";
  }
}

void silenceBuzzer() {
  noTone(BUZZER_PIN);
  digitalWrite(BUZZER_PIN, LOW);
  buzzPhase = BUZZ_REST;
  beepsLeft = 0;
  soundingHz = 0;
  lastSoundMs = 0;
}

int hzForCm(int cm) {
  // 1 ft -> 100 Hz, then +400 Hz for each extra half foot, capped at 5000 Hz.
  long hz = (static_cast<long>(cm) * 80000L) / 3048L - 700L;
  if (hz < FREQ_MIN_HZ) {
    return FREQ_MIN_HZ;
  }
  if (hz > FREQ_MAX_HZ) {
    return FREQ_MAX_HZ;
  }
  return static_cast<int>(hz);
}

void applySound(int cm, bool present) {
  if (!present) {
    if (soundingHz != 0) {
      soundingHz = 0;
      noTone(BUZZER_PIN);
      digitalWrite(BUZZER_PIN, LOW);
    }
    return;
  }
  int hz = hzForCm(cm);
  int stepped = hz - (hz % 50);
  if (stepped < FREQ_MIN_HZ) {
    stepped = FREQ_MIN_HZ;
  }
  if (stepped == soundingHz) {
    return;
  }
  soundingHz = stepped;
  tone(BUZZER_PIN, stepped, 0);
}

void soundRange(int cm, bool present) {
  uint32_t now = millis();
  if (lastSoundMs != 0 && static_cast<uint32_t>(now - lastSoundMs) < SOUND_INTERVAL_MS) {
    return;
  }
  lastSoundMs = now;
  applySound(cm, present);
}

void startBuzzGroup() {
  beepsLeft = BUZZ_GROUP;
  buzzPhase = BUZZ_BEEP;
  buzzMs = millis();
  tone(BUZZER_PIN, freqHz, BUZZ_BEEP_MS);
}

void setBuzz(int hz) {
  if (hz < FREQ_MIN_HZ || hz > FREQ_MAX_HZ) {
    Serial.println("err buzz");
    return;
  }
  freqHz = hz;
  buzzerReady = true;
  Serial.print("buzz ");
  Serial.println(freqHz);
  tone(BUZZER_PIN, freqHz, 180);
}

void holdBuzz() {
  noTone(BUZZER_PIN);
  digitalWrite(BUZZER_PIN, HIGH);
  delay(BUZZ_HOLD_MS);
  digitalWrite(BUZZER_PIN, LOW);
  Serial.println("buzz hold");
}

void enterMode(Mode next) {
  sweeping = false;
  if (next == MODE_BUZZER_CAL) {
    buzzPhase = BUZZ_REST;
    beepsLeft = 0;
  } else {
    silenceBuzzer();
  }
  phase = PHASE_MOVE;
  phaseMs = millis();
  angleDeg = 0;
  angleStep = 1;
  mode = next;
  Serial.print("mode ");
  Serial.println(modeName(mode));

  if (mode == MODE_SERVO_CAL) {
    holdPulse(pulseUs);
  } else if (mode == MODE_SERVO_RUN || mode == MODE_RADAR) {
    writeAngle(0);
    Serial.println("angle 0");
  } else if (mode == MODE_BUZZER_RUN) {
    startBuzzGroup();
  }
}

bool beginLidar() {
  Wire.begin();
  Wire.setClock(100000);
  bool found = pingLidar();
  if (!found) {
    delay(50);
    found = pingLidar();
  }
  if (!found) {
    Serial.println("err lidar");
    return false;
  }
  if (writeReg(REG_MODE, 0x01)) {
    delay(100);
    uint8_t modeReg = 0;
    if (readRegs(REG_MODE, &modeReg, 1) && modeReg == 0x01) {
      useTrigger = true;
    }
  }
  lidarReady = true;
  return true;
}

void markZero(int us) {
  if (us < PULSE_MIN_US || us > PULSE_MAX_US) {
    Serial.println("err servo");
    return;
  }
  zeroUs = us;
  servoReady = true;
  Serial.print("zero ");
  Serial.println(zeroUs);
}

void setLunaCal(int offset) {
  if (offset > OFFSET_LIMIT_CM || offset < -OFFSET_LIMIT_CM) {
    Serial.println("err luna");
    return;
  }
  offsetCm = offset;
  lunaReady = true;
  Serial.print("offset ");
  Serial.println(offsetCm);
}

void startSweepTest() {
  sweeping = true;
  sweepUs = PULSE_LEFT_US;
  sweepStep = NUDGE_FINE_US;
  lastSweepMs = millis();
  radarServo.writeMicroseconds(sweepUs);
  pulseUs = sweepUs;
}

void captureAverage() {
  if (!lidarReady) {
    Serial.println("err lidar");
    return;
  }
  long sum = 0;
  int good = 0;
  LunaSample sample;
  for (int i = 0; i < CAPTURE_COUNT; i++) {
    if (!triggerRead(&sample) || !reliable(&sample)) {
      continue;
    }
    sum += sample.distCm;
    good++;
  }
  if (good < CAPTURE_MIN_GOOD) {
    haveMeasured = false;
    Serial.println("err capture");
    return;
  }
  measuredCm = static_cast<int>((sum + good / 2) / good);
  haveMeasured = true;
  Serial.print("measured ");
  Serial.println(measuredCm);
}

void acceptTape(int tapeCm) {
  int basis = haveMeasured ? measuredCm : lastRawCm;
  if (!haveMeasured && !haveRaw) {
    Serial.println("err capture");
    return;
  }
  if (tapeCm < DIST_MIN_CM || tapeCm > DIST_MAX_CM) {
    Serial.println("err tape");
    return;
  }
  int next = tapeCm - basis;
  if (next > OFFSET_LIMIT_CM || next < -OFFSET_LIMIT_CM) {
    Serial.println("err tape");
    return;
  }
  setLunaCal(next);
}

void handleKey(char key) {
  if (mode == MODE_LUNA_CAL && key == 'c') {
    captureAverage();
    return;
  }
  if (mode != MODE_SERVO_CAL) {
    return;
  }
  sweeping = false;
  if (key == 'a') {
    holdPulse(pulseUs - NUDGE_FINE_US);
  } else if (key == 'd') {
    holdPulse(pulseUs + NUDGE_FINE_US);
  } else if (key == 'A') {
    holdPulse(pulseUs - NUDGE_COARSE_US);
  } else if (key == 'D') {
    holdPulse(pulseUs + NUDGE_COARSE_US);
  } else if (key == 'c') {
    holdPulse(zeroUs);
  } else if (key == 'z') {
    markZero(pulseUs);
  } else if (key == 't') {
    startSweepTest();
  }
}

void handleLine(char *line) {
  while (*line == ' ') {
    line++;
  }
  if (strncmp(line, "mode ", 5) == 0) {
    const char *name = line + 5;
    if (strcmp(name, "idle") == 0) {
      enterMode(MODE_IDLE);
    } else if (strcmp(name, "servo-cal") == 0) {
      enterMode(MODE_SERVO_CAL);
    } else if (strcmp(name, "servo-run") == 0) {
      if (!servoReady) {
        Serial.println("err servo");
        return;
      }
      enterMode(MODE_SERVO_RUN);
    } else if (strcmp(name, "luna-cal") == 0) {
      enterMode(MODE_LUNA_CAL);
    } else if (strcmp(name, "luna-run") == 0) {
      if (!lunaReady) {
        Serial.println("err luna");
        return;
      }
      enterMode(MODE_LUNA_RUN);
    } else if (strcmp(name, "radar") == 0) {
      if (!servoReady) {
        Serial.println("err servo");
        return;
      }
      if (!lunaReady) {
        Serial.println("err luna");
        return;
      }
      enterMode(MODE_RADAR);
    } else if (strcmp(name, "buzzer-cal") == 0) {
      enterMode(MODE_BUZZER_CAL);
    } else if (strcmp(name, "buzzer-run") == 0) {
      if (!buzzerReady) {
        Serial.println("err buzz");
        return;
      }
      enterMode(MODE_BUZZER_RUN);
    }
    return;
  }
  if (strncmp(line, "buzz ", 5) == 0) {
    if (strcmp(line + 5, "hold") == 0) {
      holdBuzz();
    } else {
      setBuzz(atoi(line + 5));
    }
    return;
  }
  if (strncmp(line, "zero ", 5) == 0) {
    markZero(atoi(line + 5));
    return;
  }
  if (strncmp(line, "cal luna ", 9) == 0) {
    int offset = 0;
    if (sscanf(line + 9, "%d", &offset) == 1) {
      setLunaCal(offset);
    } else {
      Serial.println("err luna");
    }
    return;
  }
  if (strncmp(line, "key ", 4) == 0 && line[4] != '\0') {
    handleKey(line[4]);
    return;
  }
  if (strncmp(line, "tape ", 5) == 0) {
    acceptTape(atoi(line + 5));
  }
}

void pollSerial() {
  while (Serial.available() > 0) {
    char c = static_cast<char>(Serial.read());
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      lineBuf[lineLen] = '\0';
      if (lineLen > 0) {
        handleLine(lineBuf);
      }
      lineLen = 0;
      continue;
    }
    if (lineLen < sizeof(lineBuf) - 1) {
      lineBuf[lineLen++] = c;
    }
  }
}

void serviceSweepTest() {
  if (!sweeping) {
    return;
  }
  uint32_t now = millis();
  if (static_cast<uint32_t>(now - lastSweepMs) < SWEEP_STEP_MS) {
    return;
  }
  lastSweepMs = now;
  sweepUs += sweepStep;
  bool reached = (sweepStep > 0 && sweepUs >= PULSE_RIGHT_US) || (sweepStep < 0 && sweepUs <= PULSE_LEFT_US);
  if (reached) {
    sweepUs = zeroUs;
    sweeping = false;
  }
  radarServo.writeMicroseconds(sweepUs);
  pulseUs = sweepUs;
  Serial.print("pulse ");
  Serial.println(pulseUs);
  if (!sweeping) {
    Serial.println("sweep done");
  }
}

void advanceAngle() {
  angleDeg += angleStep;
  if (angleDeg >= ANGLE_MAX) {
    angleDeg = ANGLE_MAX;
    angleStep = -1;
  } else if (angleDeg <= ANGLE_MIN) {
    angleDeg = ANGLE_MIN;
    angleStep = 1;
  }
}

void serviceServoRun() {
  uint32_t now = millis();
  if (static_cast<uint32_t>(now - phaseMs) < SERVO_STEP_MS) {
    return;
  }
  phaseMs = now;
  advanceAngle();
  writeAngle(angleDeg);
  Serial.print("angle ");
  Serial.println(angleDeg);
}

void printDistance(const LunaSample *sample, bool corrected) {
  if (!reliable(sample)) {
    if (sample->amp == AMP_OVEREXPOSED) {
      Serial.println("overexposed");
    } else {
      Serial.print("weak ");
      Serial.println(sample->amp);
    }
    return;
  }
  int shown = sample->distCm;
  lastRawCm = sample->distCm;
  haveRaw = true;
  if (corrected) {
    shown += offsetCm;
    if (shown < 0) {
      shown = 0;
    }
  }
  Serial.print("dist ");
  Serial.print(shown);
  Serial.print(" ");
  Serial.println(sample->amp);
}

void serviceLuna() {
  uint32_t now = millis();
  if (phase == PHASE_MOVE) {
    uint32_t gap = (mode == MODE_LUNA_CAL) ? SAMPLE_GAP_MS : 50;
    if (static_cast<uint32_t>(now - phaseMs) < gap) {
      return;
    }
    if (useTrigger) {
      writeReg(REG_TRIGGER, 0x01);
    }
    phase = PHASE_READ;
    phaseMs = now;
    return;
  }
  if (static_cast<uint32_t>(now - phaseMs) < TRIGGER_WAIT_MS) {
    return;
  }
  LunaSample sample;
  if (!readSample(&sample)) {
    Serial.println("err lidar");
    if (mode == MODE_LUNA_RUN) {
      soundRange(0, false);
    }
  } else {
    printDistance(&sample, mode == MODE_LUNA_RUN);
    if (mode == MODE_LUNA_RUN) {
      if (reliable(&sample)) {
        int shown = sample.distCm + offsetCm;
        if (shown < 0) {
          shown = 0;
        }
        soundRange(shown, true);
      } else {
        soundRange(0, false);
      }
    }
  }
  phase = PHASE_MOVE;
  phaseMs = millis();
}

void serviceRadar() {
  uint32_t now = millis();
  if (phase == PHASE_MOVE) {
    writeAngle(angleDeg);
    if (useTrigger) {
      writeReg(REG_TRIGGER, 0x01);
    }
    phase = PHASE_READ;
    phaseMs = now;
    return;
  }
  if (static_cast<uint32_t>(now - phaseMs) < TRIGGER_WAIT_MS) {
    return;
  }
  LunaSample sample;
  Serial.print("radar ");
  Serial.print(angleDeg);
  Serial.print(" ");
  if (!readSample(&sample) || !reliable(&sample)) {
    Serial.println("weak");
    soundRange(0, false);
  } else {
    int shown = sample.distCm + offsetCm;
    if (shown < 0) {
      shown = 0;
    }
    Serial.print(shown);
    Serial.print(" ");
    Serial.println(sample.amp);
    soundRange(shown, true);
  }
  advanceAngle();
  phase = PHASE_MOVE;
  phaseMs = millis();
}

void serviceBuzzer() {
  uint32_t now = millis();
  if (buzzPhase == BUZZ_BEEP) {
    if (static_cast<uint32_t>(now - buzzMs) < BUZZ_BEEP_MS) {
      return;
    }
    noTone(BUZZER_PIN);
    digitalWrite(BUZZER_PIN, LOW);
    beepsLeft--;
    buzzMs = now;
    buzzPhase = (beepsLeft > 0) ? BUZZ_GAP : BUZZ_REST;
  } else if (buzzPhase == BUZZ_GAP) {
    if (static_cast<uint32_t>(now - buzzMs) < BUZZ_GAP_MS) {
      return;
    }
    buzzPhase = BUZZ_BEEP;
    buzzMs = now;
    tone(BUZZER_PIN, freqHz, BUZZ_BEEP_MS);
  } else if (static_cast<uint32_t>(now - buzzMs) >= BUZZ_REST_MS) {
    startBuzzGroup();
  }
}

void setup() {
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  Serial.begin(115200);
  uint32_t startMs = millis();
  while (!Serial && static_cast<uint32_t>(millis() - startMs) < 2000) {
  }
  radarServo.attach(SERVO_PIN);
  radarServo.writeMicroseconds(zeroUs);
  beginLidar();
  Serial.print("zero ");
  Serial.println(zeroUs);
  Serial.println("ready");
}

void loop() {
  pollSerial();
  switch (mode) {
    case MODE_SERVO_CAL:
      serviceSweepTest();
      break;
    case MODE_SERVO_RUN:
      serviceServoRun();
      break;
    case MODE_LUNA_CAL:
    case MODE_LUNA_RUN:
      serviceLuna();
      break;
    case MODE_RADAR:
      serviceRadar();
      break;
    case MODE_BUZZER_RUN:
      serviceBuzzer();
      break;
    default:
      break;
  }
}
