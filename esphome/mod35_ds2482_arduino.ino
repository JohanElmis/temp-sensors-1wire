/*
 * Modułowo MOD-35 (DS2482-100 I2C → 1-Wire, 8x RJ45) on an ESP32 — Arduino sketch.
 *
 * Self-contained: no DS2482 library needed, only Wire.h.
 *  - finds the DS2482 at 0x18..0x1B
 *  - enumerates every 1-Wire device (ROM search)
 *  - reads all DS18B20/DS18S20/DS1822 and prints °C to Serial every 10 s
 *
 * Wiring (JP3 header on the MOD-35):
 *   GND → GND, VCC → 3V3, SDA → SDA_PIN, SCL → SCL_PIN
 * Pull-ups are already on the module (10k on SDA/SCL, 4k7 on 1-Wire).
 *
 * Tested logic against the DS2482-100 datasheet; adjust SDA_PIN/SCL_PIN for your board.
 */

#include <Wire.h>

// ---- pins: ESP32-C3 SuperMini defaults; classic ESP32 DevKit → 21 / 22
#ifndef SDA_PIN
#define SDA_PIN 6
#endif
#ifndef SCL_PIN
#define SCL_PIN 7
#endif

// ---- DS2482 commands / registers
#define DS2482_CMD_RESET        0xF0
#define DS2482_CMD_SET_READ_PTR 0xE1
#define DS2482_CMD_WRITE_CONFIG 0xD2
#define DS2482_CMD_1W_RESET     0xB4
#define DS2482_CMD_1W_SINGLE_BIT 0x87
#define DS2482_CMD_1W_WRITE_BYTE 0xA5
#define DS2482_CMD_1W_READ_BYTE  0x96
#define DS2482_CMD_1W_TRIPLET    0x78

#define DS2482_PTR_STATUS 0xF0
#define DS2482_PTR_DATA   0xE1
#define DS2482_PTR_CONFIG 0xC3

#define DS2482_STATUS_1WB 0x01  // 1-Wire busy
#define DS2482_STATUS_PPD 0x02  // presence pulse detected
#define DS2482_STATUS_SD  0x04  // short detected
#define DS2482_STATUS_SBR 0x20  // single bit result
#define DS2482_STATUS_TSB 0x40  // triplet second bit
#define DS2482_STATUS_DIR 0x80  // branch direction taken

#define DS2482_CFG_APU 0x01     // active pull-up
#define DS2482_CFG_SPU 0x04     // strong pull-up (one-shot)

// ---- 1-Wire ROM commands
#define OW_SEARCH_ROM   0xF0
#define OW_MATCH_ROM    0x55
#define OW_SKIP_ROM     0xCC
#define DS18B20_CONVERT_T       0x44
#define DS18B20_READ_SCRATCHPAD 0xBE

static uint8_t ds2482Addr = 0;

