/*
 * Reads the TF-Luna and adds the offset from calibrateTfLuna.
 *
 * Run calibrateTfLuna first. Paste the two lines it prints over the
 * constants below. Until TFLUNA_CALIBRATED is true, this sketch does
 * not range.
 *
 * SDA is D20. SCL is D21. 5 V is SPI2 pin 2. GND is SPI2 pin 6.
 * Sensor pin 5 stays tied to GND. Reported distance is centimeters.
 */

#include <Wire.h>

constexpr uint8_t TFLUNA_ADDR = 0x10;
constexpr uint8_t REG_DIST_LOW = 0x00;
constexpr uint8_t REG_MODE = 0x23;
constexpr uint8_t REG_TRIGGER = 0x24;

constexpr int DIST_MIN_CM = 20;
constexpr int DIST_MAX_CM = 800;
constexpr int AMP_MIN = 100;
constexpr int AMP_OVEREXPOSED = 65535;
constexpr uint32_t TRIGGER_WAIT_MS = 20;
constexpr uint32_t SAMPLE_GAP_MS = 50;

// Paste from calibrateTfLuna.
constexpr bool TFLUNA_CALIBRATED = false;
constexpr int TFLUNA_OFFSET_CM = 0;

struct LunaSample {
  int distCm;
  int amp;
  int tempC;
};

bool useTrigger = false;
bool running = false;

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

  if (writeReg(REG_MODE, 0x01)) {
    delay(100);
    uint8_t mode = 0;
    if (readRegs(REG_MODE, &mode, 1) && mode == 0x01) {
      useTrigger = true;
    }
  }
  return true;
}

void setup() {
  Serial.begin(115200);
  uint32_t startMs = millis();
  while (!Serial && static_cast<uint32_t>(millis() - startMs) < 2000) {
  }

  if (!TFLUNA_CALIBRATED || TFLUNA_OFFSET_CM > 100 || TFLUNA_OFFSET_CM < -100) {
    Serial.println("paste TFLUNA_CALIBRATED and TFLUNA_OFFSET_CM from calibrateTfLuna");
    return;
  }

  if (!beginLidar()) {
    return;
  }

  running = true;
  phaseMs = millis();
  Serial.print("ranging, offset ");
  Serial.print(TFLUNA_OFFSET_CM);
  Serial.println(" cm");
}

void loop() {
  if (!running) {
    return;
  }

  uint32_t now = millis();
  if (phase == PHASE_TRIGGER) {
    if (static_cast<uint32_t>(now - phaseMs) < SAMPLE_GAP_MS) {
      return;
    }
    if (useTrigger && !writeReg(REG_TRIGGER, 0x01)) {
      Serial.println("i2c fail");
      phaseMs = now;
      return;
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
    Serial.println("i2c fail");
  } else if (!reliable(&sample)) {
    if (sample.amp == AMP_OVEREXPOSED) {
      Serial.println("dist overexposed");
    } else {
      Serial.print("dist weak amp ");
      Serial.println(sample.amp);
    }
  } else {
    int corrected = sample.distCm + TFLUNA_OFFSET_CM;
    if (corrected < 0) {
      corrected = 0;
    }
    Serial.print("dist ");
    Serial.print(corrected);
    Serial.print(" cm raw ");
    Serial.print(sample.distCm);
    Serial.print(" amp ");
    Serial.println(sample.amp);
  }

  phase = PHASE_TRIGGER;
  phaseMs = millis();
}
