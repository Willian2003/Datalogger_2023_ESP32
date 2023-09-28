#include <Arduino.h>
#include <SD.h>
#include <Arduino_LSM6DS3.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <Ticker.h>
#include <ArduinoJson.h>
#include "gprs_defs.h"
#include "software_definitions.h"
#include "saving.h"

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
String rpm, speed, timestamp;
int err;
int acqr;
bool mounted = false;
bool saveFlag = false;
bool available = false;
uint8_t freq_pulse_counter = 0;
uint8_t speed_pulse_counter =  0;
unsigned long start=0, timeout=5000; // 5 sec

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

    acqr = IMU.begin();

    if(!IMU.begin())
    {
        Serial.println("Acelerometer error!!!");
        return;
    }

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
    volatile_packet.accx = 0;
    volatile_packet.accy = 0;
    volatile_packet.accz = 0;
    volatile_packet.dpsx = 0;
    volatile_packet.dpsy = 0;
    volatile_packet.dpsz = 0;
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
    //saveDebounceTimeout = millis();
    running = digitalRead(Button); 
}

/*SD State Machine*/
void SDstateMachine(void *pvParameters)
{
    while (1)
    {
        do {
            Serial.println("Mounting SD card...");
          
            err = sdConfig();
            Serial.printf("%s\n", (err==MOUNT_ERROR ? "Failed to mount the card" : err==FILE_ERROR ? "Failed to open the file" : "ok file"));
            
            if(err==FILE_OK)
            {
                dataFile.close();
                break;
            }
          
            else if(err==MOUNT_ERROR) 
            {
                Serial.println("Initiating connection attempt");
                start=millis();

                while((millis()-start)<timeout)
                {
                    if(sdConfig()==FILE_OK)
                    {
                        Serial.println("Reconnection Done!!!");
                        mounted=true;
                        break;
                    }
                    vTaskDelay(1);
                }
                vTaskDelay(1);
              
                if(!mounted)
                {
                    Serial.println("SD not mounted, resetting in 1s...");
                    delay(1000);
                    esp_restart();
                }
            } else {
                Serial.println("Select another SD!");
                return;
            }
                   
        } while(err!=FILE_OK);
      
        Serial.println("Waiting mode");
      
        digitalWrite(WAIT_LED, HIGH);
        digitalWrite(LOG_LED, LOW);

        detachInterrupt(digitalPinToInterrupt(freq_pin));
        detachInterrupt(digitalPinToInterrupt(speed_pin));
        sdTicker.detach();

        while(!running)
        {
            while(available)
            {
                digitalWrite(WAIT_LED, HIGH);
                digitalWrite(LOG_LED, HIGH);
                Serial.println("Reading mode, please wait!");
                delay(500);
                digitalWrite(WAIT_LED, LOW);
                digitalWrite(LOG_LED, LOW);
                delay(500);
            }

            digitalWrite(WAIT_LED, HIGH);
            digitalWrite(LOG_LED, LOW);
            
            vTaskDelay(1);
        }

        Serial.println("Logging mode");
      
        digitalWrite(WAIT_LED, LOW);
        digitalWrite(LOG_LED, HIGH);

        attachInterrupt(digitalPinToInterrupt(freq_pin), freq_sensor, FALLING);
        attachInterrupt(digitalPinToInterrupt(speed_pin), speed_sensor, FALLING);
        sdTicker.attach(1.0/SAMPLE_FREQ, sdCallback);

        while(running)
        {
            if(saveFlag)
            {
                volatile_packet.rpm = freq_pulse_counter;
                volatile_packet.speed = speed_pulse_counter;

                if(acqr!=0)
                {
                    if(IMU.accelerationAvailable() && IMU.gyroscopeAvailable())
                    {
                        IMU.readAcceleration(volatile_packet.accx, volatile_packet.accy, volatile_packet.accz);
                        IMU.readGyroscope(volatile_packet.dpsx, volatile_packet.dpsy, volatile_packet.dpsz);
                    }
                } else {
                    volatile_packet.accx = 0;
                    volatile_packet.accy = 0;
                    volatile_packet.accz = 0;
                    volatile_packet.dpsx = 0;
                    volatile_packet.dpsy = 0;
                    volatile_packet.dpsz = 0;
                }

                volatile_packet.timestamp = millis();

                freq_pulse_counter = 0;
                speed_pulse_counter = 0;

                sdSave();   
                saveFlag = false;
            }
            vTaskDelay(1);
        }

        available=true;
      
        vTaskDelay(1);
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
     dataString += String(volatile_packet.rpm);
     dataString += ",";
     dataString += String(volatile_packet.speed);
     dataString += ",";
     dataString += String(volatile_packet.accx);
     dataString += ",";
     dataString += String(volatile_packet.accy);
     dataString += ",";
     dataString += String(volatile_packet.accz);
     dataString += ",";
     dataString += String(volatile_packet.dpsx);
     dataString +=  ",";
     dataString += String(volatile_packet.dpsy);
     dataString += ",";
     dataString += String(volatile_packet.dpsz);
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
    Serial.println("Conecting to MQTT Broker...");
    while (!mqttClient.connected() && count < 3)
    {
        count++;
        Serial.println("Reconecting to MQTT Broker..");
        String clientId = "ESP32Client-";
        clientId += String(random(0xffff), HEX);
        if (mqttClient.connect(clientId.c_str(), "manguebaja", "aratucampeao", "/online", 2, true, "Offline", true))
        {
            sprintf(msg, "%s", "Online");
            mqttClient.publish("/online", msg);
            memset(msg, 0, sizeof(msg));
            Serial.println("Connected.");

            /* Subscribe to topics */
            mqttClient.subscribe("/esp-test");
            digitalWrite(LED_BUILTIN, HIGH);
        }
        else {
            Serial.print("Failed with state");
            Serial.println(mqttClient.state());
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

    //if (dataFile) 
    //{
        
    while(dataFile.available()) 
    {
        if(read_state) 
        {
            dataFile.seek(set_pointer); // Para setar a posição (ponteiro) de leitura do arquivo
            // Serial.println("Read state ok!!");
        }

        linha = dataFile.readStringUntil('\n');

        set_pointer = dataFile.position(); // Guardar a posição (ponteiro) de leitura do arquivo

        // Separar os valores usando a vírgula como delimitador
        int posVirgula1 = linha.indexOf(',');
        int posVirgula2 = linha.indexOf(',', posVirgula1 + 1);
        int posVirgula3 = linha.lastIndexOf(',');

        // Extrair os valores de cada sensor
        rpm = linha.substring(0, posVirgula1);
        speed = linha.substring(posVirgula1 + 1, posVirgula2);
        timestamp = linha.substring(posVirgula2 + 1, posVirgula3);

        publishPacket();

        Serial.printf("rpm=%s, speed=%s, timestamp=%s\n", rpm, speed, timestamp);

        read_state = true;
    }
        //else {
    read_state=false;
    set_pointer=0;
        //}
    //} else {
    //    Serial.println("Failed to open file for reading or the file not exist");
    //    return;
    //}
}

void publishPacket()
{
    StaticJsonDocument<300> doc;

    doc["rpm"] = (rpm);
    doc["speed"] = (speed);
    doc["TimeStamp"] = (timestamp);

    memset(msg, 0, sizeof(msg));
    serializeJson(doc, msg);
    mqttClient.publish("/logger", msg);
}