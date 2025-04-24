/**
   wifiConfig.cpp
   by Matthew Ford,  2021/12/06
   (c)2021 Forward Computing and Control Pty. Ltd.
   NSW, Australia  www.forward.com.au
   This code may be freely used for both private and commerical use.
   Provide this copyright is maintained.

*/
#include "wifiConfig.h"
#include "LittleFSsupport.h"
#include "DebugOut.h"
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <millisDelay.h>
#include <SafeString.h>
#include <Adafruit_GFX.h>
//#include <Adafruit_SSD1306.h> //0.96" OLED
#include <Adafruit_SH1106.h>  // 1.3" OLED
#include <qrcode.h>


// normally DEBUG is commented out
//#define DEBUG
static Stream* debugPtr = NULL;  // local to this file


// 128x64 OLED on I2C SDA - IO4, SCL - IO5 (D1 & D2 on D1 Mini board!)
// there is a small gap between the sections meaning a QR code can't display white over the gap
// Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, -1); .. 0.96" OLED
Adafruit_SH1106 oled(-1);     // 1.3" OLED

// replace these with your local network's SSID and password.
// they will be useed as default if nothing valid has been saved in settings.
#define wifiSSID "" 
#define wifiPassword ""

/// access point settings
#define wifiWebConfigPASSWORD ""
#define wifiWebConfigAP "Controller"
static  IPAddress local_ip = IPAddress(10, 1, 1, 1);
static  IPAddress gateway_ip = IPAddress(10, 1, 1, 1);
static  IPAddress subnet_ip = IPAddress(255, 255, 255, 0);

millisDelay endConfigTimer;
const unsigned long END_CONFIG_MS = 5ul * 60 * 1000; // 5mins to make first connection and show wifi config webpage
const unsigned long RESTART_AFTER_CONFIG_MS = 30 * 1000; // 30 sec after config set
bool initConfigCalled = false; // set true on first call, second call start AP for wifi config

static struct Wifi_CONFIG_storage_struct storage;

static struct Wifi_CONFIG_storage_struct* loadWifiConfig(); // returns pointer to wifi config storage or default values (if any)

// default config for testing
static void setInitialWifiConfig() {
  cSFA(sfSSID, storage.ssid);
  cSFA(sfPW, storage.password);
  sfSSID = wifiSSID;  // if this is empty config not set and the Power Time will go in to network setup mode on power up.
  sfPW = wifiPassword;
}

void printWifConfig(struct Wifi_CONFIG_storage_struct& storage, Stream& out) {
  out.print("ssid:");
  out.println(storage.ssid);
  out.print("password:");
  out.println(storage.password);
}

static void handleNotFound();
static void handleRoot();
static void handleConfig();
static void setupAP(const char* ssid_wifi, const char* password_wifi);
static bool saveWifiConfig(struct Wifi_CONFIG_storage_struct& storagePtr);
String urlDecode(const String& text); // from ESP8266 webserver code

static ESP8266WebServer webserver(80);  // this just sets portNo nothing else happens until begin() is called

static bool inConfigMode = false;
cSF(sfStrongestAP, MAX_SSID_LEN);

/**
  call this in setup() only !!
  if called twice the second call will startup AP (since file will exist), if not already started
*/

struct Wifi_CONFIG_storage_struct*  initializeWifiConfig() {
#ifdef DEBUG
  debugPtr = getDebugOut();
#endif
  if (debugPtr) {
    debugPtr->print("initializeWifiConfig() "); debugPtr->println();
  }
  if (inConfigMode) {
    if (debugPtr) {
      debugPtr->print("initializeWifiConfig(), AP already started, just return "); debugPtr->println();
    }
    return &storage; // AP already started
  }
  if (!initializeFS()) {
    if (debugPtr) {
      debugPtr->print("LittleFS initialize failed "); debugPtr->println();
    }
    setInitialWifiConfig();
    return &storage;
  }

  if (initConfigCalled) { // this is the second call start AP
    setupAP(wifiWebConfigAP, wifiWebConfigPASSWORD); // sets inConfigMode
    return &storage; // loaded by setupAP
  } else {
    initConfigCalled = true; // start AP on next call
    loadWifiConfig(); // loads global storage // set default if missing
    saveWifiConfig(storage); // save
    loadWifiConfig(); // reload global
    return &storage; // loaded by setupAP
  }
}

/** call this in loop() every loop()
  returns true if in config mode
  in loop() have at the top
  void loop() {
  if (handleWifiConfig()) {
  return; // skip rest of the loop
  }
*/
bool handleWifiConfig() {
  if (!inConfigMode) {
    return false; // not doing wifi config so just ignore this call
  }
  // else in config mode
  webserver.handleClient();
  if (endConfigTimer.justFinished()) {
    ESP.restart(); 
  }
  return true;
}

