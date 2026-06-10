/*
  HP Paper Tape Reader/Punch Emulator
  Target: STM32F103C8 using Roger Clark / Maple STM32 Arduino core

  Notes:
  - This is a first complete firmware structure based on the supplied requirements.
  - USB MSC support is isolated behind usbMscStart()/usbMscStop() because the exact
    API depends on the chosen USB MSC library for the Roger Clark core.
  - All timing-critical paper tape handshakes avoid Serial printing and GUI updates.
  - JTAG is disabled in setup() so PA15, PB3 and PB4 can be used as GPIO.
*/

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
//#include <SD.h>
#include <SdFat.h>
#include <USBComposite.h>
#include <USBMassStorage.h>
#include <U8g2lib.h>

#define DEBUG 0

#if DEBUG
  #define DBG_BEGIN() Serial1.begin(115200)
  #define DBG_PRINT(x) Serial1.print(x)
  #define DBG_PRINTLN(x) Serial1.println(x)
#else
  #define DBG_BEGIN()
  #define DBG_PRINT(x)
  #define DBG_PRINTLN(x)
#endif

SdFat SD;
static uint8_t usbCardType = 0;
// -----------------------------------------------------------------------------
// Pin mapping from requirements
// -----------------------------------------------------------------------------

static const uint8_t PIN_BTN_UP      = PB0;
static const uint8_t PIN_BTN_DOWN    = PB1;
static const uint8_t PIN_BTN_SELECT  = PB10;

static const uint8_t PIN_SD_CS       = PA4;
static const uint8_t PIN_SD_DETECT   = PA2;  // Active low, pull-up required

static const uint8_t PIN_USB_VBUS    = PA1;  // High when USB VBUS is present
static const uint8_t PIN_USB_PULLUP  = PA3;  // Drive high to connect USB pull-up circuit

static const uint8_t PIN_ACTIVITY_LED = PC13;

static const uint8_t PIN_BUS_DIR      = PB8;  // Active low: low enables external host-to-device buffer for punch input
static const uint8_t PIN_LEVEL_SELECT = PB2;  // High = 12 V interface, Low = 5 V interface
static const uint8_t PIN_OUT_OF_TAPE  = PC14;

static const uint8_t PIN_READER_CMD   = PB11;
static const uint8_t PIN_READER_ACK   = PA15;

static const uint8_t PIN_PUNCH_CMD    = PA0;
static const uint8_t PIN_PUNCH_ACK    = PC15;

static const uint8_t DATA_PINS[8] = {
  PB12, // D0
  PB13, // D1
  PB14, // D2
  PB15, // D3
  PB3,  // D4, requires JTAG disabled
  PB4,  // D5, requires JTAG disabled
  PB5,  // D6
  PA8   // D7
};

// -----------------------------------------------------------------------------
// USB MSC
// -----------------------------------------------------------------------------

// USBComposite MSC exposes the SD card as raw 512-byte sectors.
// Use the classic Sd2Card class bundled with Arduino SD.h for raw block access.
// This avoids mixing SD.h with the external SdFat 2.x library, which clashes with
// the older Roger Clark / Maple core.
Sd2Card rawCard;
USBMassStorage MassStorage;
static uint32_t usbMscSectorCount = 0;
static const uint16_t USB_PRODUCT_ID = 0x29;



static bool usbMscReadBlocks(uint8_t *readbuff, uint32_t startSector, uint16_t numSectors) {
  // Read raw 512-byte sectors from the SD card for the USB host.
  for (uint16_t i = 0; i < numSectors; i++) {

    // USB MSC always asks in 512-byte sector numbers.
    uint32_t lba = startSector + i;

    // Old SDSC cards use byte addressing in Sd2Card.
    // SDHC/SDXC cards use block addressing.
    if (usbCardType != SD_CARD_TYPE_SDHC) {
      lba *= 512UL;
    }

    // Copy this sector into the correct position in the USB buffer.
    if (!rawCard.readBlock(lba, readbuff + (i * 512UL))) {
      return false;
    }
  }

  return true;
}

static bool usbMscWriteBlocks(const uint8_t *writebuff, uint32_t startSector, uint16_t numSectors) {
  // Write raw sectors from the USB host to the SD card.
  for (uint16_t i = 0; i < numSectors; i++) {
        // USB MSC always asks in 512-byte sector numbers.
    uint32_t lba = startSector + i;

    // Old SDSC cards use byte addressing in Sd2Card.
    // SDHC/SDXC cards use block addressing.
    if (usbCardType != SD_CARD_TYPE_SDHC) {
      lba *= 512UL;
    }
    // Sd2Card writes one 512-byte sector at a time.
    if (!rawCard.writeBlock(startSector + i, writebuff + (i * 512UL))) {
      return false;
    }
  }
  return true;
}

static bool usbMscStatus(void) {
  // Report whether the SD card is still present.
  return checkSdPresent();
}

// -----------------------------------------------------------------------------
// Display
// -----------------------------------------------------------------------------

