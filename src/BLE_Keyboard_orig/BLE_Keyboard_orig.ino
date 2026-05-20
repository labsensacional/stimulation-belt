/********************************************************************
 *  ESP32  RF-TRANSMITTER + BLE KEYBOARD TRIGGER
 *  ---------------------------------------------------------------
 *  Scans for a BLE HID keyboard and connects to the first one
 *  found.  Any key press fires a level-1 shock on the configured
 *  channel using the CaiXianlin OOK protocol (fully encoded in
 *  firmware — no hardcoded captured waveforms).
 *
 *  IMPORTANT: Requires an ESP32.  The ESP8266 has no Bluetooth
 *  hardware and cannot run this sketch.
 *
 *  Before flashing:
 *    1. Set TRANSMITTER_ID to the 16-bit ID your collar is paired
 *       to.  You can find it by decoding one of the .sub files
 *       with a Flipper Zero or by putting the collar in pairing
 *       mode and using any value you like.
 *    2. Channels 0 and 1 are used (mapped to keys 7/4/1 and 8/5/2).
 *
 *  RF wiring (433 MHz OOK module with ENABLE pin):
 *    DATA_PIN   → DATA pin of transmitter
 *    ENABLE_PIN → ENABLE / VCC-switch pin of transmitter
 *
 *  Board: ESP32 Dev Module (or any ESP32 variant)
 *  Serial monitor baud rate: 115200
 ********************************************************************/

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEClient.h>
#include <BLESecurity.h>

// ---------------- RF pin assignments ----------------
const uint8_t DATA_PIN   = 26;  // GPIO26 — same side as VIN/3V3
const uint8_t ENABLE_PIN = 27;  // GPIO27 — same side as VIN/3V3

// ---------------- Collar configuration ----------------
#define TRANSMITTER_ID  0xB497

// -------------------------------------------------------
// CaiXianlin OOK Protocol Encoder
// -------------------------------------------------------
// Action codes
#define ACTION_SHOCK    1
#define ACTION_VIBRATE  2
#define ACTION_BEEP     3

// ---------------- Per-channel intensity levels (1–99) ----------------
static uint8_t levelCh1 = 1;
static uint8_t levelCh2 = 1;

static uint8_t clampLevel(int v) {
  if (v < 1)  return 1;
  if (v > 99) return 99;
  return (uint8_t)v;
}

// ---------------- RF command queue ----------------
// sendStimulus() blocks for ~440 ms (busy-wait bit-banging).
// Calling it directly from the BLE notification callback starves the
// BLE stack task and causes missed/delayed notifications.
// Instead, the callback posts a command here and loop() executes it.
struct RFCmd {
  uint16_t txId;
  uint8_t  channel;
  uint8_t  action;
  uint8_t  intensity;
};

static QueueHandle_t rfQueue = nullptr;
#define RF_QUEUE_LEN 8

void sendStimulus(uint16_t txId, uint8_t channel, uint8_t action,
                  uint8_t intensity, uint8_t repeats = 10) {

  if (channel > 2) channel = 2;
  if (action < 1 || action > 3) action = ACTION_SHOCK;
  if (intensity > 99) intensity = 99;
  if (action == ACTION_BEEP) intensity = 0;

  const char* actionName = (action == ACTION_SHOCK)   ? "SHOCK"
                         : (action == ACTION_VIBRATE) ? "VIBRATE"
                                                      : "BEEP";

  uint64_t payload = ((uint64_t)txId      << 24) |
                     ((uint64_t)channel   << 20) |
                     ((uint64_t)action    << 16) |
                     ((uint64_t)intensity <<  8);

  uint8_t checksum = 0;
  for (uint8_t i = 0; i < 5; i++) {
    checksum += (uint8_t)((payload >> (i * 8)) & 0xFF);
  }

  uint64_t packet = payload | (uint64_t)checksum;

  Serial.printf("[RF] >>> TX  txId=0x%04X  ch=%d  action=%s  intensity=%d"
                "  checksum=0x%02X  repeats=%d\n",
                txId, channel, actionName, intensity, checksum, repeats);
  Serial.printf("[RF]     packet bits (hex): %02X %02X %02X %02X %02X\n",
                (uint8_t)(packet >> 32), (uint8_t)(packet >> 24),
                (uint8_t)(packet >> 16), (uint8_t)(packet >>  8),
                (uint8_t)(packet));

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
    // End marker: two 0-bits
    digitalWrite(DATA_PIN, HIGH); delayMicroseconds(250);
    digitalWrite(DATA_PIN, LOW);  delayMicroseconds(750);
    digitalWrite(DATA_PIN, HIGH); delayMicroseconds(250);
    digitalWrite(DATA_PIN, LOW);  delayMicroseconds(750);
  }

  digitalWrite(DATA_PIN, LOW);
  digitalWrite(ENABLE_PIN, LOW);
  Serial.printf("[RF] <<< done (%d repeats sent)\n", repeats);
}