// loads global storage and returns pointer to it
static struct Wifi_CONFIG_storage_struct* loadWifiConfig() {
#ifdef DEBUG
  debugPtr = getDebugOut();
#endif
  setInitialWifiConfig();
  if (!initializeFS()) {
    if (debugPtr) {
      debugPtr->println("FS failed to initialize");
    }
    if (debugPtr) {
      debugPtr->println("set config");
      printWifConfig(storage, *debugPtr);
    }
    return &storage; // returns default if cannot open FS
  }
  if (!LittleFS.exists(wifiConfigFileName)) {
    if (debugPtr) {
      debugPtr->print(wifiConfigFileName); debugPtr->print(" missing.");
    }
    if (debugPtr) {
      debugPtr->println("set config");
      printWifConfig(storage, *debugPtr);
    }
    return &storage; // returns default if missing
  }
  // else load config
  File f = LittleFS.open(wifiConfigFileName, "r");
  if (!f) {
    if (debugPtr) {
      debugPtr->print(wifiConfigFileName); debugPtr->print(" did not open for read.");
      debugPtr->println("set config");
      printWifConfig(storage, *debugPtr);
    }
    return &storage; // returns default wrong size
  }
  if (f.size() != sizeof(storage)) {
    if (debugPtr) {
      debugPtr->print(wifiConfigFileName); debugPtr->print(" wrong size.");
    }
    f.close();
    if (debugPtr) {
      debugPtr->println("set config");
      printWifConfig(storage, *debugPtr);
    }
    return &storage; // returns default wrong size
  }
  int bytesIn = f.read((uint8_t*)(&storage), sizeof(storage));
  if (bytesIn != sizeof(storage)) {
    if (debugPtr) {
      debugPtr->print(wifiConfigFileName); debugPtr->print(" wrong size read in.");
    }
    setInitialWifiConfig(); // again
    f.close();
    if (debugPtr) {
      debugPtr->println("set config");
      printWifConfig(storage, *debugPtr);
    }
    return &storage;
  }
  f.close();
  // else return settings
  if (debugPtr) {
    debugPtr->println("Loaded config");
    printWifConfig(storage, *debugPtr);
  }
  return &storage;
}

static bool saveWifiConfig(struct Wifi_CONFIG_storage_struct& storage) {
#ifdef DEBUG
  debugPtr = getDebugOut();
#endif
  if (!initializeFS()) {
    if (debugPtr) {
      debugPtr->println("FS failed to initialize");
    }
    return false;
  }
  // else save config
  File f = LittleFS.open(wifiConfigFileName, "w"); // create/overwrite
  if (!f) {
    if (debugPtr) {
      debugPtr->print(wifiConfigFileName); debugPtr->print(" did not open for write.");
    }
    return false; // returns default wrong size
  }
  int bytesOut = f.write((uint8_t*)(&storage), sizeof(struct Wifi_CONFIG_storage_struct));
  if (bytesOut != sizeof(struct Wifi_CONFIG_storage_struct)) {
    if (debugPtr) {
      debugPtr->print(wifiConfigFileName); debugPtr->print(" write failed.");
    }
    return false;
  }
  // else return settings
  f.close(); // no rturn
  if (debugPtr) {
    debugPtr->print(wifiConfigFileName); debugPtr->print(" config saved.");
    //    printWifConfig(storage, *debugPtr);
  }
  return true;
}


/**
   will return name of AP with strongest signal found or return empty string if none found
*/
static void scanForStrongestAP(SafeString &result) {
  result.clear();
  // WiFi.scanNetworks will return the number of networks found
  int8_t n = WiFi.scanNetworks();
  if (n <= 0) {
    if (debugPtr) {
      debugPtr->println("Wifi network scan failed");
    }
    return;
  }
  if (debugPtr) {
    debugPtr->println("Scan done");
    debugPtr->print("Found ");   debugPtr->print(n);    debugPtr->println(" networks");
  }
  int32_t maxRSSI = -10000;
  for (int8_t i = 0; i < n; ++i) {
    //const char * ssid_scan = WiFi.SSID_charPtr(i);
    int32_t rssi_scan = WiFi.RSSI(i);
    if (rssi_scan > maxRSSI) {
      maxRSSI = rssi_scan;
      String ssid = WiFi.SSID(i);
      result = ssid.c_str();
    }
    if (debugPtr) {
      debugPtr->print(result);
      debugPtr->print(" ");
      debugPtr->println(rssi_scan);
    }
    delay(0);
  }
}