// ------------------------------------------------------------------ I2C helpers
static bool i2cWrite(uint8_t cmd) {
  Wire.beginTransmission(ds2482Addr);
  Wire.write(cmd);
  return Wire.endTransmission() == 0;
}
static bool i2cWrite(uint8_t cmd, uint8_t arg) {
  Wire.beginTransmission(ds2482Addr);
  Wire.write(cmd);
  Wire.write(arg);
  return Wire.endTransmission() == 0;
}
static uint8_t i2cRead() {
  Wire.requestFrom(ds2482Addr, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0xFF;
}

// ------------------------------------------------------------------ DS2482 core
static bool ds2482Reset() {
  if (!i2cWrite(DS2482_CMD_RESET)) return false;
  uint8_t st = i2cRead();
  return (st & 0xF7) == 0x10;  // RST bit set, everything else cleared
}

static bool ds2482SetReadPointer(uint8_t ptr) {
  return i2cWrite(DS2482_CMD_SET_READ_PTR, ptr);
}

// Point at the status register and poll until the 1-Wire busy bit clears.
static uint8_t ds2482WaitIdle() {
  uint8_t st;
  uint32_t t0 = millis();
  ds2482SetReadPointer(DS2482_PTR_STATUS);
  do {
    st = i2cRead();
  } while ((st & DS2482_STATUS_1WB) && millis() - t0 < 50);
  return st;
}

// Config byte: low nibble = config, high nibble = inverted config.
static bool ds2482WriteConfig(uint8_t cfg) {
  cfg &= 0x0F;
  if (!i2cWrite(DS2482_CMD_WRITE_CONFIG, cfg | ((~cfg & 0x0F) << 4))) return false;
  return i2cRead() == cfg;
}

static bool owReset() {           // returns true if a presence pulse was seen
  ds2482WaitIdle();
  if (!i2cWrite(DS2482_CMD_1W_RESET)) return false;
  uint8_t st = ds2482WaitIdle();
  if (st & DS2482_STATUS_SD) {
    Serial.println(F("!! 1-Wire short detected"));
    return false;
  }
  return st & DS2482_STATUS_PPD;
}

static void owWriteByte(uint8_t b) {
  ds2482WaitIdle();
  i2cWrite(DS2482_CMD_1W_WRITE_BYTE, b);
  ds2482WaitIdle();
}

static uint8_t owReadByte() {
  ds2482WaitIdle();
  i2cWrite(DS2482_CMD_1W_READ_BYTE);
  ds2482WaitIdle();
  ds2482SetReadPointer(DS2482_PTR_DATA);
  return i2cRead();
}

// Enable strong pull-up for the next byte (used while DS18B20 converts, parasite-safe).
static void owStrongPullupNext() {
  ds2482WriteConfig(DS2482_CFG_APU | DS2482_CFG_SPU);
}

// ------------------------------------------------------------------ ROM search
static uint8_t searchRom[8];
static int     searchLastDiscrepancy = 0;
static bool    searchLastDevice = false;

static void owSearchReset() {
  searchLastDiscrepancy = 0;
  searchLastDevice = false;
  memset(searchRom, 0, 8);
}

static uint8_t crc8(const uint8_t *d, uint8_t len) {
  uint8_t crc = 0;
  while (len--) {
    uint8_t in = *d++;
    for (uint8_t i = 0; i < 8; i++) {
      uint8_t mix = (crc ^ in) & 0x01;
      crc >>= 1;
      if (mix) crc ^= 0x8C;
      in >>= 1;
    }
  }
  return crc;
}

// Standard Maxim search algorithm, using the DS2482 triplet command.
static bool owSearchNext(uint8_t *rom) {
  if (searchLastDevice) return false;
  if (!owReset()) { owSearchReset(); return false; }
  owWriteByte(OW_SEARCH_ROM);

  int idBit = 1, lastZero = 0;
  for (int bitNum = 1; bitNum <= 64; bitNum++) {
    int byteIdx = (bitNum - 1) / 8;
    uint8_t mask = 1 << ((bitNum - 1) % 8);

    uint8_t dir;
    if (bitNum < searchLastDiscrepancy)      dir = (searchRom[byteIdx] & mask) ? 1 : 0;
    else if (bitNum == searchLastDiscrepancy) dir = 1;
    else                                      dir = 0;

    ds2482WaitIdle();
    i2cWrite(DS2482_CMD_1W_TRIPLET, dir ? 0x80 : 0x00);
    uint8_t st = ds2482WaitIdle();

    uint8_t idBitVal  = (st & DS2482_STATUS_SBR) ? 1 : 0;
    uint8_t cmpBitVal = (st & DS2482_STATUS_TSB) ? 1 : 0;
    uint8_t taken     = (st & DS2482_STATUS_DIR) ? 1 : 0;

    if (idBitVal && cmpBitVal) { owSearchReset(); return false; }  // no devices
    if (!idBitVal && !cmpBitVal && taken == 0) lastZero = bitNum;  // discrepancy, took 0

    if (taken) searchRom[byteIdx] |= mask; else searchRom[byteIdx] &= ~mask;
    (void)idBit;
  }

  searchLastDiscrepancy = lastZero;
  if (lastZero == 0) searchLastDevice = true;

  if (crc8(searchRom, 7) != searchRom[7]) { owSearchReset(); return false; }
  memcpy(rom, searchRom, 8);
  return true;
}

// ------------------------------------------------------------------ DS18B20
static void owMatchRom(const uint8_t *rom) {
  owWriteByte(OW_MATCH_ROM);
  for (int i = 0; i < 8; i++) owWriteByte(rom[i]);
}

static bool isTempSensor(uint8_t family) {
  return family == 0x28 || family == 0x10 || family == 0x22;
}

// Returns NAN on CRC error / missing device.
static float ds18ReadTemp(const uint8_t *rom) {
  if (!owReset()) return NAN;
  owMatchRom(rom);
  owWriteByte(DS18B20_READ_SCRATCHPAD);
  uint8_t sp[9];
  for (int i = 0; i < 9; i++) sp[i] = owReadByte();
  if (crc8(sp, 8) != sp[8]) return NAN;

  int16_t raw = (int16_t)((sp[1] << 8) | sp[0]);
  if (rom[0] == 0x10) {                       // DS18S20: 9-bit, 0.5 °C steps
    raw = raw << 3;
    if (sp[7] == 0x10) raw = (raw & 0xFFF0) + 12 - sp[6];
  }
  return raw / 16.0f;
}

// ------------------------------------------------------------------ device table
#define MAX_DEVICES 32
static uint8_t devices[MAX_DEVICES][8];
static int     deviceCount = 0;

static void printRom(const uint8_t *rom) {
  for (int i = 7; i >= 0; i--) {              // ESPHome-style 0x28..... (MSB first)
    if (rom[i] < 0x10) Serial.print('0');
    Serial.print(rom[i], HEX);
  }
}

static void enumerateBus() {
  deviceCount = 0;
  owSearchReset();
  uint8_t rom[8];
  while (deviceCount < MAX_DEVICES && owSearchNext(rom)) {
    memcpy(devices[deviceCount++], rom, 8);
  }
  Serial.printf("Found %d 1-Wire device(s):\n", deviceCount);
  for (int i = 0; i < deviceCount; i++) {
    Serial.print("  0x"); printRom(devices[i]);
    Serial.println(isTempSensor(devices[i][0]) ? "  (temperature sensor)" : "");
  }
}

// ------------------------------------------------------------------ setup / loop
void setup() {
  Serial.begin(115200);
  delay(300);
  Wire.begin(SDA_PIN, SCL_PIN, 100000);

  Serial.println(F("\nMOD-35 / DS2482-100 1-Wire hub"));
  for (uint8_t a = 0x18; a <= 0x1B; a++) {
    ds2482Addr = a;
    if (ds2482Reset()) {
      Serial.printf("DS2482 found at 0x%02X\n", a);
      break;
    }
    ds2482Addr = 0;
  }
  if (!ds2482Addr) {
    Serial.println(F("!! No DS2482 on 0x18..0x1B — check wiring/VCC. Halting."));
    while (true) delay(1000);
  }
  ds2482WriteConfig(DS2482_CFG_APU);          // active pull-up on, good for long cables
  enumerateBus();
}

void loop() {
  static uint32_t lastScan = 0;
  if (millis() - lastScan > 60000) {          // re-scan once a minute so hot-plugged sensors appear
    enumerateBus();
    lastScan = millis();
  }

  // Start a conversion on every sensor at once (Skip ROM), strong pull-up during the ~750 ms.
  if (owReset()) {
    owWriteByte(OW_SKIP_ROM);
    owStrongPullupNext();
    owWriteByte(DS18B20_CONVERT_T);
    delay(800);
    ds2482WriteConfig(DS2482_CFG_APU);        // SPU auto-clears, but make config explicit again
  }

  for (int i = 0; i < deviceCount; i++) {
    if (!isTempSensor(devices[i][0])) continue;
    float t = ds18ReadTemp(devices[i]);
    Serial.print("0x"); printRom(devices[i]); Serial.print(": ");
    if (t != t) Serial.println("read error");   // NaN check without relying on isnan macro
    else          Serial.printf("%.2f C\n", t);
  }
  Serial.println();
  delay(10000);
}