// -------------------------------------------------------
// BLE HID client
// -------------------------------------------------------
static BLEUUID hidServiceUUID("1812");
static BLEUUID reportCharUUID("2a4d");

static BLEAdvertisedDevice* targetDevice  = nullptr;
static BLEClient*           bleClient     = nullptr;
static bool                 doConnect     = false;
static bool                 connected     = false;
static unsigned long        disconnectTimeMs = 0;
static unsigned long        lastHeartbeatMs  = 0;

#define RECONNECT_DELAY_MS  3000
#define HEARTBEAT_MS        5000   // print status every 5 s

// ── BLE security (Just Works / bonding) ─────────────────
class SecurityCB : public BLESecurityCallbacks {
  uint32_t onPassKeyRequest() override {
    Serial.println("[SEC] Passkey requested from us — sending 0");
    return 0;
  }
  void onPassKeyNotify(uint32_t passkey) override {
    Serial.printf("[SEC] Passkey notify: %06lu\n", passkey);
  }
  bool onSecurityRequest() override {
    Serial.println("[SEC] Security request received — accepting");
    return true;
  }
  bool onConfirmPIN(uint32_t pin) override {
    Serial.printf("[SEC] Confirm PIN %06lu — accepting\n", pin);
    return true;
  }
  void onAuthenticationComplete(esp_ble_auth_cmpl_t auth) override {
    if (auth.success) {
      Serial.println("[SEC] Authentication + bonding OK");
    } else {
      Serial.printf("[SEC] Authentication FAILED  reason=0x%02X\n",
                    auth.fail_reason);
    }
  }
};

// ── Helper: enqueue an RF command (safe to call from BLE task) ───────
static void enqueueRF(uint8_t channel, uint8_t action, uint8_t intensity) {
  RFCmd cmd = { TRANSMITTER_ID, channel, action, intensity };
  if (xQueueSend(rfQueue, &cmd, 0) != pdTRUE) {
    Serial.println("[RF] queue full — command dropped");
  }
}