/**
   sets up AP and loads current wifi settings
*/
static void setupAP(const char* ssid_wifi, const char* password_wifi) {
  /**
    Start scan WiFi networks available
    @param async         run in async mode
    @param show_hidden   show hidden networks
    @param channel       scan only this channel (0 for all channels)
    @param ssid*         scan for only this ssid (NULL for all ssid's)
    @return Number of discovered networks
  */
  inConfigMode = true; // in config mode
  if (debugPtr) {
    debugPtr->println(F("Setting up Access Point for WifiWebConfig"));
  }
  // connect to temporary wifi network for setup

  scanForStrongestAP(sfStrongestAP);
  if (debugPtr) {
    debugPtr->println(F("configure WifiWebConfig"));
  }

  if (debugPtr) {
    debugPtr->println(F("Access Point setup"));
  }
  WiFi.softAPConfig(local_ip, gateway_ip, subnet_ip);
  WiFi.softAP(ssid_wifi, password_wifi);

  if (debugPtr) {
    debugPtr->println("done");
    IPAddress myIP = WiFi.softAPIP();
    debugPtr->print(F("AP IP address: "));
    debugPtr->println(myIP);
  }
  delay(10);
  webserver.on ( "/", handleRoot );
  webserver.on ( "/config", handleConfig );
  webserver.onNotFound ( handleNotFound );
  webserver.begin();
  if (debugPtr) {
    debugPtr->println ( "HTTP webserver started" );
  }
  loadWifiConfig(); // sets global storage
  endConfigTimer.start(END_CONFIG_MS);

  // display QR code for AP
  // wifi QRcode format =  WIFI:T:WPA;S:myNetworkName;P:myPassword;;
  String sAP = "WIFI:S:" + String(ssid_wifi) + ";";
  generateQRCode(sAP.c_str());
  oled.setTextSize(1);         // set text size
  oled.setTextColor(WHITE);    // set text color
  oled.setCursor(0, 0);       // set position to display (10 cosl x 8 rows)
  oled.println();
  oled.println("Connect to");
  oled.println("Controller");
  oled.println("Wifi net");
  oled.println(" ..then .."); 
  oled.println("Browse to");
  oled.println("http://");
  oled.println("10.1.1.1");
  oled.display();              // display on OLED
}

static void handleConfig() {
  // set defaults
  
  if (webserver.args() > 0) {
    if (debugPtr) {
      String message = "Config results\n\n";
      message += "URI: ";
      message += webserver.uri();
      message += "\nMethod: ";
      message += ( webserver.method() == HTTP_GET ) ? "GET" : "POST";
      message += "\nArguments: ";
      message += webserver.args();
      message += "\n";
      for ( uint8_t i = 0; i < webserver.args(); i++ ) {
        message += " " + webserver.argName ( i ) + ": " + webserver.arg ( i ) + "\n";
      }
      debugPtr->println(message);
      debugPtr->println();
    }

    cSFA(sfSSID, storage.ssid);
    cSFA(sfPW, storage.password);

    uint8_t numOfArgs = webserver.args();
    uint8_t i = 0;
    for (; (i < numOfArgs); i++ ) {
      // check field numbers
      if (webserver.argName(i)[0] == '1') {
        String decoded = urlDecode(webserver.arg(i)); // result is always <= source so just copy over
        decoded.trim();
        sfSSID = decoded.c_str();
      } else if (webserver.argName(i)[0] == '2') {
        String decoded = urlDecode(webserver.arg(i)); // result is always <= source so just copy over
        decoded.trim();
        if (decoded != "*") {
          // update it
          sfPW = decoded.c_str();
        }
        // if password all blanks make it empty
      }
    }

    if (debugPtr) {
      debugPtr->println();
      printWifConfig(storage, *debugPtr);
    }

    // store the settings
    if (!saveWifiConfig(storage)) {
      loadWifiConfig(); // re-initialize
    }
  } // else if no args just return current settings

  delay(0);
  loadWifiConfig();
  if (debugPtr) {
    debugPtr->println();
    printWifConfig(storage, *debugPtr);
  }
  String rtnMsg = "<html>"
                  "<head>"
                  "<title>Controler - Wifi Network Config</title>"
                  "<meta charset=\"utf-8\" />"
                  "<meta name=viewport content=\"width=device-width, initial-scale=1\">"
                  "</head>"
                  "<body>"
                  "<h2>Controller Wifi Network Config Settings saved.</h2><br>Power cycle to connect to ";
  if (storage.password[0] == '\0') {
    rtnMsg += "the open network ";
  }
  rtnMsg += "<b>";
  rtnMsg += storage.ssid;
  rtnMsg += "</b>";

  rtnMsg += "<p>";
  rtnMsg += "<b>You also need to reconnect this device<br> to the ";
  rtnMsg += storage.ssid;
  rtnMsg += " network</b>";
  rtnMsg += "<p>";
  rtnMsg += " Controller will auto restart in 30 seconds</b>";
  rtnMsg += "</body>";
  rtnMsg += "</html>";

  webserver.send ( 200, "text/html", rtnMsg );
  endConfigTimer.start(RESTART_AFTER_CONFIG_MS); // restart in 30sec
}


