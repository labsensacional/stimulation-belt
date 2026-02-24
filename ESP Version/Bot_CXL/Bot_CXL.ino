/********************************************************************
 *  ESP8266 RF-TRANSMITTER + TELEGRAM BOT  –  CaiXianlin Edition
 *  ---------------------------------------------------------------
 *  Encodes packets on-the-fly using the CaiXianlin OOK protocol.
 *  No hardcoded waveforms – any intensity 1-99 on any channel.
 *
 *  Board:  NodeMCU 1.0 (ESP-12E module)  80 MHz / 4 M SPIFFS
 *
 *  Before flashing:
 *    • Set TRANSMITTER_ID to the 16-bit ID your collars are paired
 *      to (reverse-engineer from a .sub file or re-pair the collars
 *      using this ID).
 *    • /channel1 targets protocol channel 0  (collar paired to ch1)
 *      /channel2 targets protocol channel 1  (collar paired to ch2)
 *
 *  Commands:
 *    /start          – show this help
 *    level N         – set default shock intensity to N  (1–99)
 *    /channel1       – shock on channel 1 at current intensity
 *    /channel2       – shock on channel 2 at current intensity
 *    /test1          – vibrate + beep on channel 1
 *    /test2          – vibrate + beep on channel 2
 *
 *  Dependencies:
 *    • ESP8266WiFi          (built-in)
 *    • WiFiClientSecure     (built-in)
 *    • ESP8266WebServer     (built-in)
 *    • UniversalTelegramBot (github.com/witnessmenow/Universal-Arduino-Telegram-Bot)
 ********************************************************************/

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>

// ── Pin assignments ──────────────────────────────────────
const uint8_t DATA_PIN   = 5;   // D1
const uint8_t ENABLE_PIN = 4;   // D2

// ── Telegram bot token ───────────────────────────────────
const char* BOT_TOKEN = "COMPLETAR";   // keep private!

// ── Collar configuration ─────────────────────────────────
// 16-bit transmitter ID the collars are paired to (0–65535).
#define TRANSMITTER_ID  0xB497

// ── WiFi credential store (EEPROM) ───────────────────────
#define MAX_CREDENTIALS  5
#define EEPROM_SIZE      512

struct WiFiCred {
  char ssid[32];
  char pass[64];
};

WiFiCred creds[MAX_CREDENTIALS];
uint8_t  credCount = 0;

ESP8266WebServer server(80);

// ═══════════════════════════════════════════════════════════
// CaiXianlin OOK Protocol Encoder
// ═══════════════════════════════════════════════════════════
//
// Packet (40 bits, MSB first):
//   [39:24]  Transmitter ID  16 bits  (0–65535)
//   [23:20]  Channel          4 bits  (0–2)
//   [19:16]  Action           4 bits  (1=Shock, 2=Vibrate, 3=Beep)
//   [15:8]   Intensity        8 bits  (0–99; always 0 for Beep)
//   [7:0]    Checksum         8 bits  (sum of all bytes, mod 256)
//
// Bit timing (ASK/OOK @ 433.95 MHz):
//   Sync:  HIGH 1400 µs  /  LOW  750 µs
//   Bit 1: HIGH  750 µs  /  LOW  250 µs
//   Bit 0: HIGH  250 µs  /  LOW  750 µs
//
// Each packet ends with two 0-bits and is repeated `repeats` times.

#define ACTION_SHOCK    1
#define ACTION_VIBRATE  2
#define ACTION_BEEP     3

/**
 * sendStimulus — encode and transmit a CaiXianlin command.
 *
 * @param txId      16-bit transmitter ID the collar is paired to
 * @param channel   Collar channel: 0, 1, or 2
 * @param action    ACTION_SHOCK, ACTION_VIBRATE, or ACTION_BEEP
 * @param intensity Shock / vibration level 0–99 (forced to 0 for Beep)
 * @param repeats   Packet retransmission count (default 10)
 */
