// Pin assignments
const int GND_PIN = 3;  // Simulated GND
const int VCC_PIN = 4;  // Simulated VCC
const int DATA_PIN = 5;  // Data pin for transmission
const int ENABLE_PIN = 6; // Enable pin for transmitter

// Message to send
const char message[] = "101000111101000111101000111101000111";
const int bitDuration = 500; // Duration of each bit in microseconds

void setup() {
  pinMode(GND_PIN, OUTPUT);
  pinMode(VCC_PIN, OUTPUT);
  pinMode(ENABLE_PIN, OUTPUT);
  pinMode(DATA_PIN, OUTPUT);
  
  // Simulate power to the module
  digitalWrite(GND_PIN, LOW);
  digitalWrite(VCC_PIN, HIGH);
  digitalWrite(ENABLE_PIN, HIGH); // Enable the transmitter
}

void loop() {
  sendMessage(message);
  delay(2000); // Wait before sending again
}

void sendMessage(const char *msg) {
  for (int i = 0; msg[i] != '\0'; i++) {
    if (msg[i] == '1') {
      digitalWrite(DATA_PIN, HIGH);
    } else {
      digitalWrite(DATA_PIN, LOW);
    }
    delayMicroseconds(bitDuration);
  }
  
  // Ensure the signal is off after sending
  digitalWrite(DATA_PIN, LOW);
}
