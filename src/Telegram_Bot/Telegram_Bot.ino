/********************************************************************
 *  ESP32 RF-TRANSMITTER + TELEGRAM BOT
 *  ---------------------------------------------------------------
 *  CaiXianlin OOK protocol encoder – no hardcoded waveforms.
 *  Any intensity 1-99 on two collar channels, fully controlled
 *  via Telegram bot commands.
 *
 *  Board: ESP32 Dev Module (or any ESP32 variant)
 *
 *  Before flashing:
 *    1. Set TRANSMITTER_ID to the 16-bit ID your collars are paired to.
 *    2. Set BOT_TOKEN to your Telegram bot token.
 *    3. WiFi credentials are stored in EEPROM via a captive-portal
 *       web page on first boot (connect to "Shocker_Config" AP).
 *
 *  RF wiring (433 MHz OOK module):
 *    DATA_PIN   → DATA pin of transmitter
 *    ENABLE_PIN → ENABLE / VCC-switch pin of transmitter
 *
 *  Commands:
 *    /start          – show this help
 *    /intensity N    – set shock intensity 1-99
 *    /period N       – set periodic interval in seconds (1-3600)
 *    /shock          – send a single shock on the current channel
 *    /periodic       – start periodic shocks until /stop
 *    /stop           – stop periodic shocks
 *    /ch1            – target collar 1 only
 *    /ch2            – target collar 2 only
 *    /both           – target both collars (default)
 *    /intermittent   – alternate collar 1 / 2 each shock
 *    /status         – show current settings
 *    /test           – vibration + beep (no shock) on current channel
 *
 *  Dependencies (install via Arduino Library Manager):
 *    • UniversalTelegramBot  (Brian Lough / witnessmenow)
 *    • ArduinoJson            (Benoit Blanchon)   – required by above
 ********************************************************************/

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <UniversalTelegramBot.h>

// ── RF pin assignments ────────────────────────────────────
const uint8_t DATA_PIN   = 26;   // GPIO26
const uint8_t ENABLE_PIN = 27;   // GPIO27

#ifndef LED_BUILTIN
#define LED_BUILTIN 2   // GPIO2 is the built-in LED on most ESP32 boards
#endif

// ── Telegram bot token ────────────────────────────────────
const char* BOT_TOKEN = "7730578439:AAFxy1-uXIsE8c73HOsEb9axsZlmZ_kv684";

// ── Collar configuration ──────────────────────────────────
#define TRANSMITTER_ID  0xB497   // 16-bit ID the collars are paired to

// ── Hardcoded fallback network ────────────────────────────
const char* HARDCODED_SSID = "si sos kinky y lo sabes, aplaudi";
const char* HARDCODED_PASS = "obligame";

// ── WiFi credential store (EEPROM) ───────────────────────
#define MAX_CREDENTIALS  5
#define EEPROM_SIZE      700   // 1 + MAX_CREDENTIALS * sizeof(WiFiCred) = 1 + 5*128

struct WiFiCred {
  char ssid[64];   // 64 to accommodate long SSIDs
  char pass[64];
};

WiFiCred creds[MAX_CREDENTIALS];
uint8_t  credCount = 0;

WebServer configServer(80);

// ═══════════════════════════════════════════════════════════
// CaiXianlin OOK Protocol Encoder
//
// Packet (40 bits, MSB first):
//   [39:24]  Transmitter ID  16 bits
//   [23:20]  Channel          4 bits  (0 = ch1, 1 = ch2)
//   [19:16]  Action           4 bits  (1=Shock, 2=Vibrate, 3=Beep)
//   [15:8]   Intensity        8 bits  (0-99; always 0 for Beep)
//   [7:0]    Checksum         8 bits  (sum of all 5 payload bytes mod 256)
//
// Bit timing @ 433.95 MHz OOK:
//   Sync:  HIGH 1400 µs  /  LOW 750 µs
//   Bit 1: HIGH  750 µs  /  LOW 250 µs
//   Bit 0: HIGH  250 µs  /  LOW 750 µs
// ═══════════════════════════════════════════════════════════

