#include <Wire.h>
#include <DFRobot_MAX30102.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// --- Ρυθμίσεις Αισθητήρα DFRobot ---
DFRobot_MAX30102 particleSensor;

// --- Ρυθμίσεις BLE GATT ---
BLEServer* pServer = NULL;
BLECharacteristic* pHeartRateChar = NULL;
BLECharacteristic* pSpO2Char = NULL;
bool deviceConnected = false;

#define HEART_RATE_SERVICE_UUID "0000180d-0000-1000-8000-00805f9b34fb" //"180D"
#define HEART_RATE_CHAR_UUID   "00002a37-0000-1000-8000-00805f9b34fb" // "2A37"

#define SPO2_SERVICE_UUID       "00001822-0000-1000-8000-00805f9b34fb" //"1822" 
#define SPO2_CHAR_UUID          "00002a5f-0000-1000-8000-00805f9b34fb" //"2A5E" 

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("Εφαρμογή συνδέθηκε!");
    };
    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("Εφαρμογή αποσυνδέθηκε! Επανεκκίνηση εκπομπής...");
      pServer->startAdvertising();
    }
};
void createSfloat(int value, uint8_t* byte_array) {
    // Εκθέτης (Exponent) είναι 0, οπότε τα 4 ανώτερα bits θα είναι 0.
    // Επειδή είναι Little Endian, σπάμε τον 16-bit αριθμό στα δύο:
    byte_array[0] = value & 0xFF;         // Το κάτω Byte (LSB)
    byte_array[1] = (value >> 8) & 0x0F;  // Το πάνω Byte (MSB) + 0 στον εκθέτη
}
void setup() {
  Serial.begin(9600);
  
  // 1. Αρχικοποίηση DFRobot MAX30102
  Serial.println("Αρχικοποίηση DFRobot MAX30102...");
  
  // Περιμένει μέχρι να βρει τον αισθητήρα στο I2C
  while (!particleSensor.begin()) {
    Serial.println("Ο αισθητήρας MAX30102 δεν βρέθηκε. Έλεγξε τα καλώδια!");
    delay(1000);
  }
  
  // Ρύθμιση του αισθητήρα (Led brightness, sample rate κλπ)
  particleSensor.sensorConfiguration(/*ledBrightness=*/50, /*sampleAverage=*/SAMPLEAVG_4, \
                                     /*ledMode=*/MODE_MULTILED, /*sampleRate=*/SAMPLERATE_100, \
                                     /*pulseWidth=*/PULSEWIDTH_411, /*adcRange=*/ADCRANGE_16384);

  // 2. Αρχικοποίηση BLE
  Serial.println("Αρχικοποίηση BLE Server...");
  BLEDevice::init("TTGO_Health");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Στήσιμο Heart Rate Service
  BLEService *pHeartRateService = pServer->createService(HEART_RATE_SERVICE_UUID);
  pHeartRateChar = pHeartRateService->createCharacteristic(
                     HEART_RATE_CHAR_UUID,
                     BLECharacteristic::PROPERTY_NOTIFY
                   );
  pHeartRateChar->addDescriptor(new BLE2902());
  pHeartRateService->start();

  // Στήσιμο SpO2 Service
  BLEService *pSpO2Service = pServer->createService(SPO2_SERVICE_UUID);
  pSpO2Char = pSpO2Service->createCharacteristic(
                SPO2_CHAR_UUID,
                BLECharacteristic::PROPERTY_NOTIFY
              );
  pSpO2Char->addDescriptor(new BLE2902());
  pSpO2Service->start();

  // 3. Εκκίνηση Advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(HEART_RATE_SERVICE_UUID);
  pAdvertising->addServiceUUID(SPO2_SERVICE_UUID);
  BLEDevice::startAdvertising();
  
  Serial.println("Έτοιμο! Βάλε το δάχτυλό σου στον αισθητήρα.");
}
  int32_t heartRate;      // Εδώ θα αποθηκευτούν οι παλμοί
  int8_t validHeartRate;  // Γίνεται 1 (true) αν η μέτρηση είναι αξιόπιστη
  int32_t spo2;           // Εδώ θα αποθηκευτεί το Οξυγόνο
  int8_t validSPO2;       // Γίνεται 1 (true) αν η μέτρηση είναι αξιόπιστη

void loop() {
  // Αυτή η εντολή διαβάζει τον αισθητήρα και κάνει τους υπολογισμούς
  particleSensor.heartrateAndOxygenSaturation(&spo2, &validSPO2,&heartRate, &validHeartRate);

  // Αν έχουμε αξιόπιστη μέτρηση (και τα δύο valid είναι 1)
  if (validHeartRate && validSPO2) {
      
      Serial.print("BPM: "); Serial.print(heartRate);
      Serial.print(" | SpO2: "); Serial.print(spo2); Serial.println("%");

      // Αν το κινητό είναι συνδεδεμένο μέσω Bluetooth, στείλε τα δεδομένα
      if (deviceConnected) {
          // -------------------------------------------------------------
          // 1. ΑΠΟΣΤΟΛΗ ΣΤΟ ΚΛΑΣΙΚΟ HEART RATE SERVICE (180D)
          // -------------------------------------------------------------
          // Το HRP απαιτεί ένα απλό byte array 2 θέσεων (Flags + BPM)
          /*
          uint8_t hrPayload[2];
          hrPayload[0] = 0x00; // Flags: 8-bit format, sensor contact not supported
          hrPayload[1] = (uint8_t)heartRate; // Η τιμή του παλμού
          
          pHeartRateChar->setValue(hrPayload, 2);
          pHeartRateChar->notify(); // << ΕΔΩ ΣΤΕΛΝΟΝΤΑΙ ΟΙ ΠΑΛΜΟΙ!*/

          // -------------------------------------------------------------
          // 2. ΑΠΟΣΤΟΛΗ ΣΤΟ ΠΡΟΦΙΛ ΟΞΥΜΕΤΡΟΥ (1822)
          // -------------------------------------------------------------
          // Το PLXP απαιτεί πίνακα 5 bytes (Flags + SpO2(SFLOAT) + PR(SFLOAT))
          uint8_t plxPayload[5];
          plxPayload[0] = 0x00; // Flags: Ακολουθούν SpO2 και PR

          createSfloat(spo2, &plxPayload[1]);      // Bytes 1 & 2
          createSfloat(heartRate, &plxPayload[3]); // Bytes 3 & 4

          pSpO2Char->setValue(plxPayload, 5);
          pSpO2Char->notify(); // << ΕΔΩ ΣΤΕΛΝΕΤΑΙ ΤΟ ΟΞΥΓΟΝΟ ΚΑΙ ΟΙ ΠΑΛΜΟΙ ΜΑΖΙ!
        }
      
      // Μικρή παύση όταν έχουμε βρει παλμό, για να μη "βομβαρδίζουμε" το Bluetooth
      delay(1000); 
  }
}