void sendStimulus(uint16_t txId, uint8_t channel, uint8_t action,
                  uint8_t intensity, uint8_t repeats = 10) {

  // Clamp / validate
  if (channel   > 2) channel = 2;
  if (action < 1 || action > 3) action = ACTION_SHOCK;
  if (intensity > 99) intensity = 99;
  if (action == ACTION_BEEP) intensity = 0;

  // Build 40-bit payload (checksum field = 0 initially)
  uint64_t payload = ((uint64_t)txId      << 24) |
                     ((uint64_t)channel   << 20) |
                     ((uint64_t)action    << 16) |
                     ((uint64_t)intensity <<  8);

  // Checksum: 8-bit sum of all 5 payload bytes, mod 256
  uint8_t checksum = 0;
  for (uint8_t i = 0; i < 5; i++) {
    checksum += (uint8_t)((payload >> (i * 8)) & 0xFF);
  }

  // Complete 40-bit packet
  uint64_t packet = payload | (uint64_t)checksum;

  // Transmit
  digitalWrite(ENABLE_PIN, HIGH);

  for (uint8_t r = 0; r < repeats; r++) {
    // Sync pulse
    digitalWrite(DATA_PIN, HIGH); delayMicroseconds(1400);
    digitalWrite(DATA_PIN, LOW);  delayMicroseconds(750);

    // 40 data bits, MSB first
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
  digitalWrite(ENABLE_PIN, LOW);
}

// ═══════════════════════════════════════════════════════════
// Bot state
// ═══════════════════════════════════════════════════════════

uint8_t defaultLevel = 1;    // current default shock intensity (1–99)

// ═══════════════════════════════════════════════════════════
// WiFi helpers
// ═══════════════════════════════════════════════════════════

void loadCreds() {
  EEPROM.begin(EEPROM_SIZE);
  credCount = EEPROM.read(0);
  if (credCount > MAX_CREDENTIALS) credCount = 0;
  for (uint8_t i = 0; i < credCount; i++) {
    EEPROM.get(1 + i * sizeof(WiFiCred), creds[i]);
  }
}

void saveCred(const char* s, const char* p) {
  if (credCount >= MAX_CREDENTIALS) return;
  strncpy(creds[credCount].ssid, s, sizeof(creds[0].ssid));
  strncpy(creds[credCount].pass, p, sizeof(creds[0].pass));
  EEPROM.put(1 + credCount * sizeof(WiFiCred), creds[credCount]);
  credCount++;
  EEPROM.write(0, credCount);
  EEPROM.commit();
}

bool connectToKnown() {
  Serial.println(F("Scanning-and-connecting…"));
  for (uint8_t i = 0; i < credCount; i++) {
    Serial.printf("  trying %s … ", creds[i].ssid);
    WiFi.begin(creds[i].ssid, creds[i].pass);
    for (uint8_t j = 0; j < 30 && WiFi.status() != WL_CONNECTED; j++) delay(333);
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("OK");
      return true;
    }
    Serial.println("fail");
  }
  return false;
}

void startConfigPortal() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("RF_Bot_Config", "rfbot123");
  IPAddress ip = WiFi.softAPIP();
  Serial.printf("Config AP up – connect to RF_Bot_Config and browse to http://%s/\n",
                ip.toString().c_str());

  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html",
      "<form method='POST' action='/save'>"
      "SSID:<br><input name='s'><br>"
      "PASS:<br><input name='p' type='password'><br><br>"
      "<input type='submit' value='Save &amp; Reboot'></form>");
  });

  server.on("/save", HTTP_POST, []() {
    String ssid = server.arg("s");
    String pass = server.arg("p");
    if (ssid.length()) {
      saveCred(ssid.c_str(), pass.c_str());
      server.send(200, "text/html",
        "Saved! Rebooting…<script>setTimeout(()=>{location.href='/'},5000);</script>");
      delay(1000);
      ESP.restart();
    } else {
      server.send(400, "text/plain", "SSID missing");
    }
  });
  server.begin();
}

// ═══════════════════════════════════════════════════════════
// "level N" parser
// Returns the parsed level (1–99) or -1 if the text doesn't match.
// Accepts: "level 5", "level5", "level 50", etc. (case-insensitive)
// ═══════════════════════════════════════════════════════════
int parseLevel(const String& text) {
  // text has already been lowercased by caller
  if (!text.startsWith("level")) return -1;

  int pos = 5;                          // skip "level"
  while (pos < (int)text.length() && text[pos] == ' ') pos++;  // skip spaces

  if (pos >= (int)text.length() || !isDigit(text[pos])) return -1;

  int numStart = pos;
  while (pos < (int)text.length() && isDigit(text[pos])) pos++;
  int val = text.substring(numStart, pos).toInt();

  if (val < 1 || val > 99) return -1;
  return val;
}

// ═══════════════════════════════════════════════════════════
// Telegram
// ═══════════════════════════════════════════════════════════

