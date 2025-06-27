/********************************************************************
 *  ESP8266 RF-TRANSMITTER + TELEGRAM BOT
 *  ---------------------------------------------------------------
 *  Uses:      - ESP8266WiFi library   (built-in)
 *             - WiFiClientSecure
 *             - UniversalTelegramBot (github.com/witnessmenow/Universal-Arduino-Telegram-Bot)
 *
 *  Board:     NodeMCU 1.0 (ESP-12E module) 80 MHz / 4 M SPIFFS
 ********************************************************************/
#include <Arduino.h>
#include <ESP8266WiFi.h>              // *** NEW
#include <WiFiClientSecure.h>         // *** NEW
#include <UniversalTelegramBot.h>     // *** NEW
#include <pgmspace.h>

#include <EEPROM.h>               // *** NEW – simple KV storage
#include <ESP8266WebServer.h>     // *** NEW – tiny HTTP server

// ---------------- Pin assignments ----------------
const uint8_t DATA_PIN   = 5;  // D1
const uint8_t ENABLE_PIN = 4;  // D2

// ---------------- Wi-Fi & Telegram credentials ----
//const char* WIFI_SSID     = "Internet";       // *** NEW
//const char* WIFI_PASSWORD = "1nt3rn3t";   // *** NEW

const char* BOT_TOKEN = "7730578439:AAHMcMsn6uOFxHZzYb1dxCDllRwg8Dd3XMw";          // *** NEW  (keep private!)
//const char* CHAT_ID   = "987654321";                // *** NEW  (your personal chat)

// *** NEW --------------------------------------------------------------------
#define MAX_CREDENTIALS 5
#define EEPROM_SIZE     512       // enough for 5×(32+64) chars + header

struct WiFiCred {                 // fixed-length to keep offsets simple
  char ssid[32];
  char pass[64];
};

WiFiCred creds[MAX_CREDENTIALS];
uint8_t  credCount = 0;

ESP8266WebServer server(80);      // captive portal
// ---------------------------------------------------------------------------

