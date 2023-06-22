#include <Arduino.h>
#include <FS.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <Ticker.h>
#include <ArduinoJson.h>
#include "gprs_defs.h"
#include "software_definitions.h"
#include "saving.h"

// GPRS credentials
const char apn[] = "timbrasil.br";    // Your APN
const char gprsUser[] = "tim";         // User
const char gprsPass[] = "tim";         // Password
const char simPIN[] = "1010";          // SIM card PIN code, if any

/*Configuração padrão da datelo*/
/*const char apn[] = "datelo.nlt.br";    // Your APN
const char gprsUser[] = "nlt";         // User
const char gprsPass[] = "nlt";         // Password
const char simPIN[] = "6214";          // SIM card PIN code, if any*/

const char *server = "64.227.19.172";
char msg[MSG_BUFFER_SIZE];
char payload_char[MSG_BUFFER_SIZE];

// ESP hotspot defini  tions
const char *host = "esp32";                   // Here's your "host device name"
const char *ESP_ssid = "Mangue_Baja_DEV";     // Here's your ESP32 WIFI ssid
const char *ESP_password = "aratucampeaodev"; // Here's your ESP32 WIFI pass

/*Arduino Tools*/
Ticker sdTicker;

/*Global variables*/
bool mounted=false;
bool saveFlag=false;
bool savingBlink = false;
bool c=false;

/*Interrupt routine*/
void toggle_logging();
/*General Functions*/
void pinConfig();
void setupVolatilePacket();
void taskSetup();
/*SD functions*/
void sdConfig();
int countFiles(File dir);
void sdSave();
String packetToString();
void sdCallback();
/*GPRS functions*/
void gsmCallback(char *topic, byte *payload, unsigned int length);
void gsmReconnect();
void publishPacket();
/*States Machines*/
void SDstateMachine(void *pvParameters);
void ConnStateMachine(void *pvParameters);

void setup() 
{
    Serial.begin(115200);
    SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);

    pinConfig(); // Hardware and Interrupt Config

    setupVolatilePacket(); // volatile packet default values
    taskSetup();           // Tasks
}

void loop() {}

/* Setup Descriptions */

void pinConfig()
{
    /*Pins*/
    pinMode(EMBEDDED_LED,OUTPUT);
    attachInterrupt(digitalPinToInterrupt(Button),toggle_logging,CHANGE);
    sdTicker.attach(1.0/SAMPLE_FREQ, sdCallback); //Start data acquisition

    return;
}

void toggle_logging() 
{
    running = !running;
}

void setupVolatilePacket()
{
    volatile_packet.rpm=0;
    volatile_packet.speed=0;
}

void taskSetup()
{
  xTaskCreatePinnedToCore(SDstateMachine, "SDStateMachine", 10000, NULL, 5, NULL, 0);
  // This state machine is responsible for the Basic CAN logging
  xTaskCreatePinnedToCore(ConnStateMachine, "ConnectivityStateMachine", 10000, NULL, 5, NULL, 1);
  // This state machine is responsible for the GPRS, GPS and possible bluetooth connection
}

/*SD State Machine*/

void SDstateMachine(void *pvParameters)
{
    while (true)
    {
        while (!running)
        {
            pinMode(EMBEDDED_LED,HIGH);
            mounted=false;
        }
         
        while (running) 
        {
            if(saveFlag)
            {
                sdConfig();
                saveFlag = false;
            }
        }
    }
}

/*SD functions*/

void sdConfig()
{
    if (!mounted)
    {
        if (!SD.begin(SD_CS))
        {
            return;
        }

        root = SD.open("/");
        int num_files = countFiles(root);
        sprintf(file_name, "/%s%d.csv", "data", num_files + 1);
        mounted = true;   
    }
    sdSave();
}

int countFiles(File dir)
{
    int fileCountOnSD = 0; // for counting files

    while (true)
    {
        File entry = dir.openNextFile();
    
        if (!entry)
        {
        // no more files
        break;
        }
        // for each file count it
        fileCountOnSD++;
        entry.close();
    }

    return fileCountOnSD - 1;
}

