#ifndef GPRS_DEFS_H_
#define GPRS_DEFS_H_

#define SerialAT Serial2 // Set serial for AT commands (to the module)
#define MODEM_TX 17
#define MODEM_RX 16
#define PORT     1883
//#define PORT 1880 //Test for mqtt connection

// Configure TinyGSM library
#define TINY_GSM_MODEM_SIM800   // Modem is SIM800
#define TINY_GSM_RX_BUFFER 1024 // Set RX buffer to 1Kb
#define MSG_BUFFER_SIZE 1024

#include <TinyGSM.h>
#include <TinyGsmClient.h>
#include <PubSubClient.h>

TinyGsm modem(SerialAT);
TinyGsmClient client(modem);
PubSubClient mqttClient(client);

#endif
