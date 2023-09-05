#include <Arduino.h>
#include <SD.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <Ticker.h>
#include <ArduinoJson.h>
#include "gprs_defs.h"
#include "software_definitions.h"
#include "saving.h"
                                                                            
// GPRS credentials
// const char apn[] = "timbrasil.br";    // Your APN
// const char gprsUser[] = "tim";         // User
// const char gprsPass[] = "tim";         // Password
// const char simPIN[] = "1010";          // SIM card PIN code, if any

// Configuração padrão da claro
const char apn[] = "claro.com.br";      // Your APN
const char gprsUser[] = "claro";        // User
const char gprsPass[] = "claro";        // Password
const char simPIN[] = "3636";           // SIM cad PIN code, id any

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
int err;
bool mounted = false;
bool saveFlag = false;
bool available = false;
bool read_state = false;
String rpm, speed, timestamp;
uint8_t freq_pulse_counter = 0;
uint8_t speed_pulse_counter =  0;
unsigned long start=0, timeout=5000; //5 segundos 
unsigned long set_pointer = 0;

/*Interrupt routine*/
void toggle_logger();
void sdCallback();
void freq_sensor();
void speed_sensor();
/*General Functions*/
void pinConfig();
void setupVolatilePacket();
void taskSetup();
// SD functions
int sdConfig();
int countFiles(File dir);
void sdSave();
String packetToString();
void readFile();
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

/*Interrupt of setup*/
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
            Serial.println("Montando cartão SD...");
          
            err = sdConfig();
            Serial.printf("%s\n", (err==MOUNT_ERROR ? "Falha ao montar o cartão" : err==FILE_ERROR ? "Falha ao abrir o arquivo" : "Arquivo ok"));
            
            if(err==FILE_OK)
            {
                dataFile.close();
                break;
            }
          
            else if(err==MOUNT_ERROR) 
            {
                Serial.println("Iniciando tentativa de conexão");
                start=millis();

                while((millis()-start)<timeout)
                {
                    if(sdConfig()==FILE_OK)
                    {
                        Serial.println("Reconexão Feita!!!");
                        mounted=true;
                        break;
                    }
                    vTaskDelay(1);
                }
                vTaskDelay(1);
              
                if(!mounted)
                {
                    Serial.println("SD não montado, resetando em 1s...");
                    delay(1000);
                    esp_restart();
                }
            } else {
                Serial.println("Selecione outro SD!");
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
    sprintf(file_name, "/%s%d.csv", "data", num_files);

    dataFile = SD.open(file_name, FILE_APPEND);

    if (dataFile)
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
    //Aqui vai guardar os valores dos sensores
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

void readFile()
{
    String linha;
    
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