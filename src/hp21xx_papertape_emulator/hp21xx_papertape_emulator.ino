/*
  STM32F103C8 Paper Tape Emulator for HP 12566 (5V logic via buffers)
  Roger Clark Arduino_STM32 core + USBComposite MSC + SdFat + U8g2

  Updates in this version (per your request)
  - PA3 = USB pull-up enable output (controls PMOS that connects 1.5k to 3.3V on D+)
      * PA3 LOW  => PMOS ON  => USB "attached"
      * PA3 HIGH => PMOS OFF => USB "detached"
    (Assumes you also have a 100k gate pull-up to 3.3V to default OFF at reset.)

  - PA2 = SD card detect input (active-low)
      * PA2 LOW  => SD card inserted
      * Uses internal pull-up (INPUT_PULLUP)

  Behavior
  - If SD not inserted: SD init is skipped, MSC not started, UI shows "Insert SD".
  - If SD inserted: SD initialized, config loaded from /PTRCFG.TXT, file list scanned.
  - USB MSC is only started when SD is inserted.
  - USB host detection is still by enumeration (USBComposite becomes "ready").
    If a host enumerates: emulation stops immediately and MSC becomes active.
    If only a USB charger: no enumeration => emulation continues.

  Notes
  - This sketch avoids SD filesystem operations while MSC is active (host mounted).
  - To run emulation while host is connected, open Settings (hold Select >4s) and choose "Start emulation".
    That detaches USB (PA3 HIGH + USBComposite.end()) and resumes emulation.
    Choose "Stop emulation" to re-enable MSC (PA3 LOW + USBComposite.begin()).

  Libraries
  - U8g2
  - SdFat
  - USBComposite (Roger Clark core)

  IMPORTANT hardware (shared DATA pins with two 245 buffers)
  - Shared node: STM32 GPIO pins connect to BOTH:
      * 74HCT245 A-side (device->HP)  [5V] DIR fixed A->B
      * 74LVC245 B-side (HP->device)  [3.3V] DIR fixed A->B
  - Control BOTH /OE pins:
      * HCT245 /OE active-low: enable ONLY in Reader mode
      * LVC245 /OE active-low: enable ONLY in Punch mode
*/

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <SdFat.h>
#include <USBComposite.h>

// ---------------- OLED ----------------
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// ---------------- SD + MSC ----------------
SdFat sd;
SdFile ioFile;
USBMassStorage MassStorage;

static uint32_t g_sectorCount = 0;
static bool usbStarted = false;
static bool usbHostConfigured = false;

// ---------------- Pins ----------------
// Buttons (active-low)
static const uint8_t PIN_BTN_UP     = PB0;
static const uint8_t PIN_BTN_DOWN   = PB1;
static const uint8_t PIN_BTN_SELECT = PB10;

// HP handshake
static const uint8_t PIN_HP_CMD_IN   = PB11;  // CMD after 5V->3.3V conditioning
static const uint8_t PIN_HP_FLAG_OUT = PA15;  // FLAG output (JTAG pin is OK after disabling JTAG)

// Shared DATA pins (avoid PA9/10 UART, PA11/12 USB, PA13/14 SWD)
static const uint8_t DATA_PINS[8] = {
  PB12, PB13, PB14, PB15,  // bits 0..3
  PB3,  PB4,  PB5,  PA8    // bits 4..7
};

// /OE pins (active-low)
static const uint8_t PIN_OE_HCT_OUT = PB9;  // HCT245 /OE
static const uint8_t PIN_OE_LVC_IN  = PB8;  // LVC245 /OE

// SD SPI1
static const uint8_t PIN_SD_CS = PA4;

// SD card detect (active-low, internal pull-up)
static const uint8_t PIN_SD_DETECT = PA2;

// USB pull-up enable (controls PMOS switch for 1.5k to 3.3V on D+)
static const uint8_t PIN_USB_PULLUP_EN = PA3;

// Activity LED
static const uint8_t PIN_LED = PC13;

// ---------------- Config ----------------
enum class Mode : uint8_t { Reader = 0, Punch = 1 };
enum class CmdEdge : uint8_t { Rising = 0, Falling = 1 };

struct Settings {
  Mode mode;
  bool invData;
  bool invCmd;
  bool invFlag;
  uint16_t flagPulseUs;
  CmdEdge cmdEdge;
  bool ledEnable;
};