static void handleRoot() {
  endConfigTimer.start(END_CONFIG_MS); // allow another 5mins stop 5min time out on first connection
  String msg;
  msg = "<html>"
        "<head>"
        "<title>Controler Wifi Network Config</title>"
        "<meta charset=\"utf-8\" />"
        "<meta name=viewport content=\"width=device-width, initial-scale=1\">"
        "</head>"
        "<body>"
        "<h2>Controler Wifi Network Config</h2>"
        "<p>Use this form to configure the controller to connect to your Wifi network.<br>"
        "<i>Leading and trailing spaces are trimmed.</i></p>"
        "<form class=\"form\" method=\"post\" action=\"/config\" >"
        "<p class=\"name\">"
        "<label for=\"name\">Network name (SSID)</label><br>"
        "<input type=\"text\" name=\"1\" id=\"ssid\" placeholder=\"wifi network name\"  required "; // field 1

  if (!sfStrongestAP.isEmpty()) {
    msg += " value=\"";
    msg += sfStrongestAP.c_str();
    msg += "\" ";
  }
  msg += " />"
         "<p class=\"password\">"
         "<label for=\"password\">Password for WEP/WPA/WPA2 (enter a space if there is no password, i.e. OPEN)<br>"
         "To use existing password leave as * </label><br>"
         "<input type=\"text\" name=\"2\" id=\"password\" placeholder=\"wifi network password\" autocomplete=\"off\" required "; // field 2
  if (storage.password[0] != '\0') {
    msg += " value=\"";
    msg += "*"; //storage.password[0];
    msg += "\" ";
  }
  msg += " />"
         "</p>"
         "<p class=\"submit\">"
         "<input type=\"submit\" style=\"font-size:25px;\" value=\"Configure\"  />"
         "</p>"
         "</form>"
         "The control will auto-restart in 5 mins if current config not changed"
         "</body>"
         "</html>";

  webserver.send ( 200, "text/html", msg );
}


static void handleNotFound() {
  handleRoot();
}


String urlDecode(const String& text) {
  String decoded;
  char temp[] = "0x00";
  unsigned int len = text.length();
  unsigned int i = 0;
  while (i < len)
  {
    char decodedChar;
    char encodedChar = text.charAt(i++);
    if ((encodedChar == '%') && (i + 1 < len))
    {
      temp[2] = text.charAt(i++);
      temp[3] = text.charAt(i++);

      decodedChar = strtol(temp, NULL, 16);
    }
    else {
      if (encodedChar == '+')
      {
        decodedChar = ' ';
      }
      else {
        decodedChar = encodedChar;  // normal ascii char
      }
    }
    decoded += decodedChar;
  }
  return decoded;
}

void generateQRCode(const char* text) {

// formats for url, wifi, email, etc.
// https://github.com/zxing/zxing/wiki/Barcode-Contents
  // Create a QR code object
  QRCode qrcode;
  uint8_t qrcodeData[qrcode_getBufferSize(3)];
  qrcode_initText(&qrcode, qrcodeData, 3, ECC_LOW, text);
  oled.clearDisplay();

  int offset = 64; // Adjust offset as needed
  for (uint8_t y = 0; y < qrcode.size; y++) {
    for (uint8_t x = 0; x < qrcode.size; x++) {
      if (qrcode_getModule(&qrcode, x, y)) {
        oled.fillRect(offset + x * 2, y * 2, 2, 2, WHITE);
      }
    }
  }
  oled.display();
}

void startOLED() {
  // oled.begin(SSD1306_SWITCHCAPVCC, 0x3C, true) 
  oled.begin(SH1106_SWITCHCAPVCC, 0x3C);
  oled.clearDisplay(); // clear display
}

void printIPconfig(const char* nIP) { // display IP address screen & QR code
// https://github.com/ricmoo/QRCode
  generateQRCode(nIP); // show QR code for our IP address at rhs of display
  String tStr = nIP;
  oled.setTextSize(1);         // set text size
  oled.setTextColor(WHITE);    // set text color
  oled.setCursor(0, 0);       // set position to display (11 cols x 8 rows)
  oled.println("To setup");
  oled.println("controller");
  oled.println("browse/QR");
  oled.println("to addr");
  oled.println(tStr.substring(0,10));
  oled.println(tStr.substring(10));
  
  oled.display();              // display on OLED
}