#define ACTION_SHOCK    1
#define ACTION_VIBRATE  2
#define ACTION_BEEP     3

void sendStimulus(uint16_t txId, uint8_t channel, uint8_t action,
                  uint8_t intensity, uint8_t repeats = 10) {

  if (channel > 2)                 channel = 2;
  if (action < 1 || action > 3)   action  = ACTION_SHOCK;
  if (intensity > 99)              intensity = 99;
  if (action == ACTION_BEEP)       intensity = 0;

  uint64_t payload = ((uint64_t)txId      << 24) |
                     ((uint64_t)channel   << 20) |
                     ((uint64_t)action    << 16) |
                     ((uint64_t)intensity <<  8);

  uint8_t checksum = 0;
  for (uint8_t i = 0; i < 5; i++)
    checksum += (uint8_t)((payload >> (i * 8)) & 0xFF);

  uint64_t packet = payload | (uint64_t)checksum;

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

// ── RF command queue ──────────────────────────────────────
// sendStimulus() blocks for ~440 ms (busy-wait bit-banging).
// We enqueue commands from the Telegram handler and execute them
// in loop(), keeping the main loop responsive between transmissions.
struct RFCmd {
  uint8_t channel;
  uint8_t action;
  uint8_t intensity;
};

static QueueHandle_t rfQueue = nullptr;
#define RF_QUEUE_LEN 16

static void enqueueRF(uint8_t channel, uint8_t action, uint8_t intensity) {
  RFCmd cmd = { channel, action, intensity };
  if (xQueueSend(rfQueue, &cmd, 0) != pdTRUE)
    Serial.println("[RF] queue full — command dropped");
}

// ═══════════════════════════════════════════════════════════
// Bot state
// ═══════════════════════════════════════════════════════════

enum ChannelMode { CH_BOTH = 0, CH_1, CH_2, CH_INTERMITTENT };

static uint8_t      intensity        = 5;       // shock intensity 1-99
static uint32_t     periodMs         = 2000;    // periodic interval in ms
static bool         periodicRunning  = false;
static unsigned long lastShockMs     = 0;
static ChannelMode  channelMode      = CH_BOTH;
static uint8_t      intermittentCh   = 0;       // 0 or 1, tracks alternation

static uint8_t clampIntensity(int v) {
  if (v < 1)  return 1;
  if (v > 99) return 99;
  return (uint8_t)v;
}

static uint32_t secondsToMs(int secs) {
  if (secs < 1)    secs = 1;
  if (secs > 3600) secs = 3600;
  return (uint32_t)secs * 1000UL;
}

const char* channelModeName() {
  switch (channelMode) {
    case CH_BOTH:         return "ambos";
    case CH_1:            return "collar 1";
    case CH_2:            return "collar 2";
    case CH_INTERMITTENT: return "intermitente";
  }
  return "?";
}

// Enqueue shock(s) according to current channelMode.
void doShock() {
  switch (channelMode) {
    case CH_BOTH:
      enqueueRF(0, ACTION_SHOCK, intensity);
      enqueueRF(1, ACTION_SHOCK, intensity);
      break;
    case CH_1:
      enqueueRF(0, ACTION_SHOCK, intensity);
      break;
    case CH_2:
      enqueueRF(1, ACTION_SHOCK, intensity);
      break;
    case CH_INTERMITTENT:
      enqueueRF(intermittentCh, ACTION_SHOCK, intensity);
      intermittentCh ^= 1;   // toggle 0 ↔ 1
      break;
  }
}

// Enqueue test (vibrate + beep) on current channel.
void doTest() {
  auto enqueueTest = [](uint8_t ch) {
    enqueueRF(ch, ACTION_VIBRATE, 50);
    enqueueRF(ch, ACTION_BEEP, 0);
  };
  switch (channelMode) {
    case CH_BOTH:
      enqueueTest(0);
      enqueueTest(1);
      break;
    case CH_1:
      enqueueTest(0);
      break;
    case CH_2:
      enqueueTest(1);
      break;
    case CH_INTERMITTENT:
      enqueueTest(intermittentCh);
      intermittentCh ^= 1;
      break;
  }
}

// ═══════════════════════════════════════════════════════════
// WiFi / captive-portal helpers
// ═══════════════════════════════════════════════════════════

void loadCreds() {
  EEPROM.begin(EEPROM_SIZE);
  credCount = EEPROM.read(0);
  if (credCount > MAX_CREDENTIALS) credCount = 0;
  for (uint8_t i = 0; i < credCount; i++)
    EEPROM.get(1 + i * sizeof(WiFiCred), creds[i]);
}

void saveCred(const char* s, const char* p) {
  // If SSID already exists, overwrite its password
  for (uint8_t i = 0; i < credCount; i++) {
    if (strcmp(creds[i].ssid, s) == 0) {
      strncpy(creds[i].pass, p, sizeof(creds[0].pass) - 1);
      creds[i].pass[sizeof(creds[0].pass) - 1] = '\0';
      EEPROM.put(1 + i * sizeof(WiFiCred), creds[i]);
      EEPROM.commit();
      Serial.printf("Contraseña actualizada para '%s'\n", s);
      return;
    }
  }
  // New entry
  if (credCount >= MAX_CREDENTIALS) return;
  strncpy(creds[credCount].ssid, s, sizeof(creds[0].ssid) - 1);
  strncpy(creds[credCount].pass, p, sizeof(creds[0].pass) - 1);
  creds[credCount].ssid[sizeof(creds[0].ssid) - 1] = '\0';
  creds[credCount].pass[sizeof(creds[0].pass) - 1] = '\0';
  EEPROM.put(1 + credCount * sizeof(WiFiCred), creds[credCount]);
  credCount++;
  EEPROM.write(0, credCount);
  EEPROM.commit();
}

static bool tryConnect(const char* ssid, const char* pass) {
  Serial.printf("  trying '%s' … ", ssid);
  WiFi.disconnect(true);
  delay(200);
  WiFi.begin(ssid, pass);
  for (uint8_t j = 0; j < 60 && WiFi.status() != WL_CONNECTED; j++) {
    delay(500);
    digitalWrite(LED_BUILTIN, j % 2);   // parpadeo rápido = intentando
  }
  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(LED_BUILTIN, HIGH);
    Serial.println("OK");
    return true;
  }
  digitalWrite(LED_BUILTIN, LOW);
  Serial.printf("fail (status=%d)\n", WiFi.status());
  return false;
}