static Settings g = {
  Mode::Reader,
  false,
  false,
  false,
  1,
  CmdEdge::Rising,
  true
};

static const char *CFG_PATH = "/PTRCFG.TXT";

// ---------------- States ----------------
enum class SysState : uint8_t {
  EMU_RUNNING,
  USB_MSC_ACTIVE,
  EMU_FORCED_WHILE_HOST
};

static SysState sysState = SysState::EMU_RUNNING;

enum class UiState : uint8_t { Main, SettingsMenu };
static UiState uiState = UiState::Main;

// ---------------- Timing / flags ----------------
static const uint32_t DEBOUNCE_MS = 140;
static uint32_t lastUiTickMs = 0;

static bool selWasDown = false;
static uint32_t selDownAt = 0;

volatile bool cmdSeen = false;

static bool sdOk = false;
static bool sdInserted = false;
static bool armed = false;

static uint32_t lastUsbPollMs = 0;
static uint32_t lastSdPollMs = 0;

// Reader file list
static const uint16_t MAX_FILES = 128;
static String files[MAX_FILES];
static uint16_t fileCount = 0;
static int16_t selectedIndex = 0;
static int16_t topIndex = 0;

// Punch capture file
static uint16_t capIndex = 0;
static char capName[16] = "CAP0000.BIN";
static int punchCursor = 0;
static uint32_t punchBytesSinceSync = 0;

// Settings menu cursor
static int menuIndex = 0;

// LED state
static uint32_t ledLastToggleMs = 0;
static bool ledState = false;

// ---------------- Helpers: Buttons ----------------
bool btnPressed(uint8_t pin) {
  return (digitalRead(pin) == LOW);
}

// ---------------- Helpers: LED ----------------
void ledSet(bool on) {
  if (!g.ledEnable) return;
  // PC13 often active-low (Blue Pill). Flip if your PCB uses active-high.
  digitalWrite(PIN_LED, on ? LOW : HIGH);
  ledState = on;
}

void ledPulseShort() {
  if (!g.ledEnable) return;
  ledSet(true);
  delay(2);
  ledSet(false);
}

void ledUpdatePattern() {
  if (!g.ledEnable) return;
  uint32_t now = millis();

  uint32_t interval;
  if (!sdInserted) interval = 1200;                      // SD missing: very slow blink
  else if (sysState == SysState::USB_MSC_ACTIVE) interval = 250; // USB MSC mode
  else interval = armed ? 150 : 600;                     // Armed/idle

  if (now - ledLastToggleMs >= interval) {
    ledLastToggleMs = now;
    ledSet(!ledState);
  }
}

// ---------------- Helpers: Trim / parse ----------------
String trimStr(const String &s) {
  int start = 0;
  int end = (int)s.length() - 1;
  while (start <= end && isspace((unsigned char)s[start])) start++;
  while (end >= start && isspace((unsigned char)s[end])) end--;
  return s.substring(start, end + 1);
}

bool parseBool01(const String &v, bool fallback) {
  if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
  if (v == "0" || v == "false" || v == "no" || v == "off") return false;
  return fallback;
}

// ---------------- USB pull-up control (PA3) ----------------
void usbAttachPullup() {
  // PMOS ON => attach => GPIO LOW
  digitalWrite(PIN_USB_PULLUP_EN, LOW);
}

void usbDetachPullup() {
  // PMOS OFF => detach => GPIO HIGH
  digitalWrite(PIN_USB_PULLUP_EN, HIGH);
}

// ---------------- SD detect (PA2) ----------------
bool readSdInserted() {
  // Active-low: LOW means card present
  return (digitalRead(PIN_SD_DETECT) == LOW);
}

// ---------------- SD: config file ----------------
void writeDefaultConfig() {
  SdFile f;
  if (!f.open(CFG_PATH, O_CREAT | O_TRUNC | O_WRITE)) return;

  f.print("mode=reader\n");
  f.print("inv_data=0\n");
  f.print("inv_cmd=0\n");
  f.print("inv_flag=0\n");
  f.print("flag_us=1\n");
  f.print("cmd_edge=rising\n");
  f.print("led_enable=1\n");
  f.close();
}