// SH1106 128x64 over hardware I2C on PB6/PB7 for STM32F103 I2C1.
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

static const char *CONFIG_FILE = "/ptape.cfg";
static const char *TAPE_DIR    = "/tapes";  // Shared directory for both reader input and punched output

#define MAX_FILES 32
#define MAX_NAME_LEN 64

struct InterfaceSettings {
  bool commandActiveHigh;
  bool ackPulseActiveHigh;
  bool dataActiveHigh;
};

struct AppConfig {
  InterfaceSettings reader;
  InterfaceSettings punch;
  bool use12V;
  char selectedReader[MAX_NAME_LEN];
  char selectedPunch[MAX_NAME_LEN];
};

AppConfig config;

// -----------------------------------------------------------------------------
// Runtime state
// -----------------------------------------------------------------------------

enum UiMode {
  UI_MAIN,
  UI_SECTION_FILES,
  UI_CONFIG,
  UI_USB_MSC
};

enum Section {
  SECTION_READER,
  SECTION_PUNCH
};

UiMode uiMode = UI_MAIN;
Section activeSection = SECTION_READER;
uint8_t mainCursor = 0;
uint8_t fileCursor = 0;
uint8_t configCursor = 0;

char readerFiles[MAX_FILES][MAX_NAME_LEN];
char punchFiles[MAX_FILES][MAX_NAME_LEN];
uint8_t readerFileCount = 0;
uint8_t punchFileCount = 0;

File readerFile;
File punchFile;
bool sdPresent = false;
bool emulatorEnabled = false;
bool usbMscEnabled = false;

unsigned long activityLedOffAtMs = 0;

// -----------------------------------------------------------------------------
// Button handling
// -----------------------------------------------------------------------------

struct ButtonState {
  uint8_t pin;
  bool stablePressed;
  bool lastRawPressed;
  unsigned long lastChangeMs;
  unsigned long pressedAtMs;
  bool longReported;
};

ButtonState buttons[3] = {
  { PIN_BTN_UP, false, false, 0, 0, false },
  { PIN_BTN_DOWN, false, false, 0, 0, false },
  { PIN_BTN_SELECT, false, false, 0, 0, false }
};

static const unsigned long DEBOUNCE_MS = 30;
static const unsigned long LONG_PRESS_MS = 4000;

// -----------------------------------------------------------------------------
// Low-level helpers
// -----------------------------------------------------------------------------

static bool isActive(bool signalLevel, bool activeHigh) {
  // Compare the physical input level with the configured active polarity.
  return activeHigh ? !signalLevel : signalLevel;
}

static void writeConfiguredLevel(uint8_t pin, bool active, bool activeHigh) {
  // Convert a logical active/inactive state into the configured physical level.
  digitalWrite(pin, active == activeHigh ? LOW : HIGH);
}

static void setDataBusInput(void) {
  // Put all shared data bus pins in high-impedance input mode.
  for (uint8_t i = 0; i < 8; i++) {
    pinMode(DATA_PINS[i], INPUT);
  }
}

static void setDataBusOutput(void) {
  // Put all shared data bus pins in push-pull output mode.
  for (uint8_t i = 0; i < 8; i++) {
    pinMode(DATA_PINS[i], OUTPUT);
  }
}

static void writeDataBus(uint8_t value, bool activeHigh) {
  // Write one byte to D0..D7, least significant bit first.
  for (uint8_t i = 0; i < 8; i++) {
    bool tmp = (value & (1U << i));
    digitalWrite(DATA_PINS[i], tmp == activeHigh ? HIGH : LOW);
  }
}

static uint8_t readDataBus(bool activeHigh) {
  // Read one byte from D0..D7, least significant bit first.
  uint8_t value = 0;
  uint8_t complementor = activeHigh?0x00:0xff;
  for (uint8_t i = 0; i < 8; i++) {
    if (digitalRead(DATA_PINS[i]) == HIGH) {
      value |= (1U << i);
    }
  }
  
  return value^complementor;
}

static void pulseAck(uint8_t pin, bool activeHigh) {
  // Generate an approximately 1 microsecond acknowledgement pulse.
  writeConfiguredLevel(pin, true, activeHigh);
  delayMicroseconds(5);
  writeConfiguredLevel(pin, false, activeHigh);
}

static void blinkActivity(void) {
  // PC13 is often wired as active-low LED on Blue Pill style boards.
  // If your hardware is active-high, invert these two writes.
  digitalWrite(PIN_ACTIVITY_LED, HIGH);
  activityLedOffAtMs = millis() + 1;
}

static void serviceActivityLed(void) {
  // Turn the activity LED off after a short visible blink time.
  if (activityLedOffAtMs != 0 && millis() >= activityLedOffAtMs) {
    digitalWrite(PIN_ACTIVITY_LED, LOW);
    activityLedOffAtMs = 0;
  }
}

static void applyLevelSelect(void) {
  // The spec says high selects 12 V and low selects 5 V.
  digitalWrite(PIN_LEVEL_SELECT, config.use12V ? HIGH : LOW);
}