// ---------------- Pulse tables ----------------
const int16_t vibSignal[] PROGMEM = {1345,-804,235,-786,193,-798,747,-276,727,-286,709,-302,711,-288,219,-798,193,-794,227,-792,223,-792,709,-312,693,-284,747,-264,737,-258,733,-288,731,-286,213,-792,225,-792,225,-790,193,-790,225,-790,227,-790,739,-278,217,-794,207,-800,195,-800,225,-792,709,-312,217,-778,721,-294,229,-778,195,-798,745,-274,217,-798,211,-784,725,-308,217,-768,219,-800,209,-792,739,-258,213,-828,193,-796,225};
const int16_t sonSignal[] PROGMEM = {1363, -784, 227, -806, 193, -796, 711, -310, 691, -318, 695, -296, 737, -258, 215, -798, 227, -794, 195, -822, 195, -790, 739, -278, 729, -282, 727, -292, 725, -260, 737, -308, 689, -316, 215, -792, 205, -796, 195, -802, 225, -794, 195, -822, 193, -792, 739, -278, 733, -282, 215, -778, 229, -774, 227, -794, 195, -822, 193, -794, 235, -770, 219, -800, 207, -824, 205, -794, 723, -310, 689, -282, 729, -300, 709, -260, 737, -310, 689, -314, 215, -792, 205, -796, 195, -798, 225, -1026, 1363, -788, 229, -808, 195, -798, 711, -312, 691, -320, 711, -266, 741, -258, 215, -796, 227, -794, 225, -792, 195, -824, 703, -284, 731, -278, 729, -284, 715, -292, 733, -290, 705, -294, 213, -794, 193, -824, 193, -792, 227, -790, 225, -794, 193, -796, 717, -316, 715, -258, 225, -810, 195, -800, 237, -790, 193, -826, 193, -794, 223, -798, 189, -798, 245, -786, 205, -794, 721, -310, 689, -284, 731, -302, 709, -258, 737, -292, 731, -272, 215, -810, 211, -792, 199, -814, 195, -1688};
const int16_t nivel1Signal[] PROGMEM = {1333, -822, 199, -814, 193, -796, 711, -310, 723, -286, 697, -296, 739, -258, 215, -826, 193, -796, 225, -792, 195, -822, 707, -312, 691, -320, 711, -262, 739, -258, 737, -292, 731, -276, 215, -798, 213, -782, 231, -806, 193, -796, 225, -792, 195, -820, 195, -792, 743, -278, 215, -784, 245, -772, 229, -770, 227, -794, 223, -794, 203, -800, 205, -794, 713, -292, 221, -806, 715, -310, 689, -282, 739, -276, 719, -290, 713, -292, 213, -794, 747, -276, 215, -796, 211, -782, 229, -1172, 1341, -786, 245, -774, 227, -804, 715, -274, 725, -284, 729, -298, 709, -290, 183, -832, 193, -796, 225, -792, 195, -820, 709, -314, 691, -320, 677, -296, 737, -258, 737, -294, 727, -288, 211, -790, 225, -792, 193, -790, 227, -790, 227, -792, 225, -792, 193, -792, 737, -284, 217, -794, 201, -816, 193, -798, 225, -792, 195, -822, 195, -790, 225, -792, 741, -280, 215, -786, 711, -290, 715, -292, 731, -310, 689, -318, 699, -312, 213, -772, 723, -310, 215, -764, 211, -786, 231, -1172};
const int16_t nivel10Signal[] PROGMEM = {1337, -812, 207, -802, 195, -802, 715, -308, 725, -282, 705, -304, 715, -290, 221, -802, 195, -798, 225, -794, 195, -824, 709, -312, 691, -318, 679, -294, 735, -290, 703, -290, 731, -288, 213, -792, 195, -824, 193, -824, 195, -790, 197, -824, 193, -826, 193, -796, 703, -318, 213, -792, 199, -814, 195, -798, 225, -796, 709, -312, 213, -794, 683, -324, 191, -810, 721, -308, 179, -798, 209, -818, 197, -812, 193, -798, 711, -288, 731, -312, 177, -816, 211, -810, 197, -810, 195, -1160, 1341, -816, 205, -802, 195, -804, 715, -310, 691, -318, 705, -304, 711, -290, 221, -802, 193, -798, 195, -826, 193, -826, 707, -312, 693, -318, 691, -290, 695, -322, 705, -308, 689, -316, 213, -792, 205, -798, 195, -804, 225, -796, 195, -824, 193, -792, 225, -794, 707, -314, 213, -792, 205, -794, 195, -836, 193, -796, 711, -312, 213, -778, 709, -294, 199, -822, 719, -310, 177, -798, 209, -818, 197, -814, 195, -800, 713, -310, 693, -316, 213, -792, 197, -814, 193, -798, 195, -1056};
const int16_t nivel30Signal[] PROGMEM = {1399, -754, 229, -806, 195, -794, 745, -278, 723, -288, 709, -302, 715, -258, 245, -796, 225, -764, 225, -790, 225, -788, 743, -278, 725, -288, 709, -266, 747, -258, 731, -292, 729, -274, 255, -768, 215, -800, 213, -766, 217, -798, 215, -798, 249, -758, 229, -770, 749, -276, 217, -800, 213, -782, 229, -768, 749, -276, 725, -288, 707, -304, 717, -258, 245, -796, 721, -274, 255, -764, 213, -786, 727, -312, 689, -284, 253, -760, 741, -258, 213, -790, 227, -792, 225, -790, 223, -1284, 1361, -780, 229, -770, 227, -792, 745, -276, 725, -288, 709, -304, 717, -258, 245, -762, 225, -794, 225, -790, 225, -788, 743, -278, 723, -290, 709, -300, 711, -258, 733, -292, 731, -276, 253, -766, 211, -790, 227, -768, 225, -794, 225, -788, 225, -788, 225, -790, 707, -278, 253, -748, 245, -780, 229, -766, 743, -284, 729, -274, 727, -286, 741, -274, 211, -798, 719, -286, 211, -790, 225, -790, 707, -312, 727, -288, 219, -762, 725, -310, 217, -768, 245, -758, 237, -790, 195, -1158};
const int16_t nivel60Signal[] PROGMEM = {1373, -778, 245, -772, 229, -772, 713, -308, 725, -284, 707, -304, 717, -290, 217, -802, 195, -796, 227, -792, 195, -822, 707, -312, 693, -318, 677, -298, 735, -290, 705, -288, 733, -310, 179, -808, 185, -832, 185, -800, 245, -788, 199, -814, 193, -800, 201, -796, 737, -284, 215, -792, 201, -816, 717, -274, 723, -280, 217, -796, 715, -318, 693, -294, 737, -290, 1388, -666, 263, -778, 231, -772, 225, -796, 709, -312, 691, -318, 711, -300, 711, -260, 737, -308, 693, -316, 215, -786, 205, -794, 195, -804, 227, -798, 201, -802, 203, -794, 243, -772, 723, -310, 215, -772, 209, -792, 739, -288, 703, -288, 735, -274, 723, -318, 215, -756, 237, -790, 719, -310, 215, -762, 711, -296, 735, -258, 735, -288, 213, -792, 225, -794, 225, -792, 193, -824, 195, -792, 235, -1266};
const int16_t nivel90Signal[] PROGMEM = {1377, -784, 241, -770, 229, -772, 717, -310, 725, -286, 695, -294, 741, -258, 215, -828, 193, -796, 225, -796, 201, -798, 703, -320, 693, -296, 737, -288, 705, -294, 731, -254, 729, -310, 215, -798, 211, -778, 229, -808, 193, -798, 193, -824, 193, -824, 193, -792, 739, -280, 213, -796, 709, -290, 713, -294, 213, -792, 227, -794, 223, -792, 709, -312, 693, -318, 709, -264, 741, -288, 217, -798, 717, -274, 725, -282, 731, -300, 709, -290, 707, -294, 213, -794, 225, -794, 203, -1066, 1345, -786, 209, -792, 237, -792, 721, -274, 723, -282, 739, -276, 717, -290, 219, -804, 195, -798, 237, -790, 195, -796, 717, -320, 711, -264, 739, -260, 735, -292, 733, -274, 723, -280, 217, -794, 225, -796, 189, -830, 187, -800, 209, -824, 205, -792, 195, -802, 713, -310, 215, -798, 717, -258, 755, -260, 213, -828, 193, -796, 225, -792, 709, -310, 691, -318, 711, -300, 709, -288, 185, -830, 717, -274, 723, -318, 701, -274, 721, -292, 711, -292, 213, -794, 225, -794, 195, -1184};