void sdSave()
{
    dataFile = SD.open(file_name, FILE_APPEND);

    if (dataFile)
    {
        dataFile.println(packetToString());
        dataFile.close();
    
        digitalWrite(EMBEDDED_LED, LOW);
    }

    else
    {
        savingBlink = !savingBlink;
        digitalWrite(EMBEDDED_LED, savingBlink);
        Serial.println("falha no save");
    }
}

String packetToString()
{
    //aqui vai guardar os valores dos sensores
    
    String dataString = "";
     dataString += String(volatile_packet.rpm=0);
     dataString += String(volatile_packet.speed=0);

    return dataString;
}

void sdCallback()
{
    saveFlag=true;
}

/*Conectivity State Machine*/

void ConnStateMachine(void *pvParameters)
{
    if (c) {
        // To skip it, call init() instead of restart()
        Serial.println("Initializing modem...");
        modem.restart();
        // Or, use modem.init() if you don't need the complete restart

        String modemInfo = modem.getModemInfo();
        Serial.print("Modem: ");
        Serial.println(modemInfo);

        int modemstatus = modem.getSimStatus();
        Serial.print("Status: ");
        Serial.println(modemstatus);

        // Unlock your SIM card with a PIN if needed
        if (strlen(simPIN) && modem.getSimStatus() != 3)
        {
            modem.simUnlock(simPIN);
        }

        Serial.print("Waiting for network...");
        if (!modem.waitForNetwork(240000L))
        {
            Serial.println(" fail");
            delay(10000);
            return;
        }
        Serial.println(" OK");

        if (modem.isNetworkConnected())
        {
            Serial.println("Network connected");
        }

        Serial.print(F("Connecting to APN: "));
        Serial.print(apn);
        if (!modem.gprsConnect(apn, gprsUser, gprsPass))
        {
            Serial.println(" fail");
            delay(10000);
            return;
        }
        Serial.println(" OK");

        // Wi-Fi Config and Debug
        WiFi.mode(WIFI_MODE_AP);
        WiFi.softAP(ESP_ssid, ESP_password);

        if (!MDNS.begin(host)) // Use MDNS to solve DNS
        {
            // http://esp32.local
            Serial.println("Error configuring mDNS. Rebooting in 1s...");
            delay(1000);
            ESP.restart();
        }
        Serial.println("mDNS configured;");

        mqttClient.setServer(server, PORT);
        mqttClient.setCallback(gsmCallback);

        Serial.println("Ready");
        Serial.print("SoftAP IP address: ");
        Serial.println(WiFi.softAPIP());

        while (1)
        {
            if (!mqttClient.connected())
            {
            gsmReconnect();
            }

            publishPacket();

            mqttClient.loop();
            vTaskDelay(1);
        }
    }
}

/*GPRS Functions*/

void gsmCallback(char *topic, byte *payload, unsigned int length) 
{
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");

    memset(payload_char, 0, sizeof(payload_char));

    for (int i = 0; i < length; i++)
    {
        Serial.print((char)payload[i]);
        payload_char[i] = (char)payload[i];
    }
    Serial.println();
}

void gsmReconnect()
{
    int count = 0;
  Serial.println("Conecting to MQTT Broker...");
  while (!mqttClient.connected() && count < 3)
  {
    count++;
    Serial.println("Reconecting to MQTT Broker..");
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    if (mqttClient.connect(clientId.c_str(), "manguebaja", "aratucampeao", "/esp-connected", 2, true, "Offline", true))
    {
        sprintf(msg, "%s", "Online");
        mqttClient.publish("/esp-connected", msg);
        memset(msg, 0, sizeof(msg));
        Serial.println("Connected.");

        /* Subscribe to topics */
        mqttClient.subscribe("/esp-test");
        digitalWrite(LED_BUILTIN, HIGH);
    }
      else
    {
        Serial.print("Failed with state");
        Serial.println(mqttClient.state());
        delay(2000);
    }
  }
}

void publishPacket()
{
    StaticJsonDocument<300> doc;

    doc["rpm"] = volatile_packet.rpm;
    doc["speed"] = (volatile_packet.speed);

    memset(msg, 0, sizeof(msg));
    serializeJson(doc, msg);
    mqttClient.publish("/logging", msg);
}