// ── HID report callback ───────────────────────────────────
// IMPORTANT: this runs in the BLE stack FreeRTOS task.
// Never call sendStimulus() here — it busy-waits for ~440 ms and
// starves the BLE stack, causing missed/delayed notifications.
// Instead, post to rfQueue and let loop() do the actual transmit.
void onHIDReport(BLERemoteCharacteristic* chr, uint8_t* data,
                 size_t len, bool isNotify) {
  // Print raw report bytes every time (key down AND key up)
  Serial.printf("[HID] report  handle=0x%04X  len=%d  bytes:",
                chr->getHandle(), len);
  for (size_t i = 0; i < len; i++) Serial.printf(" %02X", data[i]);
  Serial.printf("  (%s)\n", isNotify ? "notify" : "indicate");

  if (len < 3) {
    Serial.println("[HID] report too short — ignored");
    return;
  }

  uint8_t mod = data[0];
  uint8_t key = data[2];

  if (key == 0) {
    Serial.println("[HID] key release — ignored");
    return;
  }

  Serial.printf("[HID] key=0x%02X  mod=0x%02X\n", key, mod);

  switch (key) {

    case 0x24: case 0x5F:  // 7 / KP7 — shock ch1
      Serial.printf("[KEY] 7 → shock Ch1  level=%d\n", levelCh1);
      enqueueRF(0, ACTION_SHOCK, levelCh1);
      break;

    case 0x25: case 0x60:  // 8 / KP8 — shock ch2
      Serial.printf("[KEY] 8 → shock Ch2  level=%d\n", levelCh2);
      enqueueRF(1, ACTION_SHOCK, levelCh2);
      break;

    case 0x21: case 0x5C:  // 4 / KP4 — ch1 level +1
      levelCh1 = clampLevel((int)levelCh1 + 1);
      Serial.printf("[KEY] 4 → Ch1 level %d\n", levelCh1);
      break;

    case 0x22: case 0x5D:  // 5 / KP5 — ch2 level +1
      levelCh2 = clampLevel((int)levelCh2 + 1);
      Serial.printf("[KEY] 5 → Ch2 level %d\n", levelCh2);
      break;

    case 0x1E: case 0x59:  // 1 / KP1 — ch1 level -1
      levelCh1 = clampLevel((int)levelCh1 - 1);
      Serial.printf("[KEY] 1 → Ch1 level %d\n", levelCh1);
      break;

    case 0x1F: case 0x5A:  // 2 / KP2 — ch2 level -1
      levelCh2 = clampLevel((int)levelCh2 - 1);
      Serial.printf("[KEY] 2 → Ch2 level %d\n", levelCh2);
      break;

    case 0x23: case 0x5E:  // 6 / KP6 — test ch1
      Serial.printf("[KEY] 6 → test Ch1 (vibrate+beep)  level=%d\n", levelCh1);
      enqueueRF(0, ACTION_VIBRATE, levelCh1);
      enqueueRF(0, ACTION_BEEP, 0);
      break;

    case 0x20: case 0x5B:  // 3 / KP3 — test ch2
      Serial.printf("[KEY] 3 → test Ch2 (vibrate+beep)  level=%d\n", levelCh2);
      enqueueRF(1, ACTION_VIBRATE, levelCh2);
      enqueueRF(1, ACTION_BEEP, 0);
      break;

    case 0x57:             // KP+ — both +1
      levelCh1 = clampLevel((int)levelCh1 + 1);
      levelCh2 = clampLevel((int)levelCh2 + 1);
      Serial.printf("[KEY] KP+ → both +1  Ch1=%d Ch2=%d\n", levelCh1, levelCh2);
      break;

    case 0x2E:             // = key (+shift = +) — both +1
      if (mod & 0x22) {
        levelCh1 = clampLevel((int)levelCh1 + 1);
        levelCh2 = clampLevel((int)levelCh2 + 1);
        Serial.printf("[KEY] + → both +1  Ch1=%d Ch2=%d\n", levelCh1, levelCh2);
      } else {
        Serial.println("[KEY] = pressed (no shift) — ignored");
      }
      break;

    case 0x56: case 0x2D:  // KP- / row-minus — both -1
      levelCh1 = clampLevel((int)levelCh1 - 1);
      levelCh2 = clampLevel((int)levelCh2 - 1);
      Serial.printf("[KEY] - → both -1  Ch1=%d Ch2=%d\n", levelCh1, levelCh2);
      break;

    case 0x27: case 0x62:  // 0 / KP0 — reset
      levelCh1 = 1;
      levelCh2 = 1;
      Serial.println("[KEY] 0 → reset  Ch1=1 Ch2=1");
      break;

    default:
      Serial.printf("[KEY] unhandled key=0x%02X mod=0x%02X — no action\n",
                    key, mod);
      break;
  }
}

// ── Scan callback ─────────────────────────────────────────
class ScanCB : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice dev) override {
    Serial.printf("[SCAN] Device: addr=%s  rssi=%d  name=\"%s\"\n",
                  dev.getAddress().toString().c_str(),
                  dev.getRSSI(),
                  dev.haveName() ? dev.getName().c_str() : "(none)");

