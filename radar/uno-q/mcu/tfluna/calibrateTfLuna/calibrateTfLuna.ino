/*
 * Measures the TF-Luna against a tape and prints the offset to paste
 * into runTfLuna.ino.
 *
 * Put a flat target in front of the lens, fully covering the spot.
 * Press c to average the reading, then type the tape distance in
 * centimeters and press enter. Press p to print the paste lines.
 *
 * Serial monitor at 115200.
 *   c   average 16 readings
 *   p   print the lines for runTfLuna.ino
 *
 * SDA is D20. SCL is D21. 5 V is SPI2 pin 2. GND is SPI2 pin 6.
 * Sensor pin 5 stays tied to GND so the lidar stays in I2C mode.
 * Distance registers are centimeters. A return with strength below 100,
 * or 65535, is rejected.
 */

#include <Wire.h>

constexpr uint8_t TFLUNA_ADDR = 0x10;
constexpr uint8_t REG_DIST_LOW = 0x00;
constexpr uint8_t REG_MODE = 0x23;
constexpr uint8_t REG_TRIGGER = 0x24;
constexpr uint8_t REG_VERSION = 0x0A;
constexpr uint8_t REG_SIGNATURE = 0x3C;

constexpr int DIST_MIN_CM = 20;
constexpr int DIST_MAX_CM = 800;
constexpr int AMP_MIN = 100;
constexpr int AMP_OVEREXPOSED = 65535;
constexpr int OFFSET_LIMIT_CM = 100;
constexpr int CAPTURE_COUNT = 16;
constexpr int CAPTURE_MIN_GOOD = 8;
constexpr uint32_t TRIGGER_WAIT_MS = 20;
constexpr uint32_t SAMPLE_GAP_MS = 200;

struct LunaSample {
  int distCm;
  int amp;
  int tempC;
};

bool useTrigger = false;
bool lidarReady = false;
bool waitingForTape = false;
bool haveOffset = false;
int measuredCm = 0;
int offsetCm = 0;

char tapeDigits[5];
uint8_t tapeLen = 0;

enum Phase { PHASE_TRIGGER, PHASE_READ };
Phase phase = PHASE_TRIGGER;
uint32_t phaseMs = 0;

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

bool triggerAndRead(LunaSample *sample) {
  if (useTrigger && !writeReg(REG_TRIGGER, 0x01)) {
    return false;
  }
  if (useTrigger) {
    delay(TRIGGER_WAIT_MS);
  }
  return readSample(sample);
}

void printSample(const LunaSample *sample) {
  if (!reliable(sample)) {
    if (sample->amp == AMP_OVEREXPOSED) {
      Serial.println("dist overexposed");
    } else {
      Serial.print("dist weak amp ");
      Serial.println(sample->amp);
    }
    return;
  }
  Serial.print("dist ");
  Serial.print(sample->distCm);
  Serial.print(" cm amp ");
  Serial.print(sample->amp);
  Serial.print(" temp ");
  Serial.println(sample->tempC);
}

void printPasteLines() {
  if (!haveOffset) {
    Serial.println("capture a target and enter the tape distance first");
    return;
  }
  Serial.println("paste into runTfLuna.ino:");
  Serial.println("constexpr bool TFLUNA_CALIBRATED = true;");
  Serial.print("constexpr int TFLUNA_OFFSET_CM = ");
  Serial.print(offsetCm);
  Serial.println(";");
}

bool captureAverage() {
  long sum = 0;
  int good = 0;
  LunaSample sample;

  for (int i = 0; i < CAPTURE_COUNT; i++) {
    if (!triggerAndRead(&sample)) {
      continue;
    }
    if (!reliable(&sample)) {
      continue;
    }
    sum += sample.distCm;
    good++;
  }

  if (good < CAPTURE_MIN_GOOD) {
    Serial.print("only ");
    Serial.print(good);
    Serial.println(" good readings, aim a flat target and try c again");
    return false;
  }

  measuredCm = static_cast<int>((sum + good / 2) / good);
  Serial.print("measured ");
  Serial.print(measuredCm);
  Serial.println(" cm");
  Serial.println("type the tape distance in cm, then enter");
  waitingForTape = true;
  tapeLen = 0;
  return true;
}