// *** NEW -------------------------------------------------
void loadCreds() {
  EEPROM.begin(EEPROM_SIZE);
  credCount = EEPROM.read(0);
  if (credCount > MAX_CREDENTIALS) credCount = 0;            // sanity
  for (uint8_t i = 0; i < credCount; i++) {
    EEPROM.get(1 + i*sizeof(WiFiCred), creds[i]);
  }
}

void saveCred(const char *s, const char *p) {
  if (credCount >= MAX_CREDENTIALS) return;                  // full
  strncpy(creds[credCount].ssid,  s, sizeof(creds[0].ssid));
  strncpy(creds[credCount].pass,  p, sizeof(creds[0].pass));
  EEPROM.put(1 + credCount*sizeof(WiFiCred), creds[credCount]);
  credCount++;
  EEPROM.write(0, credCount);
  EEPROM.commit();
}
// ---------------------------------------------------------

// *** NEW -------------------------------------------------
bool connectToKnown() {
  Serial.println(F("Scanning-and-connecting…"));
  for (uint8_t i = 0; i < credCount; i++) {
    Serial.printf("  trying %s … ", creds[i].ssid);
    WiFi.begin(creds[i].ssid, creds[i].pass);
    for (uint8_t j = 0; j < 30 && WiFi.status()!=WL_CONNECTED; j++) delay(333);
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("OK");
      return true;
    }
    Serial.println("fail");
  }
  return false;               // none worked
}

void startConfigPortal() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("RF_Bot_Config", "rfbot123");
  IPAddress ip = WiFi.softAPIP();
  Serial.printf("Config AP up – connect to %s and browse to http://%s/\n",
                "RF_Bot_Config", ip.toString().c_str());

  // ---- minimal form page ----
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html",
      "<form method='POST' action=\"/save\">"
      "SSID:<br><input name='s'><br>"
      "PASS:<br><input name='p' type='password'><br><br>"
      "<input type='submit' value='Save & Reboot'></form>");
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
// ---------------------------------------------------------


// ------------- RF helper -------------------------
template<size_t N>
void sendSignal(const int16_t (&signal)[N], uint8_t repeats = 10) {
  digitalWrite(ENABLE_PIN, HIGH);            // power the TX
  for (uint8_t r = 0; r < repeats; r++) {
    for (size_t i = 0; i < N; i++) {
      int16_t v = pgm_read_word_near(signal + i);
      digitalWrite(DATA_PIN, v > 0 ? HIGH : LOW);
      delayMicroseconds(abs(v));
    }
  }
  digitalWrite(DATA_PIN, LOW);
  digitalWrite(ENABLE_PIN, LOW);
}

// ------------- Telegram objects ------------------
// (WiFiClientSecure is mandatory – Telegram uses HTTPS)
WiFiClientSecure tgClient;                   // *** NEW
UniversalTelegramBot bot(BOT_TOKEN, tgClient); // *** NEW
unsigned long lastCheck = 0;                 // *** NEW
const unsigned POLL_INTERVAL = 2000;         // ms  // *** NEW

// ------------- SETUP -----------------------------
void setup() {
  pinMode(DATA_PIN, OUTPUT);
  pinMode(ENABLE_PIN, OUTPUT);
  Serial.begin(115200);
  delay(100);

  loadCreds();                       // **** NEW
  WiFi.mode(WIFI_STA);               // try station first
  if (!connectToKnown()) {
    startConfigPortal();             // open AP if nothing connects
  } else {
    tgClient.setInsecure();          // (Telegram TLS you already had)
  }

  Serial.println("ESP ready");
}

// ------------- LOOP ------------------------------
void loop() {
  server.handleClient();             // *** NEW (cheap when not in AP mode)
  unsigned long now = millis();

  // --- Poll Telegram every POLL_INTERVAL ms ------ // *** NEW
  if (now - lastCheck >= POLL_INTERVAL) {
    int numNewMsgs = bot.getUpdates(bot.last_message_received + 1);
    while (numNewMsgs) {
      handleMessages(numNewMsgs);
      numNewMsgs = bot.getUpdates(bot.last_message_received + 1);
    }
    lastCheck = now;
  }
}

// ----------- Command parser ---------------------- // *** NEW
void handleMessages(int numMsgs) {
  for (int i = 0; i < numMsgs; i++) {
    String chat_id = bot.messages[i].chat_id;
    String text    = bot.messages[i].text;

    // (Security) – ignore messages from other chats
    //if (chat_id != CHAT_ID) continue;

    text.toLowerCase();

    if (text == "/start") {
      bot.sendMessage(chat_id,
        "⚡ Elektra bot lista ⚡.\n\nComandos\n"
        " • /vib\n"
        " • /son\n"
        " • /nivel1 /nivel10 /nivel30 /nivel60 /nivel90", "");
    }
    else if (text == "/vib") {
      sendSignal(vibSignal);
      bot.sendMessage(chat_id, "⚠️ - Vibración activada 📳", "");
    }
    else if (text == "/son") {
      sendSignal(sonSignal);
      bot.sendMessage(chat_id, "⚠️ - Alarma activada 📢", "");
    }

    else if (text.equalsIgnoreCase("/nivel1")){
      sendSignal(nivel1Signal);
      bot.sendMessage(chat_id, "⚠️ - Estimulo eléctrico nivel 1 activado ⚡", "");
    }  
    else if (text.equalsIgnoreCase("/nivel10")){
      sendSignal(nivel10Signal);
      bot.sendMessage(chat_id, "⚠️ - Estimulo eléctrico nivel 10 activado ⚡", "");
    }  
    else if (text.equalsIgnoreCase("/nivel30")){
      sendSignal(nivel30Signal);
      bot.sendMessage(chat_id, "⚠️ - Estimulo eléctrico nivel 30 activado ⚡", "");
    }  
    else if (text.equalsIgnoreCase("/nivel60")){
      sendSignal(nivel60Signal);
      bot.sendMessage(chat_id, "⚠️ - Estimulo eléctrico nivel 60 activado ⚡", "");
    }  

    else if (text.equalsIgnoreCase("/nivel90")){
      sendSignal(nivel90Signal);
      bot.sendMessage(chat_id, "⚠️ - Estimulo eléctrico nivel 90 activado ⚡", "");
    }  

  }
}
