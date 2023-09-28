#include <Arduino.h>
#include <SD.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <Ticker.h>
#include <ArduinoJson.h>
#include <SparkFunLSM6DS3.h>
#include "gprs_defs.h"
#include "software_definitions.h"
#include "saving.h"
#include "hardware_definitions.h"
#include "packets.h"

//#define TIM   //Uncomment this line and comment the others if this is your chip
#define CLARO   //Uncomment this line and comment the others if this is your chip
//#define VIVO  //Uncomment this line and comment the others if this is your chip

// GPRS credentials
#ifdef TIM
    const char apn[] = "timbrasil.br";    // Your APN
    const char gprsUser[] = "tim";        // User
    const char gprsPass[] = "tim";        // Password
    const char simPIN[] = "1010";         // SIM card PIN code, if any
#elif defined(CLARO)
    const char apn[] = "claro.com.br";    // Your APN
    const char gprsUser[] = "claro";      // User
    const char gprsPass[] = "claro";      // Password
    const char simPIN[] = "3636";         // SIM cad PIN code, id any
#elif defined(VIVO)
    const char apn[] = "zap.vivo.com.br";  // Your APN
    const char gprsUser[] = "vivo";        // User
    const char gprsPass[] = "vivo";        // Password
    const char simPIN[] = "8486";          // SIM cad PIN code, id any
#endif

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
// Packet constantly saved
packet_t volatile_packet;
strings volatile_strings;
int err;
bool mounted = false;
bool saveFlag = false;
bool available = false;
uint8_t freq_pulse_counter = 0;
uint8_t speed_pulse_counter =  0;
unsigned long start=0, timeout=5000; // 5 sec
unsigned long lastDebounceTime = 0, debounceDelay =200;

/* Interrupt routine */
void toggle_logger();
void sdCallback();
void freq_sensor();
void speed_sensor();
/* General Functions */
void setupVolatilePacket();
void pinConfig();
void taskSetup();
// SD functions
int sdConfig();
int countFiles(File dir);
void sdSave();
String packetToString();
/* GPRS functions */
void gsmCallback(char *topic, byte *payload, unsigned int length);
void gsmReconnect();
void readFile();
void publishPacket();
/* States Machines */
void SDstateMachine(void *pvParameters);
void ConnStateMachine(void *pvParameters);

void setup() 
{
    Serial.begin(115200);
    SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);

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
    /* Pins */
    pinMode(Button, INPUT_PULLUP);
    pinMode(EMBEDDED_LED, OUTPUT);
    pinMode(WAIT_LED, OUTPUT);
    pinMode(LOG_LED, OUTPUT);
    
    attachInterrupt(digitalPinToInterrupt(Button), toggle_logger, CHANGE);
  
  return;
}

void taskSetup()
{
  xTaskCreatePinnedToCore(SDstateMachine, "SDStateMachine", 10000, NULL, 5, NULL, 0);
  // This state machine is responsible for the Basic data acquisition and storage
  xTaskCreatePinnedToCore(ConnStateMachine, "ConnectivityStateMachine", 10000, NULL, 5, NULL, 1);
  // This state machine is responsible for the GPRS and possible bluetooth connection
}

/* Interrupt of setup */
void toggle_logger() 
{
    lastDebounceTime = millis();
    running = digitalRead(Button); 
}