static void updateOutOfTapeSignal(void) {
  // Normally signal paper/tape available. This assumes the pin indicates out-of-tape,
  // so inactive means paper is available.
  writeConfiguredLevel(PIN_OUT_OF_TAPE, false, config.punch.dataActiveHigh);
}

// -----------------------------------------------------------------------------
// SD and configuration
// -----------------------------------------------------------------------------

static bool checkSdPresent(void) {
  // SD detect is active low and requires pull-up.
  return digitalRead(PIN_SD_DETECT) == LOW;
}

static void setDefaultConfig(void) {
  // Conservative defaults. These can be changed from the configuration menu.
  config.reader.commandActiveHigh = false;
  config.reader.ackPulseActiveHigh = false;
  config.reader.dataActiveHigh = true;

  config.punch.commandActiveHigh = false;
  config.punch.ackPulseActiveHigh = false;
  config.punch.dataActiveHigh = true;
  config.use12V = false;

  config.selectedReader[0] = ' ';
  config.selectedPunch[0] = '\0';
}

static void trimLine(char *s) {
  // Remove trailing CR/LF and spaces from a line read from the config file.
  size_t len = strlen(s);
  while (len > 0 && (s[len - 1] == '\r' || s[len - 1] == '\n' || s[len - 1] == ' ')) {
    s[len - 1] = '\0';
    len--;
  }
}

static bool parseBoolValue(const char *value) {
  // Accept common true values in the config file.
  return strcmp(value, "1") == 0 || strcasecmp(value, "true") == 0 || strcasecmp(value, "yes") == 0;
}

static void applyConfigKeyValue(const char *key, const char *value) {
  // Apply one key=value pair from the configuration file.
  if (strcmp(key, "reader.commandActiveHigh") == 0) config.reader.commandActiveHigh = parseBoolValue(value);
  else if (strcmp(key, "reader.ackPulseActiveHigh") == 0) config.reader.ackPulseActiveHigh = parseBoolValue(value);
  else if (strcmp(key, "reader.dataActiveHigh") == 0) config.reader.dataActiveHigh = parseBoolValue(value);
  else if (strcmp(key, "use12V") == 0) config.use12V = parseBoolValue(value);  // Backward compatibility with old config files
  else if (strcmp(key, "punch.commandActiveHigh") == 0) config.punch.commandActiveHigh = parseBoolValue(value);
  else if (strcmp(key, "punch.ackPulseActiveHigh") == 0) config.punch.ackPulseActiveHigh = parseBoolValue(value);
  else if (strcmp(key, "punch.dataActiveHigh") == 0) config.punch.dataActiveHigh = parseBoolValue(value);
  else if (strcmp(key, "selectedReader") == 0) strncpy(config.selectedReader, value, MAX_NAME_LEN - 1);
  else if (strcmp(key, "selectedPunch") == 0) strncpy(config.selectedPunch, value, MAX_NAME_LEN - 1);

  config.selectedReader[MAX_NAME_LEN - 1] = '\0';
  config.selectedPunch[MAX_NAME_LEN - 1] = '\0';
}

static void loadConfig(void) {
  // Load configuration from SD card, or create defaults if no config exists.
  setDefaultConfig();

  if (!sdPresent || !SD.exists(CONFIG_FILE)) {
    return;
  }

  File f = SD.open(CONFIG_FILE, FILE_READ);
  if (!f) return;

  char line[96];
  uint8_t pos = 0;

  while (f.available()) {
    char c = (char)f.read();
    if (c == '\n' || pos >= sizeof(line) - 1) {
      line[pos] = '\0';
      trimLine(line);
      char *eq = strchr(line, '=');
      if (eq != NULL) {
        *eq = '\0';
        applyConfigKeyValue(line, eq + 1);
      }
      pos = 0;
    } else {
      line[pos++] = c;
    }
  }

  if (pos > 0) {
    line[pos] = '\0';
    trimLine(line);
    char *eq = strchr(line, '=');
    if (eq != NULL) {
      *eq = '\0';
      applyConfigKeyValue(line, eq + 1);
    }
  }

  f.close();
}

static void saveConfig(void) {
  // Save configuration to SD card as simple key=value text.
  if (!sdPresent) return;

  SD.remove(CONFIG_FILE);
  File f = SD.open(CONFIG_FILE, FILE_WRITE);
  if (!f) return;

  f.print("reader.commandActiveHigh="); f.println(config.reader.commandActiveHigh ? "1" : "0");
  f.print("reader.ackPulseActiveHigh="); f.println(config.reader.ackPulseActiveHigh ? "1" : "0");
  f.print("reader.dataActiveHigh="); f.println(config.reader.dataActiveHigh ? "1" : "0");
  f.print("device.use12V="); f.println(config.use12V ? "1" : "0");

  f.print("punch.commandActiveHigh="); f.println(config.punch.commandActiveHigh ? "1" : "0");
  f.print("punch.ackPulseActiveHigh="); f.println(config.punch.ackPulseActiveHigh ? "1" : "0");
  f.print("punch.dataActiveHigh="); f.println(config.punch.dataActiveHigh ? "1" : "0");

  f.print("selectedReader="); f.println(config.selectedReader);
  f.print("selectedPunch="); f.println(config.selectedPunch);

  f.close();
}