void acceptTape(int tapeCm) {
  int nextOffset = tapeCm - measuredCm;
  if (nextOffset > OFFSET_LIMIT_CM || nextOffset < -OFFSET_LIMIT_CM) {
    Serial.println("offset larger than 100 cm, measure again");
    waitingForTape = false;
    return;
  }
  offsetCm = nextOffset;
  haveOffset = true;
  waitingForTape = false;
  Serial.print("tape ");
  Serial.print(tapeCm);
  Serial.print(" cm, offset ");
  Serial.print(offsetCm);
  Serial.println(" cm");
  printPasteLines();
}

void takeTapeDigit(char key) {
  if (key >= '0' && key <= '9') {
    if (tapeLen < 4) {
      tapeDigits[tapeLen] = key;
      tapeLen++;
    }
    return;
  }
  if (key != '\r' && key != '\n') {
    return;
  }
  if (tapeLen == 0) {
    return;
  }
  tapeDigits[tapeLen] = '\0';
  int tapeCm = 0;
  for (uint8_t i = 0; i < tapeLen; i++) {
    tapeCm = tapeCm * 10 + (tapeDigits[i] - '0');
  }
  tapeLen = 0;
  if (tapeCm < DIST_MIN_CM || tapeCm > DIST_MAX_CM) {
    Serial.println("tape distance must be 20 to 800 cm");
    return;
  }
  acceptTape(tapeCm);
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
    Serial.println("no answer at 0x10, tie sensor pin 5 to GND");
    return false;
  }

  uint8_t signature[4];
  if (readRegs(REG_SIGNATURE, signature, 4) &&
      signature[0] == 'L' && signature[1] == 'U' &&
      signature[2] == 'N' && signature[3] == 'A') {
    Serial.println("id LUNA");
  }

  uint8_t version[3];
  if (readRegs(REG_VERSION, version, 3)) {
    Serial.print("firmware ");
    Serial.print(version[2]);
    Serial.print(".");
    Serial.print(version[1]);
    Serial.print(".");
    Serial.println(version[0]);
  }

  if (writeReg(REG_MODE, 0x01)) {
    delay(100);
    uint8_t mode = 0;
    if (readRegs(REG_MODE, &mode, 1) && mode == 0x01) {
      useTrigger = true;
    }
  }
  if (!useTrigger) {
    Serial.println("reading continuous");
  }
  lidarReady = true;
  return true;
}

void setup() {
  Serial.begin(115200);
  uint32_t startMs = millis();
  while (!Serial && static_cast<uint32_t>(millis() - startMs) < 3000) {
  }

  Serial.println("tf-luna calibration");
  if (!beginLidar()) {
    return;
  }
  Serial.println("c capture, then type the tape cm, p print");
  phaseMs = millis();
}

void loop() {
  if (!lidarReady) {
    return;
  }

  while (Serial.available() > 0) {
    char key = static_cast<char>(Serial.read());
    if (waitingForTape) {
      takeTapeDigit(key);
      continue;
    }
    if (key == 'c') {
      captureAverage();
    } else if (key == 'p') {
      printPasteLines();
    }
  }

  if (waitingForTape) {
    return;
  }

  uint32_t now = millis();
  if (phase == PHASE_TRIGGER) {
    if (static_cast<uint32_t>(now - phaseMs) < SAMPLE_GAP_MS) {
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
  if (readSample(&sample)) {
    printSample(&sample);
  } else {
    Serial.println("i2c fail");
  }
  phase = PHASE_TRIGGER;
  phaseMs = millis();
}
