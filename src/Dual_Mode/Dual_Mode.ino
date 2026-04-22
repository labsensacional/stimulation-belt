/********************************************************************
 *  ESP32 RF-TRANSMITTER — DUAL MODE
 *  ---------------------------------------------------------------
 *  Hold BOOT (GPIO0) at power-on to select mode:
 *
 *    No press (default)  →  start in the previously saved mode
 *                           (first boot defaults to Telegram Bot)
 *    Release before 2 s  →  toggle mode (Telegram ↔ BLE Keyboard),
 *                           save choice and restart
 *    Hold for >= 2 s     →  clear saved WiFi credentials and restart
 *
 *  Mode 0 — Telegram Bot
 *    WiFi credentials stored via captive portal ("Shocker_Config" AP).
 *    Control shocks with Telegram commands.
 *
 *  Mode 1 — BLE Keyboard
 *    Scans for a BLE HID keyboard. Key presses fire shocks / adjust
 *    levels on a numpad layout.
 *    Keys: 7/8=shock  4/5=level+  1/2=level-  6/3=test  +/-=both  0=reset
 *
 *  RF wiring (433 MHz OOK module):
 *    DATA_PIN   → GPIO26
 *    ENABLE_PIN → GPIO27
 *
 *  Dependencies (Arduino Library Manager):
 *    • UniversalTelegramBot  (Brian Lough / witnessmenow)
 *    • ArduinoJson            (Benoit Blanchon)
 ********************************************************************/

// ── Mode constants ────────────────────────────────────────
#define MODE_TELEGRAM  0
#define MODE_BLE       1

// ── Includes ──────────────────────────────────────────────
#include <Arduino.h>
#include <EEPROM.h>

// Telegram mode
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <UniversalTelegramBot.h>

// BLE mode
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEClient.h>
#include <BLESecurity.h>

// ── RF pin assignments ────────────────────────────────────
const uint8_t DATA_PIN   = 26;
const uint8_t ENABLE_PIN = 27;

#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

// ── Collar configuration ──────────────────────────────────
#define TRANSMITTER_ID  0xB497

// ── Telegram credentials ──────────────────────────────────
const char* BOT_TOKEN      = "7730578439:AAFxy1-uXIsE8c73HOsEb9axsZlmZ_kv684";
const char* HARDCODED_SSID = "si sos kinky y lo sabes, aplaudi";
const char* HARDCODED_PASS = "obligame";

// ── EEPROM layout ─────────────────────────────────────────
// [0]       credCount  (uint8)
// [1..640]  WiFi credentials  (5 × 128 bytes)
// [641]     active mode  (uint8: MODE_TELEGRAM or MODE_BLE)
#define MAX_CREDENTIALS  5
#define MODE_ADDR        641
#define EEPROM_SIZE      642

// ── RF action codes ───────────────────────────────────────
#define ACTION_SHOCK    1
#define ACTION_VIBRATE  2
#define ACTION_BEEP     3

// ═══════════════════════════════════════════════════════════
// RF — shared encoder + queue
// ═══════════════════════════════════════════════════════════

struct RFCmd {
  uint16_t txId;
  uint8_t  channel;
  uint8_t  action;
  uint8_t  intensity;
};

static QueueHandle_t rfQueue = nullptr;
#define RF_QUEUE_LEN 16

static void enqueueRF(uint8_t channel, uint8_t action, uint8_t intensity) {
  RFCmd cmd = { TRANSMITTER_ID, channel, action, intensity };
  if (xQueueSend(rfQueue, &cmd, 0) != pdTRUE)
    Serial.println("[RF] queue full — command dropped");
}

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

  digitalWrite(ENABLE_PIN, HIGH);
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
    digitalWrite(DATA_PIN, HIGH); delayMicroseconds(250);
    digitalWrite(DATA_PIN, LOW);  delayMicroseconds(750);
    digitalWrite(DATA_PIN, HIGH); delayMicroseconds(250);
    digitalWrite(DATA_PIN, LOW);  delayMicroseconds(750);
  }
  digitalWrite(DATA_PIN, LOW);
  digitalWrite(ENABLE_PIN, LOW);
}

// ═══════════════════════════════════════════════════════════
// TELEGRAM BOT
// ═══════════════════════════════════════════════════════════