static void ensureDirectory(const char *path) {
  // Create a directory if the SD library and card support it.
  if (!SD.exists(path)) {
    SD.mkdir(path);
  }
}

static void scanDirectory(const char *path, char files[][MAX_NAME_LEN], uint8_t &count) {
  // Read up to MAX_FILES regular file names from one directory.
  count = 0;

  File dir = SD.open(path);
  if (!dir) return;

  while (count < MAX_FILES) {
    File entry = dir.openNextFile();
    if (!entry) break;

    if (!entry.isDirectory()) {
      // VictorPV/SdFat uses getName() instead of name().
      entry.getName(files[count], MAX_NAME_LEN);

      // Make extra sure the string is terminated.
      files[count][MAX_NAME_LEN - 1] = '\0';

      count++;
    }

    entry.close();
  }

  dir.close();
}

static void scanFiles(void) {
  // Ensure folders exist and refresh reader/punch file lists.
  if (!sdPresent) {
    readerFileCount = 0;
    punchFileCount = 0;
    return;
  }

  ensureDirectory(TAPE_DIR);
  scanDirectory(TAPE_DIR, readerFiles, readerFileCount);
  scanDirectory(TAPE_DIR, punchFiles, punchFileCount);
}

static void buildPath(char *out, size_t outSize, const char *dir, const char *name) {
  // Build a simple /dir/name path.
  snprintf(out, outSize, "%s/%s", dir, name);
}

static void closeTapeFiles(void) {
  // Close open files before changing mode, SD removal, or USB MSC entry.
  if (readerFile) readerFile.close();
  if (punchFile) punchFile.close();
}

static void openSelectedFiles(void) {
  // Open selected reader and punch files for emulator operation.
  closeTapeFiles();

  if (!sdPresent) return;

  char path[80];

  if (config.selectedReader[0] != '\0') {
    buildPath(path, sizeof(path), TAPE_DIR, config.selectedReader);
    readerFile = SD.open(path, FILE_READ);
  }

  if (config.selectedPunch[0] != '\0') {
    buildPath(path, sizeof(path), TAPE_DIR, config.selectedPunch);
    punchFile = SD.open(path, FILE_WRITE);
    if (punchFile) {
      punchFile.seek(punchFile.size());
    }
  }
}

static bool reopenReaderFromBeginning(void) {
  // Re-open the selected reader tape from byte 0.
  // This is used as a "rewind tape" action from a long DOWN press.
  if (readerFile) {
    readerFile.close();
  }

  readerWaitingForRelease = false;

  prepareReaderBusIdle();

  if (!sdPresent || config.selectedReader[0] == '\0') {
    DBG_PRINTLN("Reader rewind failed: no SD card or no reader file selected");
    return false;
  }

  char path[80];
  buildPath(path, sizeof(path), TAPE_DIR, config.selectedReader);

  readerFile = SD.open(path, FILE_READ);
  if (!readerFile) {
    DBG_PRINT("Reader rewind failed: ");
    DBG_PRINTLN(path);
    return false;
  }

  DBG_PRINT("Reader rewound: ");
  DBG_PRINTLN(path);
  return true;
}

static bool createNewPunchFile(void) {
  // Create PUNCHnnn.BIN using the first available sequence number.
  if (!sdPresent) return false;

  char name[MAX_NAME_LEN];
  char path[80];

  for (uint16_t n = 0; n < 1000; n++) {
    snprintf(name, sizeof(name), "PUNCH%03u.BIN", n);
    buildPath(path, sizeof(path), TAPE_DIR, name);

    if (!SD.exists(path)) {
      File f = SD.open(path, FILE_WRITE);
      if (!f) return false;
      f.close();

      strncpy(config.selectedPunch, name, MAX_NAME_LEN - 1);
      config.selectedPunch[MAX_NAME_LEN - 1] = '\0';
      saveConfig();
      scanFiles();
      openSelectedFiles();
      return true;
    }
  }

  return false;
}

// -----------------------------------------------------------------------------
// USB MSC adapter hooks
// -----------------------------------------------------------------------------