bool connectToKnown() {
  // Try hardcoded network first (may be overridden in EEPROM below)
  Serial.println("  [hardcoded]");
  // Check if EEPROM has an updated password for the hardcoded SSID
  const char* hardPass = HARDCODED_PASS;
  for (uint8_t i = 0; i < credCount; i++) {
    if (strcmp(creds[i].ssid, HARDCODED_SSID) == 0) {
      hardPass = creds[i].pass;  // use stored (potentially updated) password
      break;
    }
  }
  if (tryConnect(HARDCODED_SSID, hardPass)) return true;

  // Try remaining stored credentials
  for (uint8_t i = 0; i < credCount; i++) {
    if (strcmp(creds[i].ssid, HARDCODED_SSID) == 0) continue;  // already tried
    if (tryConnect(creds[i].ssid, creds[i].pass)) return true;
  }
  return false;
}

void startConfigPortal() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("Shocker_Config");   // open AP, no password
  IPAddress ip = WiFi.softAPIP();
  Serial.printf("Config AP up – connect to Shocker_Config (no password)"
                " and browse to http://%s/\n", ip.toString().c_str());

  configServer.on("/", HTTP_GET, []() {
    configServer.send(200, "text/html",
      "<form method='POST' action='/save'>"
      "SSID:<br><input name='s'><br>"
      "PASS:<br><input name='p' type='password'><br><br>"
      "<input type='submit' value='Save &amp; Reboot'></form>");
  });

  configServer.on("/save", HTTP_POST, []() {
    String ssid = configServer.arg("s");
    String pass = configServer.arg("p");
    if (ssid.length()) {
      saveCred(ssid.c_str(), pass.c_str());
      configServer.send(200, "text/html",
        "Guardado! Reiniciando…"
        "<script>setTimeout(()=>{location.href='/'},5000);</script>");
      delay(1000);
      ESP.restart();
    } else {
      configServer.send(400, "text/plain", "SSID missing");
    }
  });
  configServer.begin();
}