struct WiFiCred { char ssid[64]; char pass[64]; };

WiFiCred creds[MAX_CREDENTIALS];
uint8_t  credCount = 0;

WebServer            configServer(80);
WiFiClientSecure     tgClient;
UniversalTelegramBot* tgBot = nullptr;

unsigned long  tg_lastCheck   = 0;
const unsigned POLL_INTERVAL  = 500;

enum ChannelMode { CH_BOTH = 0, CH_1, CH_2, CH_INTERMITTENT };

static uint8_t      tg_intensity       = 5;
static uint32_t     tg_periodMs        = 2000;
static bool         tg_periodicRunning = false;
static unsigned long tg_lastShockMs    = 0;
static ChannelMode  tg_channelMode     = CH_BOTH;
static uint8_t      tg_intermittentCh  = 0;

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
  switch (tg_channelMode) {
    case CH_BOTH:         return "ambos";
    case CH_1:            return "collar 1";
    case CH_2:            return "collar 2";
    case CH_INTERMITTENT: return "intermitente";
  }
  return "?";
}

void tg_doShock() {
  switch (tg_channelMode) {
    case CH_BOTH:
      enqueueRF(0, ACTION_SHOCK, tg_intensity);
      enqueueRF(1, ACTION_SHOCK, tg_intensity);
      break;
    case CH_1:
      enqueueRF(0, ACTION_SHOCK, tg_intensity);
      break;
    case CH_2:
      enqueueRF(1, ACTION_SHOCK, tg_intensity);
      break;
    case CH_INTERMITTENT:
      enqueueRF(tg_intermittentCh, ACTION_SHOCK, tg_intensity);
      tg_intermittentCh ^= 1;
      break;
  }
}

void tg_doTest() {
  auto enqueueTest = [](uint8_t ch) {
    enqueueRF(ch, ACTION_VIBRATE, 50);
    enqueueRF(ch, ACTION_BEEP, 0);
  };
  switch (tg_channelMode) {
    case CH_BOTH:        enqueueTest(0); enqueueTest(1); break;
    case CH_1:           enqueueTest(0);                 break;
    case CH_2:           enqueueTest(1);                 break;
    case CH_INTERMITTENT:
      enqueueTest(tg_intermittentCh);
      tg_intermittentCh ^= 1;
      break;
  }
}

void loadCreds() {
  EEPROM.begin(EEPROM_SIZE);
  credCount = EEPROM.read(0);
  if (credCount > MAX_CREDENTIALS) credCount = 0;
  for (uint8_t i = 0; i < credCount; i++)
    EEPROM.get(1 + i * sizeof(WiFiCred), creds[i]);
}

void saveCred(const char* s, const char* p) {
  for (uint8_t i = 0; i < credCount; i++) {
    if (strcmp(creds[i].ssid, s) == 0) {
      strncpy(creds[i].pass, p, sizeof(creds[0].pass) - 1);
      creds[i].pass[sizeof(creds[0].pass) - 1] = '\0';
      EEPROM.put(1 + i * sizeof(WiFiCred), creds[i]);
      EEPROM.commit();
      return;
    }
  }
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
    digitalWrite(LED_BUILTIN, j % 2);
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
  const char* hardPass = HARDCODED_PASS;
  for (uint8_t i = 0; i < credCount; i++) {
    if (strcmp(creds[i].ssid, HARDCODED_SSID) == 0) {
      hardPass = creds[i].pass;
      break;
    }
  }
  if (tryConnect(HARDCODED_SSID, hardPass)) return true;
  for (uint8_t i = 0; i < credCount; i++) {
    if (strcmp(creds[i].ssid, HARDCODED_SSID) == 0) continue;
    if (tryConnect(creds[i].ssid, creds[i].pass)) return true;
  }
  return false;
}

