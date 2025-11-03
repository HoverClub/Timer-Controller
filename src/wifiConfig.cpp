/**
   wifiConfig.cpp
   by Matthew Ford,  2021/12/06
   (c)2021 Forward Computing and Control Pty. Ltd.
   NSW, Australia  www.forward.com.au
   This code may be freely used for both private and commerical use.
   Provide this copyright is maintained.

*/
#include "main.h"

// 128x64 OLED on I2C SDA - IO4, SCL - IO5 (D1 & D2 on D1 Mini board!)
// Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, -1); .. 0.96" OLED
Adafruit_SH1106 oled(-1);     // 1.3" OLED

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

void printWifConfig(struct Wifi_CONFIG_storage_struct& storage) {
  debug("ssid:");
  debugln(storage.ssid);
  debug("password:");
  debugln(storage.password);
}

static void setupAP(const char* ssid_wifi, const char* password_wifi);
static bool saveWifiConfig(struct Wifi_CONFIG_storage_struct& storagePtr);
static AsyncWebServer webserver(80);  // this just sets portNo nothing else happens until begin() is called
String urlDecode(const String& text); // from ESP8266 webserver code
//  static ESP8266WebServer webserver(80);  // this just sets portNo nothing else happens until begin() is called

cSF(sfStrongestAP, MAX_SSID_LEN);

/**
  call this in setup() only !!
  if called twice the second call will startup AP (since file will exist), if not already started
*/

bool inConfigMode = false; // made non-static to be accessible from other files if needed

struct Wifi_CONFIG_storage_struct*  initializeWifiConfig() {
  debug(F("initializeWifiConfig() ")); debugln();

  if (!initializeFS()) {
    debug(F("LittleFS initialize failed ")); debugln();
    setInitialWifiConfig();
    return &storage;
  }
  
  if (!initConfigCalled) {
    initConfigCalled = true;
    loadWifiConfig(); // loads global storage, or sets default if missing
    // If the file was missing, loadWifiConfig returns defaults, so we save them.
    saveWifiConfig(storage); // save
    loadWifiConfig(); // reload global
    return &storage; // loaded by setupAP
  }
  return &storage; // loaded by setupAP
}

/** call this in loop() every loop()
 * Starts the Access Point for configuration.
 */
void startConfigAP() {
  if (inConfigMode) {
    return; // Already in config mode
  }
  setupAP(wifiWebConfigAP, wifiWebConfigPASSWORD); // sets inConfigMode
}
/** call this in loop() every loop()
  returns true if in config mode
  in loop() have at the top
  void loop() {
  if (handleWifiConfig()) {
  return; // skip rest of the loop
  }
  Reboots the device is the configTimer has expired
*/
bool handleWifiConfig() {
  if (!inConfigMode) {
    return false; // not doing wifi config so just ignore this call
  }
  if (endConfigTimer.justFinished()) {
    ESP.restart(); 
  }
  return true;
}

// loads global storage and returns pointer to it
static struct Wifi_CONFIG_storage_struct* loadWifiConfig() {
  setInitialWifiConfig();
  if (!initializeFS()) {
    debugln(F("FS failed to initialize"));
    debugln("set config");
    printWifConfig(storage);
    return &storage; // returns default if cannot open FS
  }
  if (!LittleFS.exists(wifiConfigFileName)) {
    debug(wifiConfigFileName); debug(" missing.");
    debugln("set config");
    printWifConfig(storage);
    return &storage; // returns default if missing
  }
  // else load config
  File f = LittleFS.open(wifiConfigFileName, "r");
  if (!f) {
    debug(wifiConfigFileName); debug(F(" did not open for read."));
    debugln("set config");
    printWifConfig(storage);
    return &storage; // returns default wrong size
  }
  if (f.size() != sizeof(storage)) {
    debug(wifiConfigFileName); debug(" wrong size.");
    f.close();
    debugln("set config");
    printWifConfig(storage);
    return &storage; // returns default wrong size
  }
  int bytesIn = f.read((uint8_t*)(&storage), sizeof(storage));
  if (bytesIn != sizeof(storage)) {
    debug(wifiConfigFileName); debug(" wrong size read in.");
    setInitialWifiConfig(); // again
    f.close();
    debugln("set config");
    printWifConfig(storage);
    return &storage;
  }
  f.close();
  // else return settings
  debugln("Loaded config");
  printWifConfig(storage);
  return &storage;
}