void saveConfigToSd() {
  if (!sdOk) return;
  SdFile f;
  if (!f.open(CFG_PATH, O_CREAT | O_TRUNC | O_WRITE)) return;

  f.print("mode=");
  f.print((g.mode == Mode::Reader) ? "reader\n" : "punch\n");

  f.print("inv_data=");
  f.print(g.invData ? "1\n" : "0\n");

  f.print("inv_cmd=");
  f.print(g.invCmd ? "1\n" : "0\n");

  f.print("inv_flag=");
  f.print(g.invFlag ? "1\n" : "0\n");

  f.print("flag_us=");
  f.print(g.flagPulseUs);
  f.print("\n");

  f.print("cmd_edge=");
  f.print((g.cmdEdge == CmdEdge::Rising) ? "rising\n" : "falling\n");

  f.print("led_enable=");
  f.print(g.ledEnable ? "1\n" : "0\n");

  f.close();
}

void loadConfigFromSd() {
  if (!sdOk) return;

  if (!sd.exists(CFG_PATH)) {
    writeDefaultConfig();
    return;
  }

  SdFile f;
  if (!f.open(CFG_PATH, O_RDONLY)) return;

  String line;
  while (f.available()) {
    char c = (char)f.read();
    if (c == '\r') continue;

    if (c == '\n') {
      line = trimStr(line);

      if (line.length() == 0 || line.startsWith("#") || line.startsWith(";")) {
        line = "";
        continue;
      }

      int eq = line.indexOf('=');
      if (eq > 0) {
        String key = trimStr(line.substring(0, eq));
        String val = trimStr(line.substring(eq + 1));
        key.toLowerCase();
        val.toLowerCase();

        if (key == "mode") {
          if (val == "reader") g.mode = Mode::Reader;
          if (val == "punch")  g.mode = Mode::Punch;
        } else if (key == "inv_data") {
          g.invData = parseBool01(val, g.invData);
        } else if (key == "inv_cmd") {
          g.invCmd = parseBool01(val, g.invCmd);
        } else if (key == "inv_flag") {
          g.invFlag = parseBool01(val, g.invFlag);
        } else if (key == "flag_us") {
          int n = val.toInt();
          if (n >= 1 && n <= 50) g.flagPulseUs = (uint16_t)n;
        } else if (key == "cmd_edge") {
          if (val == "rising") g.cmdEdge = CmdEdge::Rising;
          if (val == "falling") g.cmdEdge = CmdEdge::Falling;
        } else if (key == "led_enable") {
          g.ledEnable = parseBool01(val, g.ledEnable);
        }
      }

      line = "";
    } else {
      if (line.length() < 120) line += c;
    }
  }

  f.close();
}

// ---------------- JTAG disable (keep SWD) ----------------
void disableJtagKeepSwd() {
#if defined(STM32F1xx) || defined(ARDUINO_ARCH_STM32)
  #ifdef RCC_APB2ENR_AFIOEN
    RCC->APB2ENR |= RCC_APB2ENR_AFIOEN;
  #endif
  AFIO->MAPR &= ~(0x7 << 24);
  AFIO->MAPR |=  (0x2 << 24);
#endif
}

// ---------------- HP bus helpers ----------------
void setFlag(bool asserted) {
  bool level = asserted;
  if (g.invFlag) level = !level;
  digitalWrite(PIN_HP_FLAG_OUT, level ? HIGH : LOW);
}

void pulseFlag() {
  setFlag(true);
  delayMicroseconds(g.flagPulseUs);
  setFlag(false);
}

void setHctEnabled(bool enable) {
  digitalWrite(PIN_OE_HCT_OUT, enable ? LOW : HIGH); // active-low /OE
}

void setLvcEnabled(bool enable) {
  digitalWrite(PIN_OE_LVC_IN, enable ? LOW : HIGH);  // active-low /OE
}

void writeDataBus(uint8_t b) {
  if (g.invData) b = ~b;
  for (int i = 0; i < 8; i++) {
    digitalWrite(DATA_PINS[i], (b >> i) & 0x01);
  }
}

uint8_t readDataBus() {
  uint8_t b = 0;
  for (int i = 0; i < 8; i++) {
    if (digitalRead(DATA_PINS[i]) == HIGH) b |= (1u << i);
  }
  if (g.invData) b = ~b;
  return b;
}

void applyModeToHardware() {
  const bool readerMode = (g.mode == Mode::Reader);

  if (readerMode) {
    setLvcEnabled(false);
    for (int i = 0; i < 8; i++) {
      pinMode(DATA_PINS[i], OUTPUT);
      digitalWrite(DATA_PINS[i], LOW);
    }
    setHctEnabled(true);
  } else {
    setHctEnabled(false);
    for (int i = 0; i < 8; i++) {
      pinMode(DATA_PINS[i], INPUT);
    }
    setLvcEnabled(true);
  }

  setFlag(false);
}