/*SD State Machine*/
void SDstateMachine(void *pvParameters)
{
    while (1)
    {   
        if ((millis() - lastDebounceTime) > debounceDelay){
            do {
                // Serial.println("Mounting SD card...");
            
                err = sdConfig();
                // Serial.printf("%s\n", (err==MOUNT_ERROR ? "Failed to mount the card" : err==FILE_ERROR ? "Failed to open the file" : "ok file"));
                
                if(err==FILE_OK)
                {
                    dataFile.close();
                    break;
                }

                else if(err==MOUNT_ERROR) 
                {
                    // Serial.println("Initiating connection attempt");
                    start=millis();

                    while((millis()-start)<timeout)
                    {
                        if(sdConfig()==FILE_OK)
                        {
                            // Serial.println("Reconnection Done!!!");
                            mounted=true;
                            break;
                        }
                        vTaskDelay(1);
                    }
                    vTaskDelay(1);
                
                    if(!mounted)
                    {
                        // Serial.println("SD not mounted, resetting in 1s...");
                        delay(1000);
                        esp_restart();
                    }
                } else {
                    // Serial.println("Select another SD!");
                    return;
                }
                    
            } while(err!=FILE_OK);

            // if (!IMU.begin()) {
            //     Serial.println("Failed to initialize IMU!");

            //     esp_restart();
            // }
        
            // Serial.println("Waiting mode");
        
            digitalWrite(WAIT_LED, HIGH);
            digitalWrite(LOG_LED, LOW);

            detachInterrupt(digitalPinToInterrupt(freq_pin));
            detachInterrupt(digitalPinToInterrupt(speed_pin));
            sdTicker.detach();

            while(!running)
            {

                digitalWrite(WAIT_LED, HIGH);
                digitalWrite(LOG_LED, LOW);
                
                vTaskDelay(1);
            }

            // Serial.println("Logging mode");

            digitalWrite(WAIT_LED, LOW);
            digitalWrite(LOG_LED, HIGH);

            attachInterrupt(digitalPinToInterrupt(freq_pin), freq_sensor, FALLING);
            attachInterrupt(digitalPinToInterrupt(speed_pin), speed_sensor, FALLING);
            sdTicker.attach(1.0/SAMPLE_FREQ, sdCallback);

            while(running)
            {
                if(saveFlag)
                {
                    // float x, y, z;

                    // float w, r, t;

                    // if (IMU.accelerationAvailable()) 
                    // {
                    //     IMU.readAcceleration(x, y, z);

                    //     // Serial.print(x);
                    //     // Serial.print('\t');
                    //     // Serial.print(y);
                    //     // Serial.print('\t');
                    //     // Serial.println(z);
                    // }

                    // if (IMU.gyroscopeAvailable()) 
                    // {
                    //     IMU.readGyroscope(w, r, t);

                    //     // Serial.print(w);
                    //     // Serial.print('\t');
                    //     // Serial.print(r);
                    //     // Serial.print('\t');
                    //     // Serial.println(t);
                    // }

                    // volatile_packet.accx = x;
                    // volatile_packet.accy = y;
                    // volatile_packet.accz = z;
                    // volatile_packet.gyrox = w;
                    // volatile_packet.gyroy = r;
                    // volatile_packet.gyroz = t;
                    volatile_packet.rpm = freq_pulse_counter;
                    volatile_packet.speed = speed_pulse_counter;
                    volatile_packet.timestamp = millis();

                    freq_pulse_counter = 0;
                    speed_pulse_counter = 0;

                    sdSave();   
                    saveFlag = false;
                }
                vTaskDelay(1);
            }

            available=true;

            while(available)
            {
                digitalWrite(WAIT_LED, HIGH);
                digitalWrite(LOG_LED, HIGH);
                delay(500);
                digitalWrite(WAIT_LED, LOW);
                digitalWrite(LOG_LED, LOW);
                delay(500);

            }

        vTaskDelay(1);
        }
    }
}

/*SD functions*/
int sdConfig ()
{
    if (!SD.begin())
    {
        return MOUNT_ERROR;
    }

    root = SD.open("/");
    int num_files = countFiles(root);
    sprintf(file_name, "/%s%d.csv", "data", num_files+1);

    dataFile = SD.open(file_name, FILE_APPEND);

    if(dataFile)
    {
        return FILE_OK;
    } else {
        return FILE_ERROR;
    }    
}

int countFiles(File dir)
{
    int fileCountOnSD = 0; // for counting files

    while (true)
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

    return fileCountOnSD-1;
}

void sdSave()
{
    dataFile = SD.open(file_name, FILE_APPEND);

    dataFile.println(packetToString());
    dataFile.close();
    //if (dataFile)
    //{
    
    /*} else {
        digitalWrite(EMBEDDED_LED, HIGH);
        Serial.println("falha no save");
    }*/
}