void startConfigPortal() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("Shocker_Config");
  IPAddress ip = WiFi.softAPIP();
  Serial.printf("Config AP – conectate a Shocker_Config y abrí http://%s/\n",
                ip.toString().c_str());

  configServer.on("/", HTTP_GET, []() {
    configServer.send(200, "text/html",
      "<form method='POST' action='/save'>"
      "SSID:<br><input name='s'><br>"
      "PASS:<br><input name='p' type='password'><br><br>"
      "<input type='submit' value='Guardar y reiniciar'></form>");
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

void handleMessages(int numMsgs) {
  for (int i = 0; i < numMsgs; i++) {
    String chat_id = tgBot->messages[i].chat_id;
    String text    = tgBot->messages[i].text;
    text.toLowerCase();
    Serial.printf("[TG] chat=%s  text=%s\n", chat_id.c_str(), text.c_str());

    if (text == "/start" || text == "/help") {
      tgBot->sendMessage(chat_id,
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
        " /status         – ver configuración actual", "");
    } else if (text == "/status") {
      tgBot->sendMessage(chat_id,
        String("Estado actual:\n") +
        " Intensidad:  " + String(tg_intensity) + "\n"
        " Periodo:     " + String(tg_periodMs / 1000UL) + " s\n"
        " Canal:       " + channelModeName() + "\n"
        " Periodico:   " + (tg_periodicRunning ? "ON ⚡" : "OFF ⛔"), "");
    } else if (text.startsWith("/intensity") || text.startsWith("intensity")) {
      int val = parseIntensity(text);
      if (val > 0) {
        tg_intensity = (uint8_t)val;
        tgBot->sendMessage(chat_id, "Intensidad → " + String(tg_intensity), "");
      } else {
        tgBot->sendMessage(chat_id, "Uso: /intensity N  (1-99)", "");
      }
    } else if (text.startsWith("/period") || text.startsWith("period")) {
      int val = parsePeriod(text);
      if (val > 0) {
        tg_periodMs = secondsToMs(val);
        String msg = "Periodo → " + String(val) + " s";
        if (tg_periodicRunning) { msg += " (activo)"; tg_lastShockMs = millis(); }
        tgBot->sendMessage(chat_id, msg, "");
      } else {
        tgBot->sendMessage(chat_id, "Uso: /period N  (1-3600 segundos)", "");
      }
    } else if (text == "/shock") {
      tg_doShock();
      tgBot->sendMessage(chat_id,
        "⚡ Shock – intensidad " + String(tg_intensity) +
        "  canal: " + channelModeName(), "");
    } else if (text == "/periodic") {
      tg_periodicRunning = true;
      tg_lastShockMs     = millis() - tg_periodMs;
      tgBot->sendMessage(chat_id,
        String("⚡ Modo periodico ON ⚡\n") +
        " Intensidad: " + String(tg_intensity) + "\n"
        " Periodo:    " + String(tg_periodMs / 1000UL) + " s\n"
        " Canal:      " + channelModeName() + "\n\n"
        "Enviar /stop para detener.", "");
    } else if (text == "/stop") {
      tg_periodicRunning = false;
      tgBot->sendMessage(chat_id, "⛔ Modo periodico OFF", "");
    } else if (text == "/ch1") {
      tg_channelMode = CH_1;
      tgBot->sendMessage(chat_id, "Canal → collar 1 unicamente", "");
    } else if (text == "/ch2") {
      tg_channelMode = CH_2;
      tgBot->sendMessage(chat_id, "Canal → collar 2 unicamente", "");
    } else if (text == "/both") {
      tg_channelMode = CH_BOTH;
      tgBot->sendMessage(chat_id, "Canal → ambos collares", "");
    } else if (text == "/intermittent") {
      tg_channelMode    = CH_INTERMITTENT;
      tg_intermittentCh = 0;
      tgBot->sendMessage(chat_id, "Canal → intermitente (alternando collar 1 y 2)", "");
    } else if (text == "/test") {
      tg_doTest();
      tgBot->sendMessage(chat_id,
        "Test: vibracion + sonido  canal: " + String(channelModeName()), "");
    } else {
      tgBot->sendMessage(chat_id, "Comando no reconocido. Usa /start para ver la lista.", "");
    }
  }
}

void setupTelegram() {
  Serial.println("========================================");
  Serial.println("  MODO: Telegram Bot");
  Serial.printf ("  TX ID:      0x%04X\n", TRANSMITTER_ID);
  Serial.printf ("  DATA_PIN:   GPIO%d\n", DATA_PIN);
  Serial.printf ("  ENABLE_PIN: GPIO%d\n", ENABLE_PIN);
  Serial.println("========================================\n");

  loadCreds();
  Serial.printf("Redes guardadas: %d\n", credCount);
  for (uint8_t i = 0; i < credCount; i++)
    Serial.printf("  [%d] '%s'\n", i, creds[i].ssid);

  WiFi.mode(WIFI_STA);
  if (!connectToKnown()) {
    startConfigPortal();
  } else {
    tgClient.setInsecure();
    tgBot = new UniversalTelegramBot(BOT_TOKEN, tgClient);
    Serial.printf("WiFi OK  IP=%s\n", WiFi.localIP().toString().c_str());
    Serial.printf("Ready  intensity=%d  period=%lus  channel=%s\n",
                  tg_intensity, tg_periodMs / 1000UL, channelModeName());
  }
}

void loopTelegram() {
  configServer.handleClient();

  if (WiFi.getMode() == WIFI_AP) {
    digitalWrite(LED_BUILTIN, (millis() / 1000) % 2);
    return;
  }

  unsigned long now = millis();

  RFCmd cmd;
  while (xQueueReceive(rfQueue, &cmd, 0) == pdTRUE)
    sendStimulus(cmd.txId, cmd.channel, cmd.action, cmd.intensity);

  if (tg_periodicRunning && (now - tg_lastShockMs >= tg_periodMs)) {
    tg_lastShockMs = now;
    tg_doShock();
  }

  if (tgBot && now - tg_lastCheck >= POLL_INTERVAL) {
    tg_lastCheck = now;
    int n = tgBot->getUpdates(tgBot->last_message_received + 1);
    while (n) {
      handleMessages(n);
      n = tgBot->getUpdates(tgBot->last_message_received + 1);
    }
  }
}

// ═══════════════════════════════════════════════════════════
// BLE KEYBOARD
// ═══════════════════════════════════════════════════════════

static BLEUUID hidServiceUUID("1812");
static BLEUUID reportCharUUID("2a4d");

static BLEAdvertisedDevice* targetDevice     = nullptr;
static BLEClient*           bleClient        = nullptr;
static bool                 doConnect        = false;
static bool                 ble_connected    = false;
static unsigned long        disconnectTimeMs = 0;
static unsigned long        lastHeartbeatMs  = 0;
static uint8_t              levelCh1         = 1;
static uint8_t              levelCh2         = 1;

#define RECONNECT_DELAY_MS  3000
#define HEARTBEAT_MS        5000

static uint8_t clampLevel(int v) {
  if (v < 1)  return 1;
  if (v > 99) return 99;
  return (uint8_t)v;
}

class SecurityCB : public BLESecurityCallbacks {
  uint32_t onPassKeyRequest() override { return 0; }
  void onPassKeyNotify(uint32_t passkey) override {
    Serial.printf("[SEC] Passkey: %06lu\n", passkey);
  }
  bool onSecurityRequest() override { return true; }
  bool onConfirmPIN(uint32_t pin) override { return true; }
  void onAuthenticationComplete(esp_ble_auth_cmpl_t auth) override {
    Serial.printf("[SEC] Auth %s\n", auth.success ? "OK" : "FAILED");
  }
};

void onHIDReport(BLERemoteCharacteristic* chr, uint8_t* data,
                 size_t len, bool isNotify) {
  if (len < 3) return;
  uint8_t mod = data[0];
  uint8_t key = data[2];
  if (key == 0) return;

  Serial.printf("[HID] key=0x%02X  mod=0x%02X\n", key, mod);

  switch (key) {
    // Shock
    case 0x24: case 0x5F:
      enqueueRF(0, ACTION_SHOCK, levelCh1); break;
    case 0x25: case 0x60:
      enqueueRF(1, ACTION_SHOCK, levelCh2); break;
    // Level +1
    case 0x21: case 0x5C:
      levelCh1 = clampLevel(levelCh1 + 1);
      Serial.printf("[KEY] Ch1=%d\n", levelCh1); break;
    case 0x22: case 0x5D:
      levelCh2 = clampLevel(levelCh2 + 1);
      Serial.printf("[KEY] Ch2=%d\n", levelCh2); break;
    // Level -1
    case 0x1E: case 0x59:
      levelCh1 = clampLevel(levelCh1 - 1);
      Serial.printf("[KEY] Ch1=%d\n", levelCh1); break;
    case 0x1F: case 0x5A:
      levelCh2 = clampLevel(levelCh2 - 1);
      Serial.printf("[KEY] Ch2=%d\n", levelCh2); break;
    // Test (vibrate + beep)
    case 0x23: case 0x5E:
      enqueueRF(0, ACTION_VIBRATE, levelCh1);
      enqueueRF(0, ACTION_BEEP, 0); break;
    case 0x20: case 0x5B:
      enqueueRF(1, ACTION_VIBRATE, levelCh2);
      enqueueRF(1, ACTION_BEEP, 0); break;
    // Both +1
    case 0x57:
      levelCh1 = clampLevel(levelCh1 + 1);
      levelCh2 = clampLevel(levelCh2 + 1);
      Serial.printf("[KEY] Ch1=%d Ch2=%d\n", levelCh1, levelCh2); break;
    case 0x2E:
      if (mod & 0x22) {
        levelCh1 = clampLevel(levelCh1 + 1);
        levelCh2 = clampLevel(levelCh2 + 1);
        Serial.printf("[KEY] Ch1=%d Ch2=%d\n", levelCh1, levelCh2);
      }
      break;
    // Both -1
    case 0x56: case 0x2D:
      levelCh1 = clampLevel(levelCh1 - 1);
      levelCh2 = clampLevel(levelCh2 - 1);
      Serial.printf("[KEY] Ch1=%d Ch2=%d\n", levelCh1, levelCh2); break;
    // Reset
    case 0x27: case 0x62:
      levelCh1 = levelCh2 = 1;
      Serial.println("[KEY] reset Ch1=Ch2=1"); break;
    default: break;
  }
}

class ScanCB : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice dev) override {
    if (dev.haveServiceUUID() && dev.isAdvertisingService(hidServiceUUID)) {
      Serial.printf("[SCAN] HID device: %s\n",
                    dev.getAddress().toString().c_str());
      targetDevice = new BLEAdvertisedDevice(dev);
      doConnect    = true;
      dev.getScan()->stop();
    }
  }
};