// ---------------- CMD interrupt ----------------
void IRAM_ATTR onCmdEdge() {
  cmdSeen = true;
}

void attachCmdInterrupt() {
  detachInterrupt(digitalPinToInterrupt(PIN_HP_CMD_IN));
  if (g.cmdEdge == CmdEdge::Rising) {
    attachInterrupt(digitalPinToInterrupt(PIN_HP_CMD_IN), onCmdEdge, RISING);
  } else {
    attachInterrupt(digitalPinToInterrupt(PIN_HP_CMD_IN), onCmdEdge, FALLING);
  }
}

// ---------------- SD: Reader file scanning ----------------
void scanRootFiles() {
  fileCount = 0;
  SdFile dir;
  SdFile entry;

  if (!dir.open("/")) return;

  while (entry.openNext(&dir, O_RDONLY)) {
    if (entry.isFile() && fileCount < MAX_FILES) {
      char fname[64];
      entry.getName(fname, sizeof(fname));
      files[fileCount++] = String(fname);
    }
    entry.close();
  }
  dir.close();

  if (fileCount == 0) {
    selectedIndex = 0;
    topIndex = 0;
  } else {
    selectedIndex = constrain(selectedIndex, 0, (int16_t)fileCount - 1);
  }
}

// ---------------- SD: Punch capture naming ----------------
void makeCapName(uint16_t idx) {
  snprintf(capName, sizeof(capName), "CAP%04u.BIN", (unsigned)idx);
}

uint16_t findNextFreeCapIndex() {
  for (uint16_t i = 0; i < 10000; i++) {
    char path[24];
    snprintf(path, sizeof(path), "/CAP%04u.BIN", (unsigned)i);
    if (!sd.exists(path)) return i;
  }
  return 9999;
}

bool openCapFile(uint16_t idx) {
  if (ioFile.isOpen()) ioFile.close();

  makeCapName(idx);
  char path[24];
  snprintf(path, sizeof(path), "/%s", capName);

  return ioFile.open(path, O_CREAT | O_WRITE);
}

bool startPunchSessionIfNeeded() {
  if (ioFile.isOpen()) return true;
  capIndex = findNextFreeCapIndex();
  punchBytesSinceSync = 0;
  return openCapFile(capIndex);
}

bool rotateToNewCapFile() {
  if (ioFile.isOpen()) {
    ioFile.sync();
    ioFile.close();
  }
  capIndex = findNextFreeCapIndex();
  punchBytesSinceSync = 0;
  return openCapFile(capIndex);
}

// ---------------- USB MSC callbacks ----------------
bool mscWrite(const uint8_t *buf, uint32_t startSector, uint16_t numSectors) {
  return sd.card()->writeBlocks(startSector, buf, numSectors);
}

bool mscRead(uint8_t *buf, uint32_t startSector, uint16_t numSectors) {
  return sd.card()->readBlocks(startSector, buf, numSectors);
}

bool startUsbMsc() {
  if (!sdOk) return false;
  if (usbStarted) return true;

  // Attach pull-up before starting USB stack so host sees attach
  usbAttachPullup();

  g_sectorCount = sd.card()->cardSize();
  if (g_sectorCount == 0) return false;

  USBComposite.clear();
  MassStorage.setDriveData(0, g_sectorCount, mscRead, mscWrite);
  MassStorage.registerComponent();

  if (!USBComposite.begin()) {
    return false;
  }

  usbStarted = true;
  usbHostConfigured = false;
  return true;
}

void stopUsb() {
  if (!usbStarted) {
    // Even if stack isn't started, ensure we are detached
    usbDetachPullup();
    return;
  }

  USBComposite.end();
  usbStarted = false;
  usbHostConfigured = false;

  // Physically detach (host sees disconnect)
  usbDetachPullup();
}

// ---------------- Safe stop emulation ----------------
void stopEmulationNow() {
  armed = false;
  cmdSeen = false;

  if (ioFile.isOpen()) {
    ioFile.sync();
    ioFile.close();
  }

  // Safe HP bus state
  setHctEnabled(false);
  setFlag(false);
  for (int i = 0; i < 8; i++) {
    pinMode(DATA_PINS[i], INPUT);
  }
}

