// Pin assignments
const int GND_PIN = 3;  // Simulated GND
const int VCC_PIN = 4;  // Simulated VCC
const int DATA_PIN = 5;  // Data pin for transmission
const int ENABLE_PIN = 6; // Enable pin for transmitter

// RAW signal timings in microseconds (HIGH, LOW, HIGH, LOW...)
const int rawSignal[] = {1363, -784, 227, -806, 193, -796, 711, -310, 691, -318, 695, -296, 737, -258, 215, -798, 227, -794, 195, -822, 195, -790, 739, -278, 729, -282, 727, -292, 725, -260, 737, -308, 689, -316, 215, -792, 205, -796, 195, -802, 225, -794, 195, -822, 193, -792, 739, -278, 733, -282, 215, -778, 229, -774, 227, -794, 195, -822, 193, -794, 235, -770, 219, -800, 207, -824, 205, -794, 723, -310, 689, -282, 729, -300, 709, -260, 737, -310, 689, -314, 215, -792, 205, -796, 195, -798, 225, -1026, 1363, -788, 229, -808, 195, -798, 711, -312, 691, -320, 711, -266, 741, -258, 215, -796, 227, -794, 225, -792, 195, -824, 703, -284, 731, -278, 729, -284, 715, -292, 733, -290, 705, -294, 213, -794, 193, -824, 193, -792, 227, -790, 225, -794, 193, -796, 717, -316, 715, -258, 225, -810, 195, -800, 237, -790, 193, -826, 193, -794, 223, -798, 189, -798, 245, -786, 205, -794, 721, -310, 689, -284, 731, -302, 709, -258, 737, -292, 731, -272, 215, -810, 211, -792, 199, -814, 195, -1688};
const int rawSignalSize = sizeof(rawSignal) / sizeof(rawSignal[0]);

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
  sendRawSignal();
  delay(2000); // Wait before sending again
}

void sendRawSignal() {
  for (int i = 0; i < rawSignalSize; i++) {
    if (rawSignal[i] > 0) {
      digitalWrite(DATA_PIN, HIGH);
    } else {
      digitalWrite(DATA_PIN, LOW);
    }
    delayMicroseconds(abs(rawSignal[i])); // Duration of HIGH or LOW state
  }
  
  // Ensure the signal is off after sending
  digitalWrite(DATA_PIN, LOW);
}