class ClientCB : public BLEClientCallbacks {
  void onConnect(BLEClient* c) override {
    Serial.println("[BLE] Connected");
  }
  void onDisconnect(BLEClient* c) override {
    Serial.println("[BLE] Disconnected — will retry");
    ble_connected    = false;
    disconnectTimeMs = millis();
  }
};

bool connectToDevice() {
  Serial.printf("[BLE] Connecting to %s …\n",
                targetDevice->getAddress().toString().c_str());
  if (!bleClient) {
    bleClient = BLEDevice::createClient();
    bleClient->setClientCallbacks(new ClientCB());
  }
  if (!bleClient->connect(targetDevice)) return false;

  esp_bd_addr_t addr;
  memcpy(addr, targetDevice->getAddress().getNative(), 6);
  esp_ble_set_encryption(addr, ESP_BLE_SEC_ENCRYPT_MITM);
  delay(2000);

  BLERemoteService* svc = bleClient->getService(hidServiceUUID);
  if (!svc) { bleClient->disconnect(); return false; }

  auto* chars = svc->getCharacteristics();
  int subscribed = 0;
  for (auto& kv : *chars) {
    BLERemoteCharacteristic* c = kv.second;
    if (c->getUUID().equals(reportCharUUID) && c->canNotify()) {
      c->registerForNotify(onHIDReport);
      subscribed++;
    }
  }
  if (subscribed == 0) { bleClient->disconnect(); return false; }
  Serial.printf("[BLE] Ready — subscribed to %d characteristic(s)\n", subscribed);
  return true;
}

