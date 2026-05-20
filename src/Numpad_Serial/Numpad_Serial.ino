/********************************************************************
 *  ESP32 RF-TRANSMITTER — 4x4 Numpad + Serial control
 *  ---------------------------------------------------------------
 *  No BLE, WiFi, or any radio — lowest power / no overheating.
 *
 *  INPUT 1: 4×4 matrix keypad wired directly to 8 GPIO pins
 *  INPUT 2: Serial port commands (Python script or any terminal)
 *
 *  Keypad layout:
 *    [1][2][3][ ]    1=ch1 lvl-   2=ch2 lvl-   3=test ch2
 *    [4][5][6][ ]    4=ch1 lvl+   5=ch2 lvl+   6=test ch1
 *    [7][8][9][ ]    7=shock ch1  8=shock ch2  9=shock both
 *    [*][0][#][ ]    *=both lvl-  0=both lvl+  #=reset
 *
 *  Serial commands (115200 baud, newline-terminated):
 *    7 8 9 4 5 1 2 6 3 0 + -   same single-char shortcuts
 *    SHOCK1 [N]   shock ch1 at intensity N (or current level)
 *    SHOCK2 [N]   shock ch2 at intensity N
 *    SHOCKB [N]   shock both at intensity N
 *    LEVEL1 N     set ch1 level (1-99)
 *    LEVEL2 N     set ch2 level (1-99)
 *    STATUS       print current state
 *
 *  RF wiring (módulo OOK de un solo pin):
 *    DATA → GPIO26
 *    VCC  → 5V (VIN)
 *    GND  → GND
 *
 *  Keypad wiring (ribbon pin order):
 *    Keypad pin 0 → GPIO13  (Row 0)
 *    Keypad pin 1 → GPIO14  (Row 1)
 *    Keypad pin 2 → GPIO27  (Row 2)
 *    Keypad pin 3 → GPIO25  (Row 3)
 *    Keypad pin 4 → GPIO19  (Col 0)
 *    Keypad pin 5 → GPIO5   (Col 1)
 *    Keypad pin 6 → GPIO18  (Col 2)
 *    Keypad pin 7 → GPIO17  (Col 3)
 *
 *  Board: ESP32 Dev Module (38-pin)
 *  Baud:  115200
 ********************************************************************/

#include <Arduino.h>

// ── RF pins ───────────────────────────────────────────────────────
const uint8_t DATA_PIN = 26;

// ── Collar configuration ──────────────────────────────────────────
#define TRANSMITTER_ID  0xB497
#define ACTION_SHOCK    1
#define ACTION_VIBRATE  2
#define ACTION_BEEP     3

// ── Keypad wiring ─────────────────────────────────────────────────
// Keypad ribbon pin order: 0=R0 1=R1 2=R2 3=R3 4=C0 5=C1 6=C2 7=C3
const uint8_t ROW_PINS[4] = {13, 14, 27, 25};   // keypad pins 0-3
const uint8_t COL_PINS[4] = {19,  5, 18, 17};   // keypad pins 4-7

// Transposed: physical rows/cols are swapped relative to ROW_PINS/COL_PINS
const char KEYMAP[4][4] = {
  {'1', '4', '7', '*'},
  {'2', '5', '8', '0'},
  {'3', '6', '9', '#'},
  {'A', 'B', 'C', 'D'},
};

// ── State ─────────────────────────────────────────────────────────
static uint8_t levelCh1 = 1;
static uint8_t levelCh2 = 1;

// ── Helpers ───────────────────────────────────────────────────────
static uint8_t clampLevel(int v) {
  if (v < 1)  return 1;
  if (v > 99) return 99;
  return (uint8_t)v;
}

// ── RF encoder ───────────────────────────────────────────────────
void sendStimulus(uint16_t txId, uint8_t channel, uint8_t action,
                  uint8_t intensity, uint8_t repeats = 10) {
  if (channel > 2)              channel   = 2;
  if (action < 1 || action > 3) action    = ACTION_SHOCK;
  if (intensity > 99)           intensity = 99;
  if (action == ACTION_BEEP)    intensity = 0;

  uint64_t payload = ((uint64_t)txId      << 24) |
                     ((uint64_t)channel   << 20) |
                     ((uint64_t)action    << 16) |
                     ((uint64_t)intensity <<  8);

  uint8_t checksum = 0;
  for (uint8_t i = 0; i < 5; i++)
    checksum += (uint8_t)((payload >> (i * 8)) & 0xFF);

  uint64_t packet = payload | (uint64_t)checksum;

  const char* actionName = (action == ACTION_SHOCK)   ? "SHOCK"
                         : (action == ACTION_VIBRATE) ? "VIBRATE"
                                                      : "BEEP";
  Serial.printf("[RF] ch=%d  %s  intensity=%d\n", channel, actionName, intensity);

  for (uint8_t r = 0; r < repeats; r++) {
    digitalWrite(DATA_PIN, HIGH); delayMicroseconds(1400);
    digitalWrite(DATA_PIN, LOW);  delayMicroseconds(750);
    for (int8_t b = 39; b >= 0; b--) {
      if ((packet >> b) & 1ULL) {
        digitalWrite(DATA_PIN, HIGH); delayMicroseconds(750);
        digitalWrite(DATA_PIN, LOW);  delayMicroseconds(250);
      } else {
        digitalWrite(DATA_PIN, HIGH); delayMicroseconds(250);
        digitalWrite(DATA_PIN, LOW);  delayMicroseconds(750);
      }
    }
    // End marker: two 0-bits
    digitalWrite(DATA_PIN, HIGH); delayMicroseconds(250);
    digitalWrite(DATA_PIN, LOW);  delayMicroseconds(750);
    digitalWrite(DATA_PIN, HIGH); delayMicroseconds(250);
    digitalWrite(DATA_PIN, LOW);  delayMicroseconds(750);
  }
  digitalWrite(DATA_PIN, LOW);
  Serial.println("[RF] done");
}