// ═══════════════════════════════════════════════════════════
// Command parsers
// ═══════════════════════════════════════════════════════════

// "/intensity N" → returns 1-99, or -1 on failure
int parseIntensity(const String& text) {
  int pos = 0;
  if      (text.startsWith("/intensity")) pos = 10;
  else if (text.startsWith("intensity"))  pos = 9;
  else return -1;

  while (pos < (int)text.length() && text[pos] == ' ') pos++;
  if (pos >= (int)text.length() || !isDigit(text[pos])) return -1;

  int start = pos;
  while (pos < (int)text.length() && isDigit(text[pos])) pos++;
  int val = text.substring(start, pos).toInt();
  return (val >= 1 && val <= 99) ? val : -1;
}

// "/period N" → returns 1-3600, or -1 on failure
int parsePeriod(const String& text) {
  int pos = 0;
  if      (text.startsWith("/period")) pos = 7;
  else if (text.startsWith("period"))  pos = 6;
  else return -1;

  while (pos < (int)text.length() && text[pos] == ' ') pos++;
  if (pos >= (int)text.length() || !isDigit(text[pos])) return -1;

  int start = pos;
  while (pos < (int)text.length() && isDigit(text[pos])) pos++;
  int val = text.substring(start, pos).toInt();
  return (val >= 1 && val <= 3600) ? val : -1;
}

// ═══════════════════════════════════════════════════════════
// Telegram
// ═══════════════════════════════════════════════════════════

WiFiClientSecure     tgClient;
UniversalTelegramBot tgBot(BOT_TOKEN, tgClient);
unsigned long        lastCheck    = 0;
const unsigned       POLL_INTERVAL = 500;   // ms – lower = more responsive

void handleMessages(int numMsgs);  // forward declaration

// ═══════════════════════════════════════════════════════════
// Setup
// ═══════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n========================================");
  Serial.println("  ESP32 Telegram Bot → CaiXianlin Shocker");
  Serial.println("========================================");
  Serial.printf ("  TX ID:      0x%04X\n", TRANSMITTER_ID);
  Serial.printf ("  DATA_PIN:   GPIO%d\n", DATA_PIN);
  Serial.printf ("  ENABLE_PIN: GPIO%d\n", ENABLE_PIN);
  Serial.println("========================================\n");

  rfQueue = xQueueCreate(RF_QUEUE_LEN, sizeof(RFCmd));

  pinMode(DATA_PIN,    OUTPUT);
  pinMode(ENABLE_PIN,  OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(0,           INPUT_PULLUP);  // botón BOOT
  digitalWrite(DATA_PIN,   LOW);
  digitalWrite(ENABLE_PIN, LOW);
  digitalWrite(LED_BUILTIN, LOW);

  // Mantener BOOT apretado al encender → borra todas las redes guardadas
  delay(100);
  if (digitalRead(0) == LOW) {
    Serial.println(">>> BOOT presionado: borrando credenciales WiFi <<<");
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.write(0, 0);
    EEPROM.commit();
    // Parpadeo rápido para confirmar
    for (int k = 0; k < 10; k++) { digitalWrite(LED_BUILTIN, k%2); delay(100); }
    ESP.restart();
  }

  loadCreds();
  Serial.printf("Redes guardadas: %d\n", credCount);
  for (uint8_t i = 0; i < credCount; i++)
    Serial.printf("  [%d] '%s'\n", i, creds[i].ssid);
  WiFi.mode(WIFI_STA);
  if (!connectToKnown()) {
    startConfigPortal();
  } else {
    tgClient.setInsecure();
    Serial.printf("WiFi OK  IP=%s\n", WiFi.localIP().toString().c_str());
  }

  Serial.printf("Ready  intensity=%d  period=%lus  channel=%s\n",
                intensity, periodMs / 1000UL, channelModeName());
}

