#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

BLECharacteristic *pCharacteristicTX;
BLECharacteristic *pCharacteristicRX;
bool deviceConnected = false;
int txValue = 0;

#define SERVICE_UUID "35e2384d-09ba-40ec-8cc2-a491e7bcd763"
#define CHARACTERISTIC__UUID_TX "a9248655-7f1b-4e18-bf36-ad1ee859983f"
#define CHARACTERISTIC__UUID_RX "9d5cb5f2-5eb2-4b7c-a5d4-21e61c9c6f36"

//=============================HANDLE_DATA============================================

void handleData(std::string rxValue)
{
// Example: Turn LED on/off based on received value
      if(rxValue.find("1") != std::string::npos)
      {
        Serial.println("Turning LED ON");
        digitalWrite(22, LOW);
      }
      else if (rxValue.find("0") != std::string::npos)
      {
        Serial.println("Turning LED OFF");
        digitalWrite(22, HIGH);
      }
}

//=================================CALLBACKS==========================================

class MyServerCallbacks: public BLEServerCallbacks 
{
  void onConnect(BLEServer* pServer) override
  { 
    deviceConnected = true; 
    Serial.println("Client connected");
  }

  void onDisconnect(BLEServer* pServer) override
  { 
    deviceConnected = false; 
    Serial.println("Client disconnected");
    txValue = 0;
    digitalWrite(22, HIGH);
    pServer->getAdvertising()->start();
    Serial.println("Advertising started");
  }
};

class MyCallbacks: public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *pCharacteristicRX) override 
  {
    std::string rxValue = pCharacteristicRX->getValue();
    Serial.print("onWrite callback triggered with value: ");

    if (rxValue.length() > 0)
    {
      Serial.print("Received Value: ");
      for (char c : rxValue)
      {
        Serial.print(c);
      }
      Serial.println();

      handleData(rxValue); //seperate function at the top for handling received data. 
    }
    else
    {
      Serial.println("Received empty value");
    }
  }
};

//=================================SETUP==============================================

void setup() 
{
  Serial.begin(9600);
  pinMode(22, OUTPUT);
  digitalWrite(22, HIGH);

  // Create the BLE device
  BLEDevice::init("ESP32");

  // Create the BLE server
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create the BLE service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Create TX characteristic
  pCharacteristicTX = pService->createCharacteristic(
                                      CHARACTERISTIC__UUID_TX,
                                      BLECharacteristic::PROPERTY_NOTIFY
                                    );

  // BLE2902 needed to notify
  pCharacteristicTX->addDescriptor(new BLE2902());

  // Create RX characteristic
  pCharacteristicRX = pService->createCharacteristic(
                                      CHARACTERISTIC__UUID_RX,
                                      BLECharacteristic::PROPERTY_WRITE
                                    );
  
  pCharacteristicRX->setCallbacks(new MyCallbacks());
  
  // Start the service
  pService->start();

  // Start advertising
  pServer->getAdvertising()->start();
  Serial.println("Waiting for a client connection to notify...");
}

//=================================LOOP===============================================

void loop()  //Example of sending data.
{
  if(deviceConnected)
  {
    txValue++;

    // Conversion of txValue
    char txString[8];
    dtostrf(txValue, 1, 2, txString);

    // Setting the value to the characteristic
    pCharacteristicTX->setValue(txString);
  
    // Notifying the connected client
    pCharacteristicTX->notify();
    Serial.println("Sent value: " + String(txString));
    delay(500);
  }
  else
  {
    Serial.println("Device not connected");
    delay(1000);
  }
}