// ---------------- OLED screens ----------------
String modeName() {
  return (g.mode == Mode::Reader) ? "Reader" : "Punch";
}

void drawUsbScreen(const String &status) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawStr(0, 12, "USB Disk Mode (MSC)");
  u8g2.drawHLine(0, 14, 128);
  u8g2.drawStr(0, 28, status.c_str());
  u8g2.drawStr(0, 44, "Host connected");
  u8g2.drawStr(0, 58, "Hold Sel: settings");
  u8g2.sendBuffer();
}

void drawNoSdScreen() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawStr(0, 12, "Paper Tape Emulator");
  u8g2.drawHLine(0, 14, 128);
  u8g2.drawStr(0, 32, "Insert SD card");
  u8g2.drawStr(0, 50, "USB MSC disabled");
  u8g2.sendBuffer();
}

void drawReaderBrowse(const String &status) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tf);

  String title = "Mode: " + modeName();
  u8g2.drawStr(0, 12, title.c_str());
  u8g2.drawHLine(0, 14, 128);
  u8g2.drawStr(0, 28, status.c_str());

  const int listY0 = 44;
  const int lineH = 12;
  const int visible = 2;

  if (!sdOk) {
    u8g2.drawStr(0, listY0, "SD init failed");
  } else if (fileCount == 0) {
    u8g2.drawStr(0, listY0, "No files on SD");
  } else {
    if (selectedIndex < topIndex) topIndex = selectedIndex;
    if (selectedIndex >= topIndex + visible) topIndex = selectedIndex - visible + 1;

    for (int i = 0; i < visible; i++) {
      int idx = topIndex + i;
      if (idx >= (int)fileCount) break;

      int y = listY0 + i * lineH;
      String line = files[idx];

      if (idx == selectedIndex) {
        u8g2.drawBox(0, y - 10, 128, 12);
        u8g2.setDrawColor(0);
        u8g2.drawStr(2, y, line.c_str());
        u8g2.setDrawColor(1);
      } else {
        u8g2.drawStr(2, y, line.c_str());
      }
    }
  }

  u8g2.sendBuffer();
}

void drawPunchScreen(const String &status) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tf);

  String title = "Mode: " + modeName();
  u8g2.drawStr(0, 12, title.c_str());
  u8g2.drawHLine(0, 14, 128);
  u8g2.drawStr(0, 28, status.c_str());

  const int y1 = 46;
  const int y2 = 58;

  if (punchCursor == 0) {
    u8g2.drawBox(0, y1 - 10, 128, 12);
    u8g2.setDrawColor(0);
    u8g2.drawStr(2, y1, capName);
    u8g2.setDrawColor(1);
  } else {
    u8g2.drawStr(2, y1, capName);
  }

  if (punchCursor == 1) {
    u8g2.drawBox(0, y2 - 10, 128, 12);
    u8g2.setDrawColor(0);
    u8g2.drawStr(2, y2, "New file?");
    u8g2.setDrawColor(1);
  } else {
    u8g2.drawStr(2, y2, "New file?");
  }

  u8g2.sendBuffer();
}

void redrawMainScreen(const String &status) {
  if (!sdInserted) {
    drawNoSdScreen();
    return;
  }
  if (sysState == SysState::USB_MSC_ACTIVE) {
    drawUsbScreen(status);
    return;
  }
  if (g.mode == Mode::Reader) drawReaderBrowse(status);
  else drawPunchScreen(status);
}

// ---------------- Settings menu (dynamic) ----------------
int settingsItemCount() {
  // base: mode, inv_data, inv_cmd, inv_flag, cmd_edge, led => 6
  // + host toggle: start/stop emu (only if host configured) => +1
  return usbHostConfigured ? 7 : 6;
}

String usbToggleItemText() {
  if (!usbHostConfigured) return "";
  if (sysState == SysState::USB_MSC_ACTIVE) return "Start emulation";
  return "Stop emulation";
}