static bool usbMscStart(void) {
  // Enter USB mass-storage mode. The SD card must no longer be used by the
  // paper tape emulator while the host computer owns it as a USB disk.
  if (!sdPresent) return false;
  if (digitalRead(PIN_USB_VBUS) == LOW) return false;

  closeTapeFiles();
  emulatorEnabled = false;

  // Initialize raw SD access for USB MSC sector reads/writes.
  // Sd2Card uses the classic SD library speed constants.
  if (!rawCard.init(SPI_FULL_SPEED, PIN_SD_CS)) {
    return false;
  }

  usbMscSectorCount = rawCard.cardSize();
  if (usbMscSectorCount == 0) {
    return false;
  }
  usbCardType = rawCard.type();
  // Build a simple USB MSC device. Avoid adding USB serial here because the
  // STM32F1 USB endpoint/buffer space is limited and MSC alone is enough.
  USBComposite.clear();
  USBComposite.setProductId(USB_PRODUCT_ID);
  MassStorage.clearDrives();
  MassStorage.setDriveData(0, usbMscSectorCount, usbMscReadBlocks, usbMscWriteBlocks, usbMscStatus);
  MassStorage.registerComponent();

  // Pull up PA3 to enable the external USB connect circuit described in the spec.
  digitalWrite(PIN_USB_PULLUP, HIGH);

  if (!USBComposite.begin()) {
    digitalWrite(PIN_USB_PULLUP, LOW);
    return false;
  }

  usbMscEnabled = true;
  uiMode = UI_USB_MSC;
  return true;
}

static void usbMscStop(void) {
  // Stop USB MSC and return control of the SD card to the emulator.
  MassStorage.end();
  digitalWrite(PIN_USB_PULLUP, LOW);

  usbMscEnabled = false;
  usbMscSectorCount = 0;
  uiMode = UI_MAIN;

  // Re-mount the filesystem view after the host has released the card.
  if (sdPresent) {
    SD.begin(PIN_SD_CS);
  }

  scanFiles();
  openSelectedFiles();
  emulatorEnabled = sdPresent;
}

// -----------------------------------------------------------------------------
// Paper tape emulator
// -----------------------------------------------------------------------------

static void prepareReaderBusIdle(void) {
  // Idle with the external host-to-device buffer disabled to avoid bus contention.
  digitalWrite(PIN_BUS_DIR, HIGH);
  setDataBusInput();
}

static void serviceReader(void) {
  bool cmdActive = isActive(digitalRead(PIN_READER_CMD) == HIGH,
                            config.reader.commandActiveHigh);
  
  DBG_PRINT("A");
  DBG_PRINT(cmdActive);
  /*if (readerWaitingForRelease) {
    DBG_PRINT("B");
    if (!cmdActive) {
      DBG_PRINT("C");
      readerWaitingForRelease = false;
      prepareReaderBusIdle();
    }
    return;
  }*/
  DBG_PRINT("D");
  if (!cmdActive) return;
  DBG_PRINT("H");
  if (!readerFile) return;
  DBG_PRINT("E");
  digitalWrite(PIN_BUS_DIR, HIGH);
  setDataBusOutput();

  int b = readerFile.read();

  if (b < 0) {
    DBG_PRINT("F");
    // if run out we return 0xff as if there is no tape at all!
    b=0xff;
  }
  DBG_PRINT("=");
  DBG_PRINT(b);
  DBG_PRINT("*");
  writeDataBus((uint8_t)b, config.reader.dataActiveHigh);
  pulseAck(PIN_READER_ACK, config.reader.ackPulseActiveHigh);
  blinkActivity();

  readerWaitingForRelease = true;
  DBG_PRINT("G");
}

static void servicePunch(void) {
  bool cmdActive = isActive(digitalRead(PIN_PUNCH_CMD) == HIGH,
                            config.punch.commandActiveHigh);


  if (!cmdActive || !punchFile) return;
  setDataBusInput();
  digitalWrite(PIN_BUS_DIR, LOW);

  uint8_t b = readDataBus(config.punch.dataActiveHigh);
  punchFile.write(b);
  punchFile.flush();

  pulseAck(PIN_PUNCH_ACK, config.punch.ackPulseActiveHigh);
  blinkActivity();
}

static void serviceTapeEmulator(void) {
  // The emulator is disabled while USB MSC owns the SD card.
  if (!emulatorEnabled || usbMscEnabled || !sdPresent) return;

  serviceReader();
  servicePunch();
}

// -----------------------------------------------------------------------------
// UI helpers
// -----------------------------------------------------------------------------

static bool pollButtonEvent(uint8_t buttonIndex, bool &shortPress, bool &longPress) {
  ButtonState &b = buttons[buttonIndex];
  // Debounce one active-low button and report short/long press events.
  shortPress = false;
  longPress = false;

  bool rawPressed = digitalRead(b.pin) == LOW;
  unsigned long now = millis();

  if (rawPressed != b.lastRawPressed) {
    b.lastRawPressed = rawPressed;
    b.lastChangeMs = now;
  }

  if ((now - b.lastChangeMs) < DEBOUNCE_MS) {
    return false;
  }

  if (rawPressed != b.stablePressed) {
    b.stablePressed = rawPressed;

    if (b.stablePressed) {
      b.pressedAtMs = now;
      b.longReported = false;
    } else {
      if (!b.longReported && (now - b.pressedAtMs) < LONG_PRESS_MS) {
        shortPress = true;
      }
    }
  }

  if (b.stablePressed && !b.longReported && (now - b.pressedAtMs) >= LONG_PRESS_MS) {
    b.longReported = true;
    longPress = true;
  }

  return shortPress || longPress;
}

static uint8_t selectedFileCount(void) {
  // Return current section's number of files.
  return activeSection == SECTION_READER ? readerFileCount : punchFileCount;
}