void setupBLE() {
  Serial.println("========================================");
  Serial.println("  MODO: BLE Keyboard");
  Serial.printf ("  TX ID:      0x%04X\n", TRANSMITTER_ID);
  Serial.printf ("  DATA_PIN:   GPIO%d\n", DATA_PIN);
  Serial.printf ("  ENABLE_PIN: GPIO%d\n", ENABLE_PIN);
  Serial.println("  Keys: 7/8=shock  4/5=lvl+  1/2=lvl-");
  Serial.println("        6/3=test   +/-=both   0=reset");
  Serial.println("========================================\n");

  BLEDevice::init("ESP32-Shocker");
  BLEDevice::setSecurityCallbacks(new SecurityCB());
  BLESecurity* sec = new BLESecurity();
  sec->setAuthenticationMode(ESP_LE_AUTH_BOND);
  sec->setCapability(ESP_IO_CAP_NONE);
  sec->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  sec->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

  BLEScan* scan = BLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(new ScanCB());
  scan->setActiveScan(true);
  Serial.println("[BLE] Scanning 30 s — prendé el teclado ahora …");
  scan->start(30, false);
}

void loopBLE() {
  RFCmd cmd;
  while (xQueueReceive(rfQueue, &cmd, 0) == pdTRUE)
    sendStimulus(cmd.txId, cmd.channel, cmd.action, cmd.intensity);

  if (millis() - lastHeartbeatMs >= HEARTBEAT_MS) {
    lastHeartbeatMs = millis();
    Serial.printf("[BLE] uptime=%lus  connected=%s  Ch1=%d  Ch2=%d\n",
                  millis() / 1000, ble_connected ? "YES" : "NO",
                  levelCh1, levelCh2);
  }

  if (!ble_connected && !doConnect && targetDevice != nullptr)
    if (millis() - disconnectTimeMs >= RECONNECT_DELAY_MS)
      doConnect = true;

  if (doConnect && !ble_connected) {
    ble_connected = connectToDevice();
    doConnect     = false;
    if (!ble_connected) disconnectTimeMs = millis();
  }
}