void drawSettingsMenu() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tf);

  u8g2.drawStr(0, 12, "Settings (Sel=toggle)");
  u8g2.drawHLine(0, 14, 128);

  String edgeName = (g.cmdEdge == CmdEdge::Rising) ? "Rising" : "Falling";

  const int n = settingsItemCount();
  String items[7];
  int idx = 0;

  if (usbHostConfigured) {
    items[idx++] = usbToggleItemText();
  }

  items[idx++] = "Mode: " + modeName();
  items[idx++] = String("Invert DATA: ") + (g.invData ? "Yes" : "No");
  items[idx++] = String("Invert CMD: ")  + (g.invCmd  ? "Yes" : "No");
  items[idx++] = String("Invert FLAG: ") + (g.invFlag ? "Yes" : "No");
  items[idx++] = String("CMD edge: ") + edgeName;
  items[idx++] = String("LED: ") + (g.ledEnable ? "On" : "Off");

  for (int i = 0; i < n; i++) {
    int y = 28 + i * 10;
    if (i == menuIndex) {
      u8g2.drawBox(0, y - 8, 128, 10);
      u8g2.setDrawColor(0);
      u8g2.drawStr(2, y, items[i].c_str());
      u8g2.setDrawColor(1);
    } else {
      u8g2.drawStr(2, y, items[i].c_str());
    }
  }

  u8g2.sendBuffer();
}

// ---------------- Settings actions ----------------
void startEmulationWhileHost() {
  // Detach USB MSC completely (so SD filesystem is safe again)
  stopUsb();
  stopEmulationNow();

  // Apply mode electrical config
  applyModeToHardware();
  attachCmdInterrupt();

  // SD filesystem safe now
  scanRootFiles();
  capIndex = findNextFreeCapIndex();
  makeCapName(capIndex);
  punchCursor = 0;

  sysState = SysState::EMU_FORCED_WHILE_HOST;
  redrawMainScreen("Emulation started");
}

void stopEmulationAndEnableUsb() {
  stopEmulationNow();

  // Re-enable MSC (host can remount)
  if (startUsbMsc()) {
    sysState = SysState::USB_MSC_ACTIVE;
    redrawMainScreen("USB disk active");
  } else {
    // fallback
    sysState = SysState::EMU_RUNNING;
    applyModeToHardware();
    attachCmdInterrupt();
    redrawMainScreen("USB start failed");
  }
}

void menuToggleCurrentItem() {
  const int n = settingsItemCount();
  if (menuIndex < 0) menuIndex = 0;
  if (menuIndex >= n) menuIndex = n - 1;

  int offset = 0;

  if (usbHostConfigured) {
    if (menuIndex == 0) {
      if (sysState == SysState::USB_MSC_ACTIVE) startEmulationWhileHost();
      else stopEmulationAndEnableUsb();
      drawSettingsMenu();
      return;
    }
    offset = 1;
  }

  const int item = menuIndex - offset;

  switch (item) {
    case 0: g.mode = (g.mode == Mode::Reader) ? Mode::Punch : Mode::Reader; break;
    case 1: g.invData = !g.invData; break;
    case 2: g.invCmd  = !g.invCmd;  break;
    case 3: g.invFlag = !g.invFlag; break;
    case 4: g.cmdEdge = (g.cmdEdge == CmdEdge::Rising) ? CmdEdge::Falling : CmdEdge::Rising; break;
    case 5: g.ledEnable = !g.ledEnable; break;
    default: break;
  }

  // Only write config when not in MSC active (host mounted)
  if (sdOk && sysState != SysState::USB_MSC_ACTIVE) {
    saveConfigToSd();
  }

  // Apply immediately if not in MSC active
  if (sysState != SysState::USB_MSC_ACTIVE) {
    applyModeToHardware();
    attachCmdInterrupt();
  }

  drawSettingsMenu();
}

void toggleMenuEnterExit() {
  if (uiState == UiState::Main) {
    uiState = UiState::SettingsMenu;
    menuIndex = 0;
    drawSettingsMenu();
  } else {
    uiState = UiState::Main;
    redrawMainScreen("OK");
  }
}

// ---------------- USB host detection + auto-switch ----------------
void usbPollAndAutoSwitch() {
  uint32_t now = millis();
  if (now - lastUsbPollMs < 50) return;
  lastUsbPollMs = now;

  if (!usbStarted) return;

  bool configuredNow = (bool)USBComposite;

  if (configuredNow && !usbHostConfigured) {
    usbHostConfigured = true;

    // If host enumerated, immediately stop emulation and go MSC mode
    stopEmulationNow();
    sysState = SysState::USB_MSC_ACTIVE;
    redrawMainScreen("USB host detected");
  }

  if (!configuredNow && usbHostConfigured) {
    usbHostConfigured = false;

    // If we were in USB mode, return to emulation automatically
    if (sysState == SysState::USB_MSC_ACTIVE) {
      sysState = SysState::EMU_RUNNING;

      applyModeToHardware();
      attachCmdInterrupt();

      // SD filesystem safe again
      scanRootFiles();
      capIndex = findNextFreeCapIndex();
      makeCapName(capIndex);
      punchCursor = 0;

      redrawMainScreen("Host disconnected");
    }
  }
}