static const char *fileNameAt(uint8_t index) {
  // Return the file name at index for the current section.
  return activeSection == SECTION_READER ? readerFiles[index] : punchFiles[index];
}

static bool isSelectedFile(const char *name) {
  // Check whether a file name is the currently selected file.
  return activeSection == SECTION_READER
    ? strcmp(name, config.selectedReader) == 0
    : strcmp(name, config.selectedPunch) == 0;
}

static void selectFile(const char *name) {
  // Store selected file for current section and reopen emulator files.
  if (activeSection == SECTION_READER) {
    strncpy(config.selectedReader, name, MAX_NAME_LEN - 1);
    config.selectedReader[MAX_NAME_LEN - 1] = '\0';
  } else {
    strncpy(config.selectedPunch, name, MAX_NAME_LEN - 1);
    config.selectedPunch[MAX_NAME_LEN - 1] = '\0';
  }

  saveConfig();
  openSelectedFiles();
}

static void drawHeader(const char *title) {
  // Draw common top header.
  display.setFont(u8g2_font_6x10_tf);
  display.drawStr(0, 8, title);
  display.drawHLine(0, 10, 128);
}

static void drawMainUi(void) {
  // Main UI with upper Reader and lower Punch sections.
  display.clearBuffer();
  drawHeader("HP Paper Tape");

  display.setFont(u8g2_font_6x10_tf);

  if (mainCursor == 0) display.drawBox(0, 14, 128, 22);
  display.setDrawColor(mainCursor == 0 ? 0 : 1);
  display.drawStr(3, 24, "Reader");
  display.drawStr(3, 34, config.selectedReader[0] ? config.selectedReader : "<no file>");
  display.setDrawColor(1);

  if (mainCursor == 1) display.drawBox(0, 40, 128, 22);
  display.setDrawColor(mainCursor == 1 ? 0 : 1);
  display.drawStr(3, 50, "Punch");
  display.drawStr(3, 60, config.selectedPunch[0] ? config.selectedPunch : "<no file>");
  display.setDrawColor(1);

  display.sendBuffer();
}

static void drawTruncatedStr(uint8_t x, uint8_t y, const char *text, uint8_t maxChars) {
  // Draw a string that fits in the available screen width.
  // If it is too long, keep the start of the filename and add '~'.
  char buf[32];
  uint8_t len = strlen(text);

  if (len <= maxChars) {
    display.drawStr(x, y, text);
    return;
  }

  if (maxChars >= sizeof(buf)) {
    maxChars = sizeof(buf) - 1;
  }

  strncpy(buf, text, maxChars);
  buf[maxChars - 1] = '~';
  buf[maxChars] = ' ';
  display.drawStr(x, y, buf);
}

static void drawFileUi(void) {
  // Draw file selection list for either Reader or Punch.
  // The cursor stays visible; when moving past the fourth visible row,
  // the list scrolls upward like the configuration menu.
  display.clearBuffer();
  drawHeader(activeSection == SECTION_READER ? "Reader files" : "Punch files");

  uint8_t rows = 4;
  uint8_t count = selectedFileCount();
  uint8_t extra = activeSection == SECTION_PUNCH ? 1 : 0;
  uint8_t total = count + extra;
  uint8_t first = 0;

  if (fileCursor >= rows) {
    first = fileCursor - rows + 1;
  }

  for (uint8_t row = 0; row < rows; row++) {
    uint8_t item = first + row;
    if (item >= total) break;

    uint8_t y = 22 + row * 12;
    bool highlighted = item == fileCursor;

    if (highlighted) display.drawBox(0, y - 9, 128, 11);
    display.setDrawColor(highlighted ? 0 : 1);

    if (activeSection == SECTION_PUNCH && item == 0) {
      display.drawStr(3, y, "+ New punch file");
    } else {
      uint8_t fileIndex = activeSection == SECTION_PUNCH ? item - 1 : item;
      const char *name = fileNameAt(fileIndex);
      display.drawStr(3, y, isSelectedFile(name) ? "*" : " ");
      drawTruncatedStr(11, y, name, 19);
    }

    display.setDrawColor(1);
  }

  display.sendBuffer();
}

static const char *configItemName(uint8_t index) {
  // Return the short label for one configuration list item.
  switch (index) {
    case 0: return "RDR CMD ACTIVE";
    case 1: return "RDR FLAG ACTIVE";
    case 2: return "RDR DATA ACTIVE";
    case 3: return "PUN CMD ACTIVE";
    case 4: return "PUN FLAG ACTIVE";
    case 5: return "PUN DATA ACTIVE";
    case 6: return "SIGNAL LEVEL";
    case 7: return "EXIT";
    default: return "";
  }
}