    if (dev.haveServiceUUID()) {
      Serial.printf("[SCAN]   advertised services:");
      for (int i = 0; i < (int)dev.getServiceUUIDCount(); i++) {
        Serial.printf(" %s", dev.getServiceUUID(i).toString().c_str());
      }
      Serial.println();
    } else {
      Serial.println("[SCAN]   no service UUIDs advertised");
    }

    if (dev.haveServiceUUID() &&
        dev.isAdvertisingService(hidServiceUUID)) {
      Serial.println("[SCAN]   *** HID device found! Stopping scan ***");
      targetDevice = new BLEAdvertisedDevice(dev);
      doConnect    = true;
      dev.getScan()->stop();
    }
  }
};

// ── Client callbacks ──────────────────────────────────────
class ClientCB : public BLEClientCallbacks {
  void onConnect(BLEClient* c) override {
    Serial.printf("[BLE] onConnect  conn_id=%d\n", c->getConnId());
  }
  void onDisconnect(BLEClient* c) override {
    Serial.printf("[BLE] onDisconnect  conn_id=%d — will retry in %d s\n",
                  c->getConnId(), RECONNECT_DELAY_MS / 1000);
    connected        = false;
    disconnectTimeMs = millis();
  }
};

// ── Connect and subscribe to HID report characteristics ──
bool connectToDevice() {
  Serial.printf("[BLE] Attempting connection to %s …\n",
                targetDevice->getAddress().toString().c_str());

  if (!bleClient) {
    Serial.println("[BLE] Creating BLE client");
    bleClient = BLEDevice::createClient();
    bleClient->setClientCallbacks(new ClientCB());
  } else {
    Serial.println("[BLE] Reusing existing BLE client");
  }

  if (!bleClient->connect(targetDevice)) {
    Serial.println("[BLE] connect() returned false — connection failed");
    return false;
  }
  Serial.printf("[BLE] TCP-level connect OK  conn_id=%d\n",
                bleClient->getConnId());
  Serial.println("[BLE] Requesting encryption (MITM) …");

  esp_bd_addr_t addr;
  memcpy(addr, targetDevice->getAddress().getNative(), 6);
  esp_ble_set_encryption(addr, ESP_BLE_SEC_ENCRYPT_MITM);
  Serial.println("[BLE] Waiting 2 s for pairing to complete …");
  delay(2000);

  Serial.println("[BLE] Looking up HID service (0x1812) …");
  BLERemoteService* svc = bleClient->getService(hidServiceUUID);
  if (!svc) {
    Serial.println("[BLE] HID service NOT found — disconnecting");
    bleClient->disconnect();
    return false;
  }
  Serial.println("[BLE] HID service found");

  Serial.println("[BLE] Enumerating characteristics:");
  auto* chars = svc->getCharacteristics();
  int subscribed = 0;
  for (auto& kv : *chars) {
    BLERemoteCharacteristic* c = kv.second;
    Serial.printf("[BLE]   uuid=%s  handle=0x%04X"
                  "  canNotify=%s  canRead=%s\n",
                  c->getUUID().toString().c_str(),
                  c->getHandle(),
                  c->canNotify() ? "yes" : "no",
                  c->canRead()   ? "yes" : "no");

    if (c->getUUID().equals(reportCharUUID) && c->canNotify()) {
      Serial.printf("[BLE]   -> subscribing to handle 0x%04X\n",
                    c->getHandle());
      c->registerForNotify(onHIDReport);
      subscribed++;
    }
  }

  if (subscribed == 0) {
    Serial.println("[BLE] No notifiable Report characteristics found — disconnecting");
    bleClient->disconnect();
    return false;
  }

  Serial.printf("[BLE] Ready — subscribed to %d report characteristic(s)\n",
                subscribed);
  return true;
}