// ═══════════════════════════════════════════════════════════
// Mode persistence
// ═══════════════════════════════════════════════════════════

static uint8_t activeMode = MODE_TELEGRAM;

uint8_t loadMode() {
  EEPROM.begin(EEPROM_SIZE);
  uint8_t m = EEPROM.read(MODE_ADDR);
  return (m == MODE_BLE) ? MODE_BLE : MODE_TELEGRAM;
}

void saveMode(uint8_t m) {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.write(MODE_ADDR, m);
  EEPROM.commit();
}

// ═══════════════════════════════════════════════════════════
// Setup & Loop
// ═══════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  delay(500);

  rfQueue = xQueueCreate(RF_QUEUE_LEN, sizeof(RFCmd));

  pinMode(DATA_PIN,    OUTPUT);
  pinMode(ENABLE_PIN,  OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(0,           INPUT_PULLUP);
  digitalWrite(DATA_PIN,    LOW);
  digitalWrite(ENABLE_PIN,  LOW);
  digitalWrite(LED_BUILTIN, LOW);

  // ── Boot button logic ─────────────────────────────────
  delay(100);
  if (digitalRead(0) == LOW) {
    unsigned long held = millis();
    while (digitalRead(0) == LOW && millis() - held < 3000) {
      digitalWrite(LED_BUILTIN, ((millis() - held) / 200) % 2);
      delay(10);
    }
    unsigned long duration = millis() - held;
    digitalWrite(LED_BUILTIN, LOW);

    if (duration >= 2000) {
      // Long press → clear WiFi credentials
      Serial.println(">>> BOOT largo: borrando credenciales WiFi <<<");
      EEPROM.begin(EEPROM_SIZE);
      EEPROM.write(0, 0);
      EEPROM.commit();
    } else {
      // Short press → toggle mode
      uint8_t next = (loadMode() == MODE_TELEGRAM) ? MODE_BLE : MODE_TELEGRAM;
      saveMode(next);
      Serial.printf(">>> BOOT corto: modo → %s <<<\n",
                    next == MODE_BLE ? "BLE Keyboard" : "Telegram Bot");
    }
    for (int k = 0; k < 10; k++) { digitalWrite(LED_BUILTIN, k % 2); delay(100); }
    ESP.restart();
  }

  activeMode = loadMode();
  Serial.printf("\n[BOOT] Modo activo: %s\n",
                activeMode == MODE_BLE ? "BLE Keyboard" : "Telegram Bot");

  if (activeMode == MODE_BLE) {
    setupBLE();
  } else {
    setupTelegram();
  }
}

void loop() {
  if (activeMode == MODE_BLE) {
    loopBLE();
  } else {
    loopTelegram();
  }
}