static const char *configItemValueText(uint8_t index) {
  // Return the visible value text for one configuration item.
  // Signal polarity items show HIGH/LOW, and signal level shows +5V/+12V.
  switch (index) {
    case 0: return config.reader.commandActiveHigh ? "HIGH" : "LOW";
    case 1: return config.reader.ackPulseActiveHigh ? "HIGH" : "LOW";
    case 2: return config.reader.dataActiveHigh ? "HIGH" : "LOW";
    case 3: return config.punch.commandActiveHigh ? "HIGH" : "LOW";
    case 4: return config.punch.ackPulseActiveHigh ? "HIGH" : "LOW";
    case 5: return config.punch.dataActiveHigh ? "HIGH" : "LOW";
    case 6: return config.use12V ? "+12V" : "+5V";
    default: return "";
  }
}

static void toggleConfigItem(uint8_t index) {
  // Toggle one configuration item or exit config mode.
  switch (index) {
    case 0: config.reader.commandActiveHigh = !config.reader.commandActiveHigh; break;
    case 1: config.reader.ackPulseActiveHigh = !config.reader.ackPulseActiveHigh; break;
    case 2: config.reader.dataActiveHigh = !config.reader.dataActiveHigh; break;
    case 3: config.punch.commandActiveHigh = !config.punch.commandActiveHigh; break;
    case 4: config.punch.ackPulseActiveHigh = !config.punch.ackPulseActiveHigh; break;
    case 5: config.punch.dataActiveHigh = !config.punch.dataActiveHigh; break;
    case 6: config.use12V = !config.use12V; break;
    case 7: {
      uiMode = UI_MAIN;
      writeConfiguredLevel(PIN_READER_ACK, false, config.reader.ackPulseActiveHigh);
      writeConfiguredLevel(PIN_PUNCH_ACK, false, config.punch.ackPulseActiveHigh);       
    }
    break;
  }

  saveConfig();
  applyLevelSelect();
  updateOutOfTapeSignal();
}

static void drawConfigUi(void) {
  // Draw hardware configuration screen.
  display.clearBuffer();
  drawHeader("Configuration");

  uint8_t first = 0;
  if (configCursor > 3) first = configCursor - 3;

  for (uint8_t row = 0; row < 5; row++) {
    uint8_t item = first + row;
    if (item > 7) break;

    uint8_t y = 22 + row * 10;
    bool highlighted = item == configCursor;

    if (highlighted) display.drawBox(0, y - 8, 128, 10);
    display.setDrawColor(highlighted ? 0 : 1);
    display.drawStr(2, y, configItemName(item));

    if (item < 7) {
      display.drawStr(98, y, configItemValueText(item));
    }

    display.setDrawColor(1);
  }

  display.sendBuffer();
}

static void drawUsbUi(void) {
  // Draw USB stick mode screen.
  display.clearBuffer();
  drawHeader("USB stick mode");
  display.drawStr(0, 26, sdPresent ? "SD exported over USB" : "SD card missing");
  display.drawBox(0, 42, 128, 14);
  display.setDrawColor(0);
  display.drawStr(4, 52, "Exit USB stick mode");
  display.setDrawColor(1);
  display.sendBuffer();
}

static void redrawUi(void) {
  // Redraw the display for the current UI mode.
  if (uiMode == UI_MAIN) drawMainUi();
  else if (uiMode == UI_SECTION_FILES) drawFileUi();
  else if (uiMode == UI_CONFIG) drawConfigUi();
  else if (uiMode == UI_USB_MSC) drawUsbUi();
}

static void handleUiEvent(uint8_t buttonPin, bool shortPress, bool longPress) {
  // Dispatch button events according to the current UI mode.
  if (longPress && buttonPin == PIN_BTN_SELECT) {
    uiMode = UI_CONFIG;
    configCursor = 0;
    redrawUi();
    return;
  }

  if (longPress && buttonPin == PIN_BTN_UP) {
    usbMscStart();
    redrawUi();
    return;
  }

  if (longPress && buttonPin == PIN_BTN_DOWN) {
    // Long DOWN acts as reader tape rewind.
    // It re-opens the selected reader file from the beginning without
    // blocking the UI or changing the selected file.
    reopenReaderFromBeginning();
    redrawUi();
    return;
  }

  if (!shortPress) return;

  if (uiMode == UI_USB_MSC) {
    if (buttonPin == PIN_BTN_SELECT) {
      usbMscStop();
    }
    redrawUi();
    return;
  }

  if (uiMode == UI_MAIN) {
    if (buttonPin == PIN_BTN_UP || buttonPin == PIN_BTN_DOWN) {
      mainCursor = mainCursor == 0 ? 1 : 0;
      activeSection = mainCursor == 0 ? SECTION_READER : SECTION_PUNCH;
      applyLevelSelect();
    } else if (buttonPin == PIN_BTN_SELECT) {
      uiMode = UI_SECTION_FILES;
      fileCursor = 0;
    }
  } else if (uiMode == UI_SECTION_FILES) {
    uint8_t count = selectedFileCount();
    uint8_t total = count + (activeSection == SECTION_PUNCH ? 1 : 0);

    if (buttonPin == PIN_BTN_UP && total > 0) {
      // Clamp at the first item instead of wrapping.
      if (fileCursor > 0) fileCursor--;
    } else if (buttonPin == PIN_BTN_DOWN && total > 0) {
      // Clamp at the last item instead of letting the cursor disappear.
      if (fileCursor + 1 < total) fileCursor++;
    } else if (buttonPin == PIN_BTN_SELECT) {
      if (activeSection == SECTION_PUNCH && fileCursor == 0) {
        createNewPunchFile();
      } else {
        uint8_t fileIndex = activeSection == SECTION_PUNCH ? fileCursor - 1 : fileCursor;
        if (fileIndex < count) selectFile(fileNameAt(fileIndex));
      }
      uiMode = UI_MAIN;
    }
  } else if (uiMode == UI_CONFIG) {
    if (buttonPin == PIN_BTN_UP) {
      configCursor = configCursor == 0 ? 7 : configCursor - 1;
    } else if (buttonPin == PIN_BTN_DOWN) {
      configCursor = (configCursor + 1) % 8;
    } else if (buttonPin == PIN_BTN_SELECT) {
      toggleConfigItem(configCursor);
    }
  }

  redrawUi();
}