// ---------------- SD insertion polling ----------------
void handleSdInsertionRemoval() {
  uint32_t now = millis();
  if (now - lastSdPollMs < 100) return;
  lastSdPollMs = now;

  bool nowInserted = readSdInserted();
  if (nowInserted == sdInserted) return;

  sdInserted = nowInserted;

  if (!sdInserted) {
    // SD removed: stop everything safely
    stopEmulationNow();

    // Stop USB MSC (detach pull-up too)
    stopUsb();

    sdOk = false;
    sysState = SysState::EMU_RUNNING;
    redrawMainScreen("");
    return;
  }

  // SD inserted:
  // Only safe to init SD and load config if we are NOT in MSC active mode.
  // Since SD was removed, MSC is already stopped above. Good.
  sdOk = sd.begin(PIN_SD_CS, SD_SCK_MHZ(18));
  if (!sdOk) {
    redrawMainScreen("SD init failed");
    return;
  }

  loadConfigFromSd();

  // Apply mode/hardware per config
  applyModeToHardware();
  attachCmdInterrupt();

  scanRootFiles();
  capIndex = findNextFreeCapIndex();
  makeCapName(capIndex);
  punchCursor = 0;

  // Start MSC again (it will only enumerate if host is present)
  startUsbMsc();

  redrawMainScreen("SD inserted");
}

// ---------------- Setup ----------------
void setup() {
  delay(50);

  disableJtagKeepSwd();

  // LED
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH); // OFF for PC13 active-low
  ledState = false;

  // USB pull-up enable pin
  pinMode(PIN_USB_PULLUP_EN, OUTPUT);
  // Default detach until we explicitly start MSC
  usbDetachPullup();

  // SD detect pin
  pinMode(PIN_SD_DETECT, INPUT_PULLUP);

  // Buttons
  pinMode(PIN_BTN_UP, INPUT_PULLUP);
  pinMode(PIN_BTN_DOWN, INPUT_PULLUP);
  pinMode(PIN_BTN_SELECT, INPUT_PULLUP);

  // Buffer enables
  pinMode(PIN_OE_HCT_OUT, OUTPUT);
  pinMode(PIN_OE_LVC_IN, OUTPUT);

  // Handshake pins
  pinMode(PIN_HP_CMD_IN, INPUT);
  pinMode(PIN_HP_FLAG_OUT, OUTPUT);
  setFlag(false);

  // OLED
  Wire.begin();
  u8g2.begin();

  // Determine SD state at boot
  sdInserted = readSdInserted();

  if (!sdInserted) {
    sdOk = false;
    // Keep USB detached
    stopUsb();
    // Put bus safe
    stopEmulationNow();
    drawNoSdScreen();
    return;
  }

  // Init SD
  sdOk = sd.begin(PIN_SD_CS, SD_SCK_MHZ(18));
  if (sdOk) {
    loadConfigFromSd();
    scanRootFiles();
    capIndex = findNextFreeCapIndex();
    makeCapName(capIndex);
  }

  applyModeToHardware();
  attachCmdInterrupt();

  // Start MSC (only if SD OK)
  if (sdOk) {
    startUsbMsc();
  }

  sysState = SysState::EMU_RUNNING;
  redrawMainScreen(sdOk ? "Ready" : "SD init FAILED");
}