// ── Key action dispatcher ─────────────────────────────────────────
void handleKey(char key) {
  switch (key) {
    case '7':
      Serial.printf("[KEY] 7 → shock Ch1 level=%d\n", levelCh1);
      sendStimulus(TRANSMITTER_ID, 0, ACTION_SHOCK, levelCh1);
      break;
    case '8':
      Serial.printf("[KEY] 8 → shock Ch2 level=%d\n", levelCh2);
      sendStimulus(TRANSMITTER_ID, 1, ACTION_SHOCK, levelCh2);
      break;
    case '9':
      Serial.printf("[KEY] 9 → shock BOTH Ch1=%d Ch2=%d\n", levelCh1, levelCh2);
      sendStimulus(TRANSMITTER_ID, 0, ACTION_SHOCK, levelCh1);
      sendStimulus(TRANSMITTER_ID, 1, ACTION_SHOCK, levelCh2);
      break;
    case '4':
      levelCh1 = clampLevel(levelCh1 + 1);
      Serial.printf("[KEY] 4 → Ch1 level=%d\n", levelCh1);
      break;
    case '5':
      levelCh2 = clampLevel(levelCh2 + 1);
      Serial.printf("[KEY] 5 → Ch2 level=%d\n", levelCh2);
      break;
    case '0': case '+':
      levelCh1 = clampLevel(levelCh1 + 1);
      levelCh2 = clampLevel(levelCh2 + 1);
      Serial.printf("[KEY] 0 → both+ Ch1=%d Ch2=%d\n", levelCh1, levelCh2);
      break;
    case '1':
      levelCh1 = clampLevel(levelCh1 - 1);
      Serial.printf("[KEY] 1 → Ch1 level=%d\n", levelCh1);
      break;
    case '2':
      levelCh2 = clampLevel(levelCh2 - 1);
      Serial.printf("[KEY] 2 → Ch2 level=%d\n", levelCh2);
      break;
    case '*': case '-':
      levelCh1 = clampLevel(levelCh1 - 1);
      levelCh2 = clampLevel(levelCh2 - 1);
      Serial.printf("[KEY] * → both- Ch1=%d Ch2=%d\n", levelCh1, levelCh2);
      break;
    case '6':
      Serial.printf("[KEY] 6 → test Ch1 level=%d\n", levelCh1);
      sendStimulus(TRANSMITTER_ID, 0, ACTION_VIBRATE, levelCh1);
      sendStimulus(TRANSMITTER_ID, 0, ACTION_BEEP, 0);
      break;
    case '3':
      Serial.printf("[KEY] 3 → test Ch2 level=%d\n", levelCh2);
      sendStimulus(TRANSMITTER_ID, 1, ACTION_VIBRATE, levelCh2);
      sendStimulus(TRANSMITTER_ID, 1, ACTION_BEEP, 0);
      break;
    case '#':
      levelCh1 = levelCh2 = 1;
      Serial.println("[KEY] # → reset Ch1=Ch2=1");
      break;
    default:
      Serial.printf("[KEY] unknown '%c'\n", key);
      break;
  }
}

// ── Serial command parser ─────────────────────────────────────────
static String serialBuf;