static void serviceButtons(void) {
  // Poll all three buttons and generate UI actions.
  bool shortPress;
  bool longPress;

  if (pollButtonEvent(0, shortPress, longPress)) handleUiEvent(PIN_BTN_UP, shortPress, longPress);
  if (pollButtonEvent(1, shortPress, longPress)) handleUiEvent(PIN_BTN_DOWN, shortPress, longPress);
  if (pollButtonEvent(2, shortPress, longPress)) handleUiEvent(PIN_BTN_SELECT, shortPress, longPress);
}

// -----------------------------------------------------------------------------
// Initialization and main loop
// -----------------------------------------------------------------------------

static void disableJtagKeepSwd(void) {
  // Free PA15, PB3 and PB4 while keeping SWD on PA13/PA14.
  // This register access works on the STM32F1 Maple/libmaple core.
  afio_cfg_debug_ports(AFIO_DEBUG_SW_ONLY);
}

void setup(void) {
  // Disable JTAG before configuring PA15/PB3/PB4 as GPIO.
  disableJtagKeepSwd();

  pinMode(PIN_BTN_UP, INPUT_PULLUP);
  pinMode(PIN_BTN_DOWN, INPUT_PULLUP);
  pinMode(PIN_BTN_SELECT, INPUT_PULLUP);

  pinMode(PIN_SD_DETECT, INPUT_PULLUP);
  pinMode(PIN_USB_VBUS, INPUT);

  pinMode(PIN_USB_PULLUP, OUTPUT);
  digitalWrite(PIN_USB_PULLUP, LOW);

  pinMode(PIN_ACTIVITY_LED, OUTPUT);
  digitalWrite(PIN_ACTIVITY_LED, LOW);

  pinMode(PIN_BUS_DIR, OUTPUT);
  digitalWrite(PIN_BUS_DIR, LOW);

  pinMode(PIN_LEVEL_SELECT, OUTPUT);
  digitalWrite(PIN_LEVEL_SELECT, LOW);

  pinMode(PIN_OUT_OF_TAPE, OUTPUT);
 
  pinMode(PIN_READER_ACK, OUTPUT);
  pinMode(PIN_PUNCH_ACK, OUTPUT);

  pinMode(PIN_READER_CMD, INPUT);
  pinMode(PIN_PUNCH_CMD, INPUT);

  setDataBusInput();
  setDefaultConfig();

  // Initialize I2C display.
  display.begin();
  display.setFont(u8g2_font_6x10_tf);

  sdPresent = checkSdPresent();
  if (sdPresent) {
    SPI.begin();
    sdPresent = SD.begin(PIN_SD_CS);
  }

  loadConfig();
  writeConfiguredLevel(PIN_READER_ACK, false, config.reader.ackPulseActiveHigh);
  writeConfiguredLevel(PIN_PUNCH_ACK, false, config.punch.ackPulseActiveHigh);
  scanFiles();
  openSelectedFiles();
  applyLevelSelect();
  updateOutOfTapeSignal();
  prepareReaderBusIdle();

  emulatorEnabled = sdPresent;
  redrawUi();
  DBG_BEGIN();
  DBG_PRINTLN("Paper tape emulator booting");
}

void loop(void) {
  // Stop SD-using functions immediately if the card is removed.
  bool nowSdPresent = checkSdPresent();
  if (nowSdPresent != sdPresent) {
    sdPresent = nowSdPresent;

    if (!sdPresent) {
      closeTapeFiles();
      emulatorEnabled = false;
      if (usbMscEnabled) usbMscStop();
    } else {
      SD.begin(PIN_SD_CS);
      loadConfig();
      scanFiles();
      openSelectedFiles();
      emulatorEnabled = !usbMscEnabled;
    }

    redrawUi();
  }

  serviceButtons();
  serviceActivityLed();

  if (!usbMscEnabled) {
    serviceTapeEmulator();
  } else {
    MassStorage.loop();
  }
  //delayMicroseconds(10000);
}