// ---------------- Loop ----------------
void loop() {
  // Handle SD insert/remove
  handleSdInsertionRemoval();

  // If no SD inserted, nothing else to do
  if (!sdInserted) {
    ledUpdatePattern();
    return;
  }

  // USB MSC service
  if (usbStarted) {
    MassStorage.loop();
  }

  // Detect host enumeration and auto-switch
  usbPollAndAutoSwitch();

  // LED pattern
  ledUpdatePattern();

  // Long-press SELECT (>4s) toggles settings menu
  uint32_t now = millis();
  bool selDown = btnPressed(PIN_BTN_SELECT);

  if (selDown && !selWasDown) {
    selDownAt = now;
  } else if (selDown && selWasDown) {
    if (now - selDownAt >= 4000) {
      toggleMenuEnterExit();
      selDownAt = now + 999999UL;
    }
  }
  selWasDown = selDown;

  // UI tick
  if (now - lastUiTickMs >= DEBOUNCE_MS) {
    lastUiTickMs = now;

    if (uiState == UiState::SettingsMenu) {
      int n = settingsItemCount();

      if (btnPressed(PIN_BTN_UP)) {
        menuIndex = (menuIndex - 1 + n) % n;
        drawSettingsMenu();
      } else if (btnPressed(PIN_BTN_DOWN)) {
        menuIndex = (menuIndex + 1) % n;
        drawSettingsMenu();
      } else if (btnPressed(PIN_BTN_SELECT)) {
        menuToggleCurrentItem();
      }
    } else {
      // In USB MSC mode, user interacts only via Settings -> Start emulation
      if (sysState == SysState::USB_MSC_ACTIVE) {
        // no-op
      } else {
        // Normal emulation UI (requires SD filesystem; USB is detached in EMU_FORCED_WHILE_HOST)
        if (g.mode == Mode::Reader) {
          if (!armed) {
            if (btnPressed(PIN_BTN_UP) && fileCount > 0) {
              selectedIndex = (selectedIndex - 1 + fileCount) % fileCount;
              redrawMainScreen("Browse");
            } else if (btnPressed(PIN_BTN_DOWN) && fileCount > 0) {
              selectedIndex = (selectedIndex + 1) % fileCount;
              redrawMainScreen("Browse");
            } else if (btnPressed(PIN_BTN_SELECT)) {
              if (!sdOk || fileCount == 0) {
                redrawMainScreen("No files");
              } else {
                if (ioFile.isOpen()) ioFile.close();
                String path = "/" + files[selectedIndex];

                if (ioFile.open(path.c_str(), O_RDONLY)) {
                  armed = true;
                  redrawMainScreen("ARMED");
                } else {
                  redrawMainScreen("Open failed");
                }
              }
            }
          } else {
            if (btnPressed(PIN_BTN_SELECT)) {
              armed = false;
              if (ioFile.isOpen()) ioFile.close();
              redrawMainScreen("ABORTED");
            }
          }
        } else {
          // Punch UI
          if (!armed) {
            if (btnPressed(PIN_BTN_UP) || btnPressed(PIN_BTN_DOWN)) {
              punchCursor = 1 - punchCursor;
              redrawMainScreen("Sel=arm");
            } else if (btnPressed(PIN_BTN_SELECT)) {
              capIndex = findNextFreeCapIndex();
              makeCapName(capIndex);

              if (startPunchSessionIfNeeded()) {
                armed = true;
                redrawMainScreen("ARMED");
              } else {
                redrawMainScreen("Open CAP failed");
              }
            }
          } else {
            if (btnPressed(PIN_BTN_UP) || btnPressed(PIN_BTN_DOWN)) {
              punchCursor = 1 - punchCursor;
              redrawMainScreen("Capturing");
            } else if (btnPressed(PIN_BTN_SELECT)) {
              if (punchCursor == 1) {
                if (rotateToNewCapFile()) {
                  redrawMainScreen("New file");
                } else {
                  redrawMainScreen("New file FAIL");
                }
              } else {
                armed = false;
                if (ioFile.isOpen()) {
                  ioFile.sync();
                  ioFile.close();
                }
                capIndex = findNextFreeCapIndex();
                makeCapName(capIndex);
                redrawMainScreen("STOPPED");
              }
            }
          }
        }
      }
    }
  }

  // Transfer engine (only when emulation allowed)
  if ((sysState == SysState::EMU_RUNNING || sysState == SysState::EMU_FORCED_WHILE_HOST) && armed && cmdSeen) {
    cmdSeen = false;

    bool cmdActive = (digitalRead(PIN_HP_CMD_IN) == HIGH);
    if (g.invCmd) cmdActive = !cmdActive;
    if (!cmdActive) return;

    ledPulseShort();

    if (g.mode == Mode::Reader) {
      int c = ioFile.read();
      if (c < 0) {
        armed = false;
        ioFile.close();
        redrawMainScreen("DONE");
        return;
      }
      writeDataBus((uint8_t)c);
      pulseFlag();
    } else {
      uint8_t b = readDataBus();
      ioFile.write(&b, 1);
      punchBytesSinceSync++;

      if ((punchBytesSinceSync & 0x1FF) == 0) {
        ioFile.sync();
      }

      pulseFlag();
    }
  }
}
