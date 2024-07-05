#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Preferences.h>

BLECharacteristic *pCharacteristicTX;
BLECharacteristic *pCharacteristicRX;
BLECharacteristic *pCharacteristicAuth;
BLEServer* pServer;  // Declare a global variable to hold the BLEServer instance
bool deviceConnected = false;
bool authenticated = false;
unsigned long authStartMillis = 0;  // Variable to store the start time of authentication attempt
bool authTimeout = false;  // Flag to indicate if authentication timeout has occurred

#define SERVICE_UUID "35e2384d-09ba-40ec-8cc2-a491e7bcd763"
#define CHARACTERISTIC__UUID_TX "a9248655-7f1b-4e18-bf36-ad1ee859983f"
#define CHARACTERISTIC__UUID_RX "9d5cb5f2-5eb2-4b7c-a5d4-21e61c9c6f36"
#define CHARACTERISTIC__UUID_AUTH "e58b4b34-daa6-4a79-8a4c-50d63e6e767f"

#define DEVICE_NAME "ESP32-ECU"
#define AUTH_KEY "your_auth_key"  // Replace with your specific key (Dont forget to on AndroidStudio)  //possibly solves simple ble brute force pairing hacks by disgarding them.
const unsigned long AUTH_TIMEOUT_MS = 2000;  // Timeout period in milliseconds (e.g., 10 seconds)
const unsigned long CHECK_INTERVAL_MS = 1000;  // Interval for checking authentication status

//Stuff the esp controls (keep track of the states of things)
Preferences preferences;     // Create a Preferences object
int ledState = 0;


//==========================SEND:DATA:TO:APP==========================================

void sendData(String txValue)
{
   if(deviceConnected && authenticated)
  {
         // Convert String to char array
      char txData[txValue.length() + 1];
      txValue.toCharArray(txData, sizeof(txData));

      // Setting the value to the characteristic
      pCharacteristicTX->setValue((uint8_t*)txData, sizeof(txData)-1); // -1 to exclude null terminator

      // Notifying the connected client
      pCharacteristicTX->notify();
      
      // Print sent value
      Serial.print("Sent value: ");
      Serial.println(txValue);
  }
  else
  {
    Serial.println("Device not connected or not authenticated");
    delay(CHECK_INTERVAL_MS);
  }
}


//=============================SEND:STATE:TO:APP======================================

void sendStatesToApp()
{
  Serial.println("Sending device state to app");
  delay(50); 
  
  
  if(ledState == 1)
  { sendData("011"); } 

  else if(ledState == 0)
  { sendData("010"); }   
   

  delay(50);
  sendData("999");
  Serial.println("Device state send to app");
}


//=============================SAVE:STATE:TO:NVS======================================


void applyReadStateFromNVS() {
  if (ledState == 1) {
    digitalWrite(22, LOW);
  } else {
    digitalWrite(22, HIGH);
  }
}

void readStateFromNVS() {
  preferences.begin("my-app", false);
  ledState = preferences.getInt("ledState", 0); // default = off
  Serial.println("Read from NVS: ledState = " + String(ledState));
  preferences.end();

  applyReadStateFromNVS();
}

void saveStateToNVS() {
  preferences.begin("my-app", false);
    preferences.putInt("ledState", ledState);
  Serial.println("Written to NVS: ledState = " + String(ledState));
  preferences.end();

  sendData("991");
  Serial.println("Saving complete");
}


//=========================HANDLE:INCOMING:DATA=======================================

void handleData(std::string rxValue) 
{
  int encodedNumber = std::atoi(rxValue.c_str()); // Convert std::string to int
  int command = encodedNumber / 10;  // Extract first two digits (tens and hundreds place)
  int state = encodedNumber % 10; // Extract last digit (units place)
  
  switch (command) 
  {
      case 1: // LED
          if (state == 1) 
          {              
             digitalWrite(22, LOW); // Uncomment for actual hardware control
             ledState = 1;
             sendData("011");
          }
          else if (state == 0) 
          {
             digitalWrite(22, HIGH); // Uncomment for actual hardware control
             ledState = 0;
             sendData("010");
          }
          break;

      case 99: // MASTER COMMANDS (SAVE, RESYNC)
          if (state == 9) 
          {              
            sendStatesToApp();            
          }
          else if (state == 1) 
          {
            saveStateToNVS();            
          }    
          break;

      // Add more cases as needed
      
      default:
          break;
  }
}


//=============================START:ADVERTISING======================================

void startAdvertising() 
{
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
 
    // Start advertising
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
  
  readStateFromNVS();

  // Create the BLE device
  BLEDevice::init(DEVICE_NAME);

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

  // Start advertising
  startAdvertising();  
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
  delay(100);  // Small delay to avoid rapid cycling
}