// ═══════════════════════════════════════════════════════════
// Loop
// ═══════════════════════════════════════════════════════════
void loop() {
  configServer.handleClient();

  // Parpadeo lento del LED cuando está en modo AP esperando configuración
  if (WiFi.getMode() == WIFI_AP) {
    unsigned long t = millis();
    digitalWrite(LED_BUILTIN, (t / 1000) % 2);
    return;
  }

  unsigned long now = millis();

  // ── Drain RF queue – execute here in main task (not Telegram callback) ──
  RFCmd cmd;
  while (xQueueReceive(rfQueue, &cmd, 0) == pdTRUE) {
    sendStimulus(TRANSMITTER_ID, cmd.channel, cmd.action, cmd.intensity);
  }

  // ── Periodic shock engine ─────────────────────────────────────────────
  if (periodicRunning && (now - lastShockMs >= periodMs)) {
    lastShockMs = now;
    doShock();
    Serial.printf("[PERIODIC] shock  intensity=%d  period=%lus  ch=%s\n",
                  intensity, periodMs / 1000UL, channelModeName());
  }

  // ── Poll Telegram ─────────────────────────────────────────────────────
  if (now - lastCheck >= POLL_INTERVAL) {
    lastCheck = now;
    int n = tgBot.getUpdates(tgBot.last_message_received + 1);
    while (n) {
      handleMessages(n);
      n = tgBot.getUpdates(tgBot.last_message_received + 1);
    }
  }
}

