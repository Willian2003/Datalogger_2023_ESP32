#include <Arduino.h>
#include <FS.h>
#include <Wire.h>
#include <SPI.h>
#include <I2S.h>
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

// ESP hotspot definitions
const char *host = "esp32";                   // Here's your "host device name"
const char *ESP_ssid = "Mangue_Baja_DEV";     // Here's your ESP32 WIFI ssid
const char *ESP_password = "aratucampeaodev"; // Here's your ESP32 WIFI pass

/*Arduino Tools*/
Ticker sdTicker;

/*Global variables*/
bool mounted = false;
bool saveFlag = false;
bool savingBlink = false;
bool aberto = true;
int waiting = 0;
int logging = 0;
bool currentState;
/*Interrupt routine*/
void toggle_logging();
void sdCallback();
void freq_sensor();
void speed_sensor();
/*General Functions*/
void pinConfig();
void setupVolatilePacket();
void taskSetup();
void data_acquisition();
/*SD functions*/
void sdConfig();
int countFiles(File dir);
void sdSave();
String packetToString();
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

    digitalWrite (WAIT_LED, HIGH);
    setupVolatilePacket();  // volatile packet default values
    pinConfig();            // Hardware and Interrupt Config
    taskSetup();            // Tasks
}

void loop() {}

/* Setup Descriptions */

void setupVolatilePacket()
{
    volatile_packet.rpm = 0;
    volatile_packet.speed = 0;
    volatile_packet.timestamp = 0;
}

void pinConfig()
{
    /*Pins*/
    pinMode(Button, INPUT_PULLUP);
    pinMode(EMBEDDED_LED, OUTPUT);
    pinMode(WAIT_LED, OUTPUT);
    pinMode(LOG_LED, OUTPUT);
    
    attachInterrupt(digitalPinToInterrupt(Button), toggle_logging, CHANGE);
}

void taskSetup()
{
  xTaskCreatePinnedToCore(SDstateMachine, "SDStateMachine", 10000, NULL, 5, NULL, 0);
  // This state machine is responsible for the Basic data acquisition and storage
  //xTaskCreatePinnedToCore(ConnStateMachine, "ConnectivityStateMachine", 10000, NULL, 5, NULL, 1);
  // This state machine is responsible for the GPRS and possible bluetooth connection
}

/*Interrupt of setup*/
void toggle_logging() 
{
    saveDebounceTimeout = millis();
    save = digitalRead(Button); 
}

/*SD State Machine*/

void SDstateMachine(void *pvParameters)
{
    while(1)
    {
        //while ((millis() - saveDebounceTimeout) > DEBOUNCETIME) {
        while(!save) 
        {
            Serial.println("não");
            digitalWrite (WAIT_LED, HIGH);
            digitalWrite (LOG_LED, LOW);
            digitalWrite (EMBEDDED_LED, LOW); 

            mounted = false;
            detachInterrupt(digitalPinToInterrupt(freq_pin));
            detachInterrupt(digitalPinToInterrupt(speed_pin));
            sdTicker.detach();
        }

        attachInterrupt(digitalPinToInterrupt(freq_pin), freq_sensor, FALLING);
        attachInterrupt(digitalPinToInterrupt(speed_pin), speed_sensor, FALLING);
        sdTicker.attach(1.0 / SAMPLE_FREQ, sdCallback);
        sdConfig();

        while(save) 
        {
            Serial.println("ok");
            digitalWrite (EMBEDDED_LED, HIGH);
            digitalWrite (WAIT_LED, LOW);
            digitalWrite (LOG_LED, HIGH);

            if (saveFlag) {
                data_acquisition();
                sdSave();   
                saveFlag = false;
            }
        } 
        //}
    /*
        while (!running)
        {
            waiting=1;
            logging=0;

            pinMode(WAIT_LED, waiting);
            pinMode(LOG_LED, logging);
            Serial.printf("\r\nrunning=%d\r\n", running);

            l_state = WAITING;
            mounted=false;

            detachInterrupt(digitalPinToInterrupt(freq_pin));
            detachInterrupt(digitalPinToInterrupt(speed_pin));
            sdTicker.detach();
        }   

        attachInterrupt(digitalPinToInterrupt(freq_pin), freq_sensor, FALLING);
        attachInterrupt(digitalPinToInterrupt(speed_pin), speed_sensor, FALLING);
        sdTicker.attach(1.0/SAMPLE_FREQ, sdCallback); //Start data acquisition

        sdConfig();
         
        while (running) 
        {
            waiting=0;
            logging=1;

            pinMode(WAIT_LED, waiting);
            pinMode(LOG_LED, logging);

            l_state = LOGGING;

            if (saveFlag)
            {
                data_acquisition();
                sdSave();   
                saveFlag = false;
            }
        }
    }*/
        vTaskDelay(1);
    }
}

/*SD functions*/

void sdConfig()
{
    if (!mounted)
    {
        if (!SD.begin())
        {
            return;
        }

        root = SD.open("/");
        int num_files = countFiles(root);
        sprintf(file_name, "/%s%d.csv", "data", num_files + 1);
        mounted = true;   
    }
}

int countFiles(File dir)
{
    int fileCountOnSD = 0; // for counting files

    while(true)
    {
        File entry = dir.openNextFile();
    
        if(!entry)
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

void data_acquisition()
{
    volatile_packet.rpm = freq_pulse_counter;
    volatile_packet.speed = speed_pulse_counter;
    volatile_packet.timestamp = millis();

    freq_pulse_counter = 0;
    speed_pulse_counter = 0;
}

void sdSave()
{
    dataFile = SD.open(file_name, FILE_APPEND);

    if(dataFile)
    {
        dataFile.println(packetToString());
        dataFile.close();
    
        savingBlink = !savingBlink;
        digitalWrite(EMBEDDED_LED, savingBlink);
    }
    else
    {
        digitalWrite(EMBEDDED_LED, HIGH);
        Serial.println("falha no save");
    }
}

String packetToString()
{
    //aqui vai guardar os valores dos sensores
    
    String dataString = "";
     dataString += String(volatile_packet.rpm);
     dataString += ",";
     dataString += String(volatile_packet.speed);
     dataString += ",";
     dataString += String(volatile_packet.timestamp);
     dataString += ",";

    return dataString;
}

void sdCallback()
{
    saveFlag=true;
}

/*Interrupts of SD Thread*/
void freq_sensor()
{
    freq_pulse_counter++;
}

void speed_sensor()
{
    speed_pulse_counter++;
}

/*Conectivity State Machine*/

void ConnStateMachine(void *pvParameters)
{
    if (aberto) {
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
    vTaskDelay(1);
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
    doc["TimeStamp"] = (volatile_packet.timestamp);

    memset(msg, 0, sizeof(msg));
    serializeJson(doc, msg);
    mqttClient.publish("/logging", msg);
}