void handleSerialCommand(const String& raw) {
  String s = raw;
  s.trim();
  if (s.length() == 0) return;

  // Single-char shortcut
  if (s.length() == 1) {
    handleKey(s[0]);
    return;
  }

  String upper = s;
  upper.toUpperCase();

  if (upper == "STATUS") {
    Serial.printf("[STATUS] Ch1=%d  Ch2=%d  txId=0x%04X\n",
                  levelCh1, levelCh2, TRANSMITTER_ID);
    return;
  }

  // SHOCK1 [N], SHOCK2 [N], SHOCKB [N]
  if (upper.startsWith("SHOCK") && upper.length() >= 6) {
    char target = upper[5];
    int intensity = -1;
    if (upper.length() > 7) {
      String numPart = upper.substring(6);
      numPart.trim();
      int v = numPart.toInt();
      if (v >= 1 && v <= 99) intensity = v;
    }
    if (target == '1') {
      uint8_t lvl = (intensity > 0) ? (uint8_t)intensity : levelCh1;
      Serial.printf("[SER] shock Ch1 intensity=%d\n", lvl);
      sendStimulus(TRANSMITTER_ID, 0, ACTION_SHOCK, lvl);
    } else if (target == '2') {
      uint8_t lvl = (intensity > 0) ? (uint8_t)intensity : levelCh2;
      Serial.printf("[SER] shock Ch2 intensity=%d\n", lvl);
      sendStimulus(TRANSMITTER_ID, 1, ACTION_SHOCK, lvl);
    } else if (target == 'B') {
      uint8_t l1 = (intensity > 0) ? (uint8_t)intensity : levelCh1;
      uint8_t l2 = (intensity > 0) ? (uint8_t)intensity : levelCh2;
      Serial.printf("[SER] shock BOTH Ch1=%d Ch2=%d\n", l1, l2);
      sendStimulus(TRANSMITTER_ID, 0, ACTION_SHOCK, l1);
      sendStimulus(TRANSMITTER_ID, 1, ACTION_SHOCK, l2);
    } else {
      Serial.println("[SER] usage: SHOCK1/SHOCK2/SHOCKB [intensity]");
    }
    return;
  }

  // LEVEL1 N, LEVEL2 N
  if (upper.startsWith("LEVEL") && upper.length() >= 7) {
    char target = upper[5];
    String numPart = upper.substring(6);
    numPart.trim();
    int val = numPart.toInt();
    if (val >= 1 && val <= 99) {
      if (target == '1') {
        levelCh1 = (uint8_t)val;
        Serial.printf("[SER] Ch1 level=%d\n", levelCh1);
      } else if (target == '2') {
        levelCh2 = (uint8_t)val;
        Serial.printf("[SER] Ch2 level=%d\n", levelCh2);
      } else {
        Serial.println("[SER] usage: LEVEL1/LEVEL2 N");
      }
    } else {
      Serial.printf("[SER] invalid level %d (1-99)\n", val);
    }
    return;
  }

  Serial.printf("[SER] unknown: %s\n", s.c_str());
}

// ── Keypad scanner ────────────────────────────────────────────────
static char          lastKey   = 0;
static unsigned long lastKeyMs = 0;
#define DEBOUNCE_MS 200

char scanKeypad() {
  for (uint8_t r = 0; r < 4; r++) {
    digitalWrite(ROW_PINS[r], LOW);
    delayMicroseconds(10);  // settling time
    for (uint8_t c = 0; c < 4; c++) {
      if (digitalRead(COL_PINS[c]) == LOW) {
        digitalWrite(ROW_PINS[r], HIGH);
        return KEYMAP[r][c];
      }
    }
    digitalWrite(ROW_PINS[r], HIGH);
  }
  return 0;
}

// ── Setup ─────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println("\n========================================");
  Serial.println("  ESP32 Numpad+Serial → CaiXianlin Shocker");
  Serial.printf ("  TX ID:      0x%04X\n", TRANSMITTER_ID);
  Serial.printf ("  DATA:       GPIO%d\n", DATA_PIN);
  Serial.println("  Rows (kp 0-3): GPIO 13,14,27,25");
  Serial.println("  Cols (kp 4-7): GPIO 19,18,5,17");
  Serial.println("  7=shock1  8=shock2  9=both  4/5=lvl+  1/2=lvl-");
  Serial.println("  6=test1  3=test2  0=both+  *=both-  #=reset");
  Serial.println("  Serial: SHOCK1/SHOCK2/SHOCKB [N]  LEVEL1/LEVEL2 N  STATUS");
  Serial.println("========================================\n");

  pinMode(DATA_PIN, OUTPUT); digitalWrite(DATA_PIN, LOW);

  for (uint8_t r = 0; r < 4; r++) {
    pinMode(ROW_PINS[r], OUTPUT);
    digitalWrite(ROW_PINS[r], HIGH);
  }
  for (uint8_t c = 0; c < 4; c++) {
    pinMode(COL_PINS[c], INPUT_PULLUP);
  }

  Serial.printf("[SETUP] Ready  Ch1=%d  Ch2=%d\n", levelCh1, levelCh2);
}

// ── Loop ──────────────────────────────────────────────────────────
void loop() {
  // Keypad
  char key = scanKeypad();
  unsigned long now = millis();

  if (key && key != lastKey && (now - lastKeyMs) >= DEBOUNCE_MS) {
    lastKey   = key;
    lastKeyMs = now;
    handleKey(key);
  } else if (!key) {
    lastKey = 0;
  }

  // Serial
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialBuf.length() > 0) {
        handleSerialCommand(serialBuf);
        serialBuf = "";
      }
    } else {
      serialBuf += c;
    }
  }
}