// ═══════════════════════════════════════════════════════════
// Command handler
// ═══════════════════════════════════════════════════════════
void handleMessages(int numMsgs) {
  for (int i = 0; i < numMsgs; i++) {
    String chat_id = tgBot.messages[i].chat_id;
    String text    = tgBot.messages[i].text;
    text.toLowerCase();

    Serial.printf("[TG] chat=%s  text=%s\n", chat_id.c_str(), text.c_str());

    // ── /start / /help ───────────────────────────────────────────────────
    if (text == "/start" || text == "/help") {
      String msg =
        "⚡ Shocker Bot (ESP32) ⚡\n\n"
        "Comandos:\n"
        " /intensity N    – intensidad 1-99\n"
        " /period N       – periodo en segundos\n"
        " /shock          – shock único\n"
        " /periodic       – shocks periódicos (hasta /stop)\n"
        " /stop           – detener periódicos\n"
        " /ch1            – solo collar 1\n"
        " /ch2            – solo collar 2\n"
        " /both           – ambos collares (defecto)\n"
        " /intermittent   – alternar collar 1 y 2\n"
        " /test           – vibración + sonido (sin shock)\n"
        " /status         – ver configuración actual";
      tgBot.sendMessage(chat_id, msg, "");
    }

    // ── /status ──────────────────────────────────────────────────────────
    else if (text == "/status") {
      String msg =
        String("Estado actual:\n") +
        " Intensidad:  " + String(intensity) + "\n"
        " Periodo:     " + String(periodMs / 1000UL) + " s\n"
        " Canal:       " + channelModeName() + "\n"
        " Periodico:   " + (periodicRunning ? "ON ⚡" : "OFF ⛔");
      tgBot.sendMessage(chat_id, msg, "");
    }

    // ── /intensity N ─────────────────────────────────────────────────────
    else if (text.startsWith("/intensity") || text.startsWith("intensity")) {
      int val = parseIntensity(text);
      if (val > 0) {
        intensity = (uint8_t)val;
        tgBot.sendMessage(chat_id, "Intensidad → " + String(intensity), "");
        Serial.printf("[CMD] intensity=%d\n", intensity);
      } else {
        tgBot.sendMessage(chat_id, "Uso: /intensity N  (1-99)", "");
      }
    }

    // ── /period N ────────────────────────────────────────────────────────
    else if (text.startsWith("/period") || text.startsWith("period")) {
      int val = parsePeriod(text);
      if (val > 0) {
        periodMs = secondsToMs(val);
        String msg = "Periodo → " + String(val) + " s";
        if (periodicRunning) {
          msg += " (activo)";
          lastShockMs = millis();   // reset timer to new interval
        }
        tgBot.sendMessage(chat_id, msg, "");
        Serial.printf("[CMD] periodMs=%lu\n", periodMs);
      } else {
        tgBot.sendMessage(chat_id, "Uso: /period N  (1-3600 segundos)", "");
      }
    }

    // ── /shock ───────────────────────────────────────────────────────────
    else if (text == "/shock") {
      doShock();
      tgBot.sendMessage(chat_id,
        "⚡ Shock – intensidad " + String(intensity) +
        "  canal: " + channelModeName(), "");
      Serial.printf("[CMD] shock  intensity=%d  ch=%s\n",
                    intensity, channelModeName());
    }

    // ── /periodic ────────────────────────────────────────────────────────
    else if (text == "/periodic") {
      periodicRunning = true;
      lastShockMs     = millis() - periodMs;   // fire first shock immediately
      String msg =
        String("⚡ Modo periodico ON ⚡\n") +
        " Intensidad: " + String(intensity) + "\n"
        " Periodo:    " + String(periodMs / 1000UL) + " s\n"
        " Canal:      " + channelModeName() + "\n\n"
        "Enviar /stop para detener.";
      tgBot.sendMessage(chat_id, msg, "");
      Serial.printf("[CMD] periodic ON  intensity=%d  periodMs=%lu  ch=%s\n",
                    intensity, periodMs, channelModeName());
    }

    // ── /stop ────────────────────────────────────────────────────────────
    else if (text == "/stop") {
      periodicRunning = false;
      tgBot.sendMessage(chat_id, "⛔ Modo periodico OFF", "");
      Serial.println("[CMD] periodic OFF");
    }

    // ── /ch1 ─────────────────────────────────────────────────────────────
    else if (text == "/ch1") {
      channelMode = CH_1;
      tgBot.sendMessage(chat_id, "Canal → collar 1 unicamente", "");
    }

    // ── /ch2 ─────────────────────────────────────────────────────────────
    else if (text == "/ch2") {
      channelMode = CH_2;
      tgBot.sendMessage(chat_id, "Canal → collar 2 unicamente", "");
    }

    // ── /both ────────────────────────────────────────────────────────────
    else if (text == "/both") {
      channelMode = CH_BOTH;
      tgBot.sendMessage(chat_id, "Canal → ambos collares", "");
    }

    // ── /intermittent ────────────────────────────────────────────────────
    else if (text == "/intermittent") {
      channelMode   = CH_INTERMITTENT;
      intermittentCh = 0;   // start from collar 1
      tgBot.sendMessage(chat_id, "Canal → intermitente (alternando collar 1 y 2)", "");
    }

    // ── /test ────────────────────────────────────────────────────────────
    else if (text == "/test") {
      doTest();
      tgBot.sendMessage(chat_id,
        "Test: vibracion + sonido  canal: " + String(channelModeName()), "");
    }

    // ── Unknown ──────────────────────────────────────────────────────────
    else {
      tgBot.sendMessage(chat_id,
        "Comando no reconocido. Usa /start para ver la lista.", "");
    }
  }
}
