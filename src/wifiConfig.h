#ifndef _WIFI_CONFIG_H
#define _WIFI_CONFIG_H
/*   
   wifiConfig.h
   by Matthew Ford,  2021/12/06
   (c)2021 Forward Computing and Control Pty. Ltd.
   NSW, Australia  www.forward.com.au
   This code may be freely used for both private and commerical use.
   Provide this copyright is maintained.
*/

#include "main.h"

#define OLED_WIDTH 128
#define OLED_HEIGHT 64

#ifndef MAX_SSID_LEN
  #define MAX_SSID_LEN 32
#endif
const int MAX_PASSWORD_LEN = 64;
const int MAX_STATICIP_LEN = 40;

struct Wifi_CONFIG_storage_struct {
  char ssid[MAX_SSID_LEN + 1]; // WIFI ssid + null
  char password[MAX_PASSWORD_LEN + 1]; // WiFi password,  if empyt use OPEN, else use AUTO (WEP/WPA/WPA2) + null
};

const char wifiConfigFileName[] = "/wifiConfig.bin";  // binary file

struct Wifi_CONFIG_storage_struct* initializeWifiConfig(); // start AP for config call handleWifiConfig() from loop to handle web page results
void startConfigAP();
bool handleWifiConfig();
void clearRebootFile();
String urlDecode(const String& text);
void startOLED();
void printIPconfig(const char* ip); // display client IP address screen & QR code

void generateQRCode(const char* text);

#endif