static bool saveWifiConfig(struct Wifi_CONFIG_storage_struct& storage) {
  if (!initializeFS()) {
    debugln(F("FS failed to initialize"));
    return false;
  }
  // else save config
  File f = LittleFS.open(wifiConfigFileName, "w"); // create/overwrite
  if (!f) {
    debug(wifiConfigFileName); debug(F(" did not open for write."));
    return false; // returns default wrong size
  }
  int bytesOut = f.write((uint8_t*)(&storage), sizeof(struct Wifi_CONFIG_storage_struct));
  if (bytesOut != sizeof(struct Wifi_CONFIG_storage_struct)) {
    debug(wifiConfigFileName); debug(" write failed.");
    return false;
  }
  // else return settings
  f.close(); // no rturn
  debug(wifiConfigFileName); debug(" config saved.");
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
    debugln(F("Wifi network scan failed"));
    return;
  }
  debugf("Scan done - found %i networks\n", n);
  int32_t maxRSSI = -10000;
  for (int8_t i = 0; i < n; ++i) {
    int32_t rssi_scan = WiFi.RSSI(i);
    if (rssi_scan > maxRSSI) {
      maxRSSI = rssi_scan;
      String ssid = WiFi.SSID(i);
      result = ssid.c_str();
    }
    debugf(" %s RSSI %idBm\n", result.c_str(), rssi_scan);
    delay(0);
  }
}

/*
   process index.html template
*/
static String processorConf(const String& var) {
  String rtnString = "";

  // set current visible tab
  if (var == "1") {
    rtnString = sfStrongestAP.c_str();
  } else if (var == "2") {
    rtnString = storage.password;
  } else if (var == "NAME") {
    rtnString =  String(CONTROLLER_NAME);
    rtnString.replace("_", " ");
  }
  return rtnString;
}

static void handleConfig(AsyncWebServerRequest *request) {
  int params = request->params();
  cSFA(sfSSID, storage.ssid);
  cSFA(sfPW, storage.password);
  if (params > 0) {
    for (int i = 0; i < params; i++) {
      const AsyncWebParameter* param = request->getParam(i);
      if (param && param->isPost()) {
        String decoded;
        switch (param->name().charAt(0)) {
          case '1': // BT idnum - dropdown so no parsing
            decoded = urlDecode(param->value()); // result is always <= source so just copy over
            decoded.trim();
            sfSSID = decoded.c_str();
          break;
          case '2': // PIN
            decoded = urlDecode(param->value()); // result is always <= source so just copy over
            decoded.trim();
            if (decoded != "*") {
              // update it
              sfPW = decoded.c_str();
            }
          break;
        }
      }
    }
    debugln();
    printWifConfig(storage);

    // store the settings
    if (!saveWifiConfig(storage)) {
      loadWifiConfig(); // re-initialize
    }
    endConfigTimer.start(RESTART_AFTER_CONFIG_MS); // restart in 30sec
  } // else if no args just return current settings

  delay(0);
  loadWifiConfig();
  debugln();
  printWifConfig(storage);
}

/**
   sets up AP and loads current wifi settings
*/
static void setupAP(const char* ssid_wifi, const char* password_wifi) {
  /**
    Start scan WiFi networks available
    @param ssid*         scan for only this ssid (NULL for all ssid's)
    @return Number of discovered networks
  */
  inConfigMode = true; // in config mode
  debugln(F("Setting up Access Point for Web Config"));
  scanForStrongestAP(sfStrongestAP);  // get best AP before we enable our AP!
  // connect to temporary wifi network for setup
  WiFi.softAPConfig(LOCAL_IP, GATEWAY_IP, SUBNET_IP);
  WiFi.softAP(ssid_wifi, password_wifi);
  debugf("Access Point %s setup - IP address: %s\n", ssid_wifi,  WiFi.softAPIP().toString().c_str());
  delay(10);

  webserver.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    if (endConfigTimer.justFinished()) ESP.restart();
    request->send(LittleFS, "/indexc.html", String(), false, processorConf);  
    });
  webserver.on("/config", HTTP_POST, [](AsyncWebServerRequest *request)
      { handleConfig(request); request->send(LittleFS, "/config.html", String(), false, processorConf); 
    });

  webserver.begin();

  debugln ( "HTTP webserver started" );
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
  // oled.begin(SSD1306_SWITCHCAPVCC, 0x3C, true) // .. OR ...
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

bool isWifiConfigMode() {
  return inConfigMode;
}