String packetToString()
{
    String dataString = "";
    //  dataString += String(volatile_packet.accx);
    //  dataString += ",";
    //  dataString += String(volatile_packet.accy);
    //  dataString += ",";
    //  dataString += String(volatile_packet.accz);
    //  dataString += ",";
    //  dataString += String(volatile_packet.gyrox);
    //  dataString += ",";
    //  dataString += String(volatile_packet.gyroy);
    //  dataString += ",";
    //  dataString += String(volatile_packet.gyroz);
    //  dataString += ",";
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
    // To skip it, call init() instead of restart()
    // Serial.println("Initializing modem...");
    modem.restart();
    // Or, use modem.init() if you don't need the complete restart

    // String modemInfo = modem.getModemInfo();
    // Serial.print("Modem: ");
    // Serial.println(modemInfo);

    // int modemstatus = modem.getSimStatus();
    // Serial.print("Status: ");
    // Serial.println(modemstatus);

    // Unlock your SIM card with a PIN if needed
    if (strlen(simPIN) && modem.getSimStatus() != 3)
    {
        modem.simUnlock(simPIN);
    }

    // Serial.print("Waiting for network...");
    if (!modem.waitForNetwork(240000L))
    {
        // Serial.println(" fail");
        delay(10000);
        return;
    }
    // Serial.println(" OK");

    if (modem.isNetworkConnected())
    {
        // Serial.println("Network connected");
    }

    // Serial.print(F("Connecting to APN: "));
    // Serial.print(apn);
    if (!modem.gprsConnect(apn, gprsUser, gprsPass))
    {
        // Serial.println("Fail");
        delay(10000);
        return;
    }
    // Serial.println("OK");

    // Wi-Fi Config and Debug
    WiFi.mode(WIFI_MODE_AP);
    WiFi.softAP(ESP_ssid, ESP_password);

    if (!MDNS.begin(host)) // Use MDNS to solve DNS
    {
        // http://esp32.local
        // Serial.println("Error configuring mDNS. Rebooting in 1s...");
        delay(1000);
        ESP.restart();
    }
    // Serial.println("mDNS configured;");

    mqttClient.setServer(server, PORT);
    mqttClient.setCallback(gsmCallback);

    // Serial.println("Ready");
    // Serial.print("SoftAP IP address: ");
    // Serial.println(WiFi.softAPIP());

    while (1)
    {
        if(!mqttClient.connected())
        {
            gsmReconnect();
        }

        if(available)
        {
            readFile();
            available=false;
        }

        mqttClient.loop();
        vTaskDelay(1);
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
    // Serial.println("Conecting to MQTT Broker...");
    while (!mqttClient.connected() && count < 3)
    {
        count++;
        // Serial.println("Reconecting to MQTT Broker...");
        String clientId = "ESP32Client-";
        clientId += String(random(0xffff), HEX);
        if (mqttClient.connect(clientId.c_str(), "manguebaja", "aratucampeao", "/online", 2, true, "Offline", true))
        {
            sprintf(msg, "%s", "Online");
            mqttClient.publish("/online", msg);
            memset(msg, 0, sizeof(msg));
            // Serial.println("Connected.");

            /* Subscribe to topics */
            mqttClient.subscribe("/esp-test");
            digitalWrite(LED_BUILTIN, HIGH);
        }
        else {
            // Serial.print("Failed with state");
            // Serial.println(mqttClient.state());
            delay(2000);
        }
    }
}

void readFile()
{
    String linha;
    bool read_state = false;
    unsigned long set_pointer = 0;
    
    dataFile = SD.open(file_name, FILE_READ);
        
    while(dataFile.available()) 
    {
        if(!mqttClient.connected())
        {
            gsmReconnect();
        }

        if(read_state) 
        {
            dataFile.seek(set_pointer); // Para setar a posição (ponteiro) de leitura do arquivo
        }

        linha = dataFile.readStringUntil('\n');

        set_pointer = dataFile.position(); // Guardar a posição (ponteiro) de leitura do arquivo

        // Separar os valores usando a vírgula como delimitador
        // int posVirgula1 = linha.indexOf(',');
        // int posVirgula2 = linha.indexOf(',', posVirgula1 + 1);
        // int posVirgula3 = linha.indexOf(',', posVirgula2 + 1);
        // int posVirgula4 = linha.indexOf(',', posVirgula3 + 1);
        // int posVirgula5 = linha.indexOf(',', posVirgula4 + 1);
        // int posVirgula6 = linha.indexOf(',', posVirgula5 + 1);
        // int posVirgula7 = linha.indexOf(',', posVirgula6 + 1);
        // int posVirgula8 = linha.indexOf(',', posVirgula7 + 1);
        // int posVirgula9 = linha.lastIndexOf(',');

        int posVirgula1 = linha.indexOf(',');
        int posVirgula2 = linha.indexOf(',', posVirgula1 + 1);
        int posVirgula3 = linha.lastIndexOf(',');

        // Extrair os valores de cada sensor
        // volatile_strings.accx = linha.substring(0, posVirgula1);
        // volatile_strings.accy = linha.substring(posVirgula1 + 1, posVirgula2);
        // volatile_strings.accz = linha.substring(posVirgula2 + 1, posVirgula3);
        // volatile_strings.gyrox = linha.substring(posVirgula3 + 1, posVirgula4);
        // volatile_strings.gyroy = linha.substring(posVirgula4 + 1, posVirgula5);
        // volatile_strings.gyroz = linha.substring(posVirgula5 + 1, posVirgula6);
        // volatile_strings.rpm = linha.substring(posVirgula6 + 1, posVirgula7);
        // volatile_strings.speed = linha.substring(posVirgula7 + 1, posVirgula8);
        // volatile_strings.timestamp = linha.substring(posVirgula8 + 1, posVirgula9);

        volatile_strings.rpm = linha.substring(0, posVirgula1);
        volatile_strings.speed = linha.substring(posVirgula1 + 1, posVirgula2);
        volatile_strings.timestamp = linha.substring(posVirgula2 + 1, posVirgula3);

        publishPacket();

        //Serial.printf("rpm=%s, speed=%s, timestamp=%s\n", rpm, speed, timestamp);

        read_state = true;
    }
    read_state=false;
    set_pointer=0;

}

void publishPacket()
{
    StaticJsonDocument<300> doc;

    // doc["accx"] = (volatile_strings.accx);
    // doc["accy"] = (volatile_strings.accy);
    // doc["accz"] = (volatile_strings.accz);
    // doc["gyrox"] = (volatile_strings.gyrox);
    // doc["gyroy"] = (volatile_strings.gyroy);
    // doc["gyroz"] = (volatile_strings.gyroz);
    doc["rpm"] = (volatile_strings.rpm);
    doc["speed"] = (volatile_strings.speed);
    doc["timestamp"] = (volatile_strings.timestamp);

    memset(msg, 0, sizeof(msg));
    serializeJson(doc, msg);
    mqttClient.publish("/logger", msg);
}