// -------------------------------------------------------
// Setup & Loop
// -------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n\n========================================");
  Serial.println("  ESP32 BLE Keyboard → CaiXianlin Shocker");
  Serial.println("========================================");
  Serial.printf ("  Firmware built: %s %s\n", __DATE__, __TIME__);
  Serial.printf ("  TX ID:      0x%04X\n", TRANSMITTER_ID);
  Serial.printf ("  DATA_PIN:   GPIO%d\n", DATA_PIN);
  Serial.printf ("  ENABLE_PIN: GPIO%d\n", ENABLE_PIN);
  Serial.printf ("  Ch1 level:  %d\n", levelCh1);
  Serial.printf ("  Ch2 level:  %d\n", levelCh2);
  Serial.println("  Keys: 7/8=shock  4/5=lvl+  1/2=lvl-");
  Serial.println("        6/3=test   +/-=both   0=reset");
  Serial.println("========================================\n");

  rfQueue = xQueueCreate(RF_QUEUE_LEN, sizeof(RFCmd));
  Serial.println("[SETUP] RF command queue created");

  Serial.println("[SETUP] Configuring RF pins …");
  pinMode(DATA_PIN,   OUTPUT);
  pinMode(ENABLE_PIN, OUTPUT);
  digitalWrite(DATA_PIN,   LOW);
  digitalWrite(ENABLE_PIN, LOW);
  Serial.printf("[SETUP] GPIO%d (DATA)   = LOW\n", DATA_PIN);
  Serial.printf("[SETUP] GPIO%d (ENABLE) = LOW\n", ENABLE_PIN);

  Serial.println("[SETUP] Initialising BLE stack …");
  BLEDevice::init("ESP32-Shocker");
  Serial.printf("[SETUP] BLE device name: \"%s\"\n",
                BLEDevice::toString().c_str());

  Serial.println("[SETUP] Configuring BLE security (Just-Works bonding) …");
  BLEDevice::setSecurityCallbacks(new SecurityCB());
  BLESecurity* sec = new BLESecurity();
  sec->setAuthenticationMode(ESP_LE_AUTH_BOND);
  sec->setCapability(ESP_IO_CAP_NONE);
  sec->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  sec->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  Serial.println("[SETUP] Security configured");

  BLEScan* scan = BLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(new ScanCB());
  scan->setActiveScan(true);
  Serial.println("[SETUP] Starting BLE scan (30 s) — turn on your keyboard now …\n");
  scan->start(30, false);
  Serial.println("[SETUP] Scan window done. Entering loop.");
}

void loop() {
  // Drain RF command queue — execute here in the main task, not in the BLE task
  RFCmd cmd;
  while (xQueueReceive(rfQueue, &cmd, 0) == pdTRUE) {
    Serial.printf("[LOOP] Executing queued RF cmd  ch=%d  action=%d  intensity=%d\n",
                  cmd.channel, cmd.action, cmd.intensity);
    sendStimulus(cmd.txId, cmd.channel, cmd.action, cmd.intensity);
  }

  // Periodic heartbeat
  if (millis() - lastHeartbeatMs >= HEARTBEAT_MS) {
    lastHeartbeatMs = millis();
    Serial.printf("[LOOP] uptime=%lus  connected=%s  Ch1=%d  Ch2=%d\n",
                  millis() / 1000,
                  connected ? "YES" : "NO",
                  levelCh1, levelCh2);
    if (!connected && targetDevice == nullptr) {
      Serial.println("[LOOP] No device found yet — restart ESP to scan again");
    }
  }

  // Reconnect after disconnect
  if (!connected && !doConnect && targetDevice != nullptr) {
    unsigned long elapsed = millis() - disconnectTimeMs;
    if (elapsed >= RECONNECT_DELAY_MS) {
      Serial.printf("[LOOP] %.1f s since disconnect — attempting reconnect\n",
                    elapsed / 1000.0f);
      doConnect = true;
    }
  }

  if (doConnect && !connected) {
    Serial.println("[LOOP] doConnect flag set — calling connectToDevice()");
    connected = connectToDevice();
    doConnect  = false;
    if (connected) {
      Serial.println("[LOOP] connectToDevice() succeeded — now connected");
    } else {
      Serial.println("[LOOP] connectToDevice() failed — will retry");
      disconnectTimeMs = millis();
    }
  }
}