WiFiClientSecure      tgClient;
UniversalTelegramBot  bot(BOT_TOKEN, tgClient);
unsigned long         lastCheck    = 0;
const unsigned        POLL_INTERVAL = 2000;   // ms

void handleMessages(int numMsgs);   // forward declaration

// ═══════════════════════════════════════════════════════════
// Setup
// ═══════════════════════════════════════════════════════════
void setup() {
  pinMode(DATA_PIN,   OUTPUT);
  pinMode(ENABLE_PIN, OUTPUT);
  digitalWrite(DATA_PIN,   LOW);
  digitalWrite(ENABLE_PIN, LOW);

  Serial.begin(115200);
  delay(100);

  loadCreds();
  WiFi.mode(WIFI_STA);
  if (!connectToKnown()) {
    startConfigPortal();
  } else {
    tgClient.setInsecure();
  }

  Serial.printf("ESP ready  –  TX ID: 0x%04X  default level: %d\n",
                TRANSMITTER_ID, defaultLevel);
}

// ═══════════════════════════════════════════════════════════
// Loop
// ═══════════════════════════════════════════════════════════
void loop() {
  server.handleClient();
  unsigned long now = millis();

  if (now - lastCheck >= POLL_INTERVAL) {
    int numNewMsgs = bot.getUpdates(bot.last_message_received + 1);
    while (numNewMsgs) {
      handleMessages(numNewMsgs);
      numNewMsgs = bot.getUpdates(bot.last_message_received + 1);
    }
    lastCheck = now;
  }
}

// ═══════════════════════════════════════════════════════════
// Command handler
// ═══════════════════════════════════════════════════════════
void handleMessages(int numMsgs) {
  for (int i = 0; i < numMsgs; i++) {
    String chat_id = bot.messages[i].chat_id;
    String text    = bot.messages[i].text;
    text.toLowerCase();

    // ── /start ──────────────────────────────────────────
    if (text == "/start") {
      String msg = String("Elektra CXL bot lista.\n\n") +
        "Intensidad actual: " + String(defaultLevel) + "\n\n" +
        "Comandos:\n"
        " • level N        – fijar intensidad (1-99)\n"
        " • /channel1      – shock canal 1 (intensidad actual)\n"
        " • /channel2      – shock canal 2 (intensidad actual)\n"
        " • /test1         – vibracion + sonido en canal 1\n"
        " • /test2         – vibracion + sonido en canal 2";
      bot.sendMessage(chat_id, msg, "");
    }

    // ── level N ─────────────────────────────────────────
    else {
      int lvl = parseLevel(text);
      if (lvl > 0) {
        defaultLevel = (uint8_t)lvl;
        String msg = "Intensidad -> nivel " + String(defaultLevel);
        bot.sendMessage(chat_id, msg, "");
        Serial.printf("Default level set to %d\n", defaultLevel);
      }

      // ── /channel1 ───────────────────────────────────
      else if (text == "/channel1") {
        sendStimulus(TRANSMITTER_ID, 0, ACTION_SHOCK, defaultLevel);
        String msg = "Shock canal 1 – nivel " + String(defaultLevel);
        bot.sendMessage(chat_id, msg, "");
        Serial.printf("Shock ch0 level %d\n", defaultLevel);
      }

      // ── /channel2 ───────────────────────────────────
      else if (text == "/channel2") {
        sendStimulus(TRANSMITTER_ID, 1, ACTION_SHOCK, defaultLevel);
        String msg = "Shock canal 2 – nivel " + String(defaultLevel);
        bot.sendMessage(chat_id, msg, "");
        Serial.printf("Shock ch1 level %d\n", defaultLevel);
      }

      // ── /test1  (vibrate + beep on channel 1) ───────
      else if (text == "/test1") {
        sendStimulus(TRANSMITTER_ID, 0, ACTION_VIBRATE, 50);
        delay(500);
        sendStimulus(TRANSMITTER_ID, 0, ACTION_BEEP, 0);
        bot.sendMessage(chat_id, "Test canal 1: vibracion + sonido", "");
        Serial.println("Test ch0: vibrate + beep");
      }

      // ── /test2  (vibrate + beep on channel 2) ───────
      else if (text == "/test2") {
        sendStimulus(TRANSMITTER_ID, 1, ACTION_VIBRATE, 50);
        delay(500);
        sendStimulus(TRANSMITTER_ID, 1, ACTION_BEEP, 0);
        bot.sendMessage(chat_id, "Test canal 2: vibracion + sonido", "");
        Serial.println("Test ch1: vibrate + beep");
      }
    }
  }
}
