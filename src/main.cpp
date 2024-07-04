#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

BLECharacteristic *pCharacteristicTX;
BLECharacteristic *pCharacteristicRX;
BLECharacteristic *pCharacteristicAuth;
BLEServer* pServer;  // Declare a global variable to hold the BLEServer instance
bool deviceConnected = false;
bool authenticated = false;
int txValue = 0;
unsigned long authStartMillis = 0;  // Variable to store the start time of authentication attempt
bool authTimeout = false;  // Flag to indicate if authentication timeout has occurred

#define SERVICE_UUID "35e2384d-09ba-40ec-8cc2-a491e7bcd763"
#define CHARACTERISTIC__UUID_TX "a9248655-7f1b-4e18-bf36-ad1ee859983f"
#define CHARACTERISTIC__UUID_RX "9d5cb5f2-5eb2-4b7c-a5d4-21e61c9c6f36"
#define CHARACTERISTIC__UUID_AUTH "e58b4b34-daa6-4a79-8a4c-50d63e6e767f"
#define AUTH_KEY "your_auth_key"  // Replace with your specific key //possibly solves simple ble brute force pairing hacks by disgarding them.

const unsigned long AUTH_TIMEOUT_MS = 2000;  // Timeout period in milliseconds (e.g., 10 seconds)
const unsigned long CHECK_INTERVAL_MS = 1000;  // Interval for checking authentication status


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

//=============================START:ADVERTISING======================================

void startAdvertising() 
{
 // Clear existing advertising data
  BLEAdvertisementData advData;
  advData.setAppearance(0);  // Set appearance to 0 (undefined)
  advData.setCompleteServices(BLEUUID(SERVICE_UUID));  // Add the service UUID to advertising

  // Set the advertisement data
  pServer->getAdvertising()->setAdvertisementData(advData);

  // Set scan response data (optional) //!DOSNT WORK WITH SAMSUNG PHONES
  //BLEAdvertisementData scanResponseData;
  //scanResponseData.setName("");  // Clear the local name from scan response (optional)
  //pServer->getAdvertising()->setScanResponseData(scanResponseData);

  // Start advertising
  pServer->getAdvertising()->start();  
  Serial.println("Advertising started");
}

//=================================CALLBACKS==========================================

class MyServerCallbacks: public BLEServerCallbacks 
{
  void onConnect(BLEServer* server) override
  { 
    pServer = server;  // Assign the server reference to the global variable
    deviceConnected = true; 
    authenticated = false; // Reset authenticated status on new connection
    authStartMillis = millis();  // Start timing authentication attempt
    authTimeout = false;  // Reset timeout flag
    Serial.println("Client connected");
  }

  void onDisconnect(BLEServer* server) override
  { 
    deviceConnected = false; 
    Serial.println("Client disconnected");
    txValue = 0;
    digitalWrite(22, HIGH);

    startAdvertising();
  }
};

class MyAuthCallbacks: public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *pCharacteristicAuth) override 
  {
    std::string authValue = pCharacteristicAuth->getValue();
    Serial.print("Auth callback triggered with value: ");

    if (authValue.length() > 0)
    {
      Serial.print("Received Value: ");
      for (char c : authValue)
      {
        Serial.print(c);
      }
      Serial.println();

      if (authValue == AUTH_KEY)
      {
        authenticated = true;
        Serial.println("Client authenticated successfully");
      }
      else
      {
        authenticated = false;
        Serial.println("Authentication failed");
      }
    }
    else
    {
      Serial.println("Received empty auth value");
    }
  }
};

class MyCallbacks: public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *pCharacteristicRX) override 
  {
    if (!authenticated)
    {
      Serial.println("Unauthorized access attempt");
      return;
    }

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

      handleData(rxValue); // Separate function at the top for handling received data. 
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

  // Get the BLE address and print it
  BLEAddress bleAddress = BLEDevice::getAddress();
  std::string address = bleAddress.toString();  // Get the BLE address as a string
  // Convert address to uppercase
  std::transform(address.begin(), address.end(), address.begin(), [](unsigned char c){ return std::toupper(c); });
  // Print the uppercase address
  Serial.println("\n\n");
  Serial.println("BLE Address (Uppercase): ");
  Serial.println(address.c_str());
  Serial.println("");

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

  // Create Auth characteristic
  pCharacteristicAuth = pService->createCharacteristic(
                                      CHARACTERISTIC__UUID_AUTH,
                                      BLECharacteristic::PROPERTY_WRITE
                                    );

  pCharacteristicAuth->setCallbacks(new MyAuthCallbacks());

  // Start the service
  pService->start();

  startAdvertising();

  Serial.println("Waiting for a client connection to notify...");
}

//=================================LOOP===============================================

void loop()  // Example of sending data.
{
  if (deviceConnected && !authenticated)
  {
    // Check if authentication timeout has occurred
    if (millis() - authStartMillis >= AUTH_TIMEOUT_MS)
    {
      authTimeout = true;
      Serial.println("Authentication timeout");
      if (pServer->getConnectedCount() > 0)
      {       
        for (size_t i = 0; i < pServer->getConnectedCount(); i++)
        {
          pServer->disconnect(i);
        }        
      }
    }
  }

  if(deviceConnected && authenticated)
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
    Serial.println("Device not connected or not authenticated");
    delay(CHECK_INTERVAL_MS);
  }
}
