/**


A comprehensive controller for managing timers, sensors, and control outputs, with a web-based 
interface for configuration and monitoring.

   V2.0.0
   2024/12/12 V2.0.0  Removed static IP option (most routers seem to block external
                      UDP (or IP) access for non-DHCP clients.
                      Added OLED display with QR codes for WiFi config and to show 
                      DHCP-assigned IP.
                      Added compile-time multiple controls and timers per control.  
                      Each timer can have a digital or analog (thermistor) sensor 
                      with lessthan (heat demand)  and morethan (cool demand) 
                      triggers. Sensors can be shared between timers, multiplexed
                      and scaled individually. In ctrlDefs.h - search CONTROLS:
                      Added boost and advance to each control.
                      Added user-defined control names.
                      Now uses on-demand javascript fetch to load each control data 
                      into web page and to save updated configuration data.
                      Added upload/download of timer configuration data bin file.
   2022/01/11 V1.1.0  Added DHCP server to wifiConfig AccessPoint
  
   Inspired by (and includes code from) Matthew Ford,  2021/12/06
   (c)2021-2022 Forward Computing and Control Pty. Ltd.
   https://www.forward.com.au/pfod/HomeAutomation/PowerTimer/index.html   
   NSW, Australia  www.forward.com.au
   This code may be freely used for both private and commerical use.
   Provide this copyright is maintained.

1.  The web interface is designed to be used on a local network.  It is not secure 
    and should not be exposed to the internet.  It is intended to be used to configure
    and monitor the controller.  The controller can be configured to connect to the 
    local network using the web interface and then the web interface can be used to 
    monitor and configure the controller.
    
2.  To transfer files to littleFS storage using PlatformIO:
    Files MUST be in /data folder under project
    Click the PIO icon at the left side bar. The project tasks should open.
    Select board env: name (depends on the board you’re using).
    Expand the Platform menu.
    Select Build Filesystem Image.
    Make sure the serial monitor isn't connected to the board then 
    click Upload Filesystem Image.

3.  If you enter the wrong WiFi password then either wipe the flash OR
    upload a new Filesystem Image in platformIO (this will delete any WiFi & 
    control/timer settings).  If no network SSID has been saved  it will try to connect 
    to 'wifiSSID' / wifiPassword' (wifiConfig.cpp) before setting up the access point to let 
    you configure the network.  Change those values to your local network to improve 
    reliability if you will only connect to this network.  Change 'wifiWebConfigAP' to
    whatever you want to call the configuration access point setup by this device.

4.  If using a static IP and Google DNS then DNS does not work (won't resolve any site 
    names).  If using local router DNS then external addresses are resolved OK BUT ntp 
    doesn't return any data (UDP blocked?).

    Currently my router has a DHCP range of 192.168.1.63-253. Gateway is 192.168.1.254.
    May be an issue with the router blocking non-DHCP IPs or UDP from gateway access?
    https://forum.arduino.cc/t/dhcp-vs-static-ip-solved/202120/4).


The web interface loads each control tab html content into the home page when the user selects 
a control to view/modify.  If the user selects another then it is loaded onto the page at that 
time.  This considerably reduces the size of served html (there is limited serving capacity on 
ESP8266/32) and allows the number of controls to be increased up to a (theoretical) 127 without 
any noticeable performance hit.  

Timers are currently fixed at 4 per control max. BUT it would be possible to increase this 
significantly by also serving timer html on demand as per control html.  This device will 
usually be operated on a local network so multiple html requests per page won't normally be a 
significant issue.

*/


#include "DebugOut.h"
#include "millisDelay.h"
#include "LittleFSsupport.h"
#include "wifiConfig.h"
#include "webPages.h"
#include "timerControl.h"
#include "tzPosix.h"
#include "PinFlasher.h"
#include <Wire.h>
#include <ESP8266WiFi.h>
#include <SPI.h>

// use #define ESP_LED 2 for ESP-01S (Blue led on GPIO2)
// use #define ESP_LED 1 for ESP-01 (Blue led on TX (GPIO1), ESP-01 also has a Red power led)
#define ESP_LED 2

// for ESP-01 i.e. Blue led on TX pin, if #define DEBUG is un-commented, led will not flash
// once you have finished debugging comment out #define DEBUG to enable ESP-01 to use GPIO1 (TX) to flash Blue led
// for ESP-01S, Blue led is on GPIO2 and will flash even if debugging is enabled
//#define DEBUG
static Stream* debugPtr = NULL;  // local to this file

#include <TZ.h>  // for list of pre-generated TZ POSIX strings
// set a compiled default TZ here. Can be overrided/edited later by webpage.
#define DEFAULT_TZ TZ_Europe_London

// see wifiConfig.cpp to set the AccessPoint ssid and password, default is ESP8266_wifiConfig / 1234567890

static char _default_tz_[50]; // to hold the default TZ loaded from PROG memory by get_ntpSupport_DefaultTZ

// set a compiled default TZ here. Can be overrided/edited later by webpage.
// a method to give ntpSupport access to the default tz
const char *get_ntpSupport_DefaultTZ() { // magic name picked up by ntpSupport.cpp
  strncpy_P(_default_tz_, DEFAULT_TZ, sizeof(_default_tz_));
  _default_tz_[sizeof(_default_tz_) - 1] = '\0'; // terminate it incase DEFAULT_TZ was too long
  return _default_tz_; // TZ.h has #define TZ_Etc_GMTm0 PSTR("GMT0")  same as <+0>0
}

// uses ESP-LED define at the top of this file, 1 for ESP-01,  2 for ESP-01S
PinFlasher flasher(ESP_LED, true); // GPIO2, invert i.e. active LOW

static millisDelay showTimeTimer;
static unsigned long SHOW_TIME_MS = 120000; // 2min

struct Wifi_CONFIG_storage_struct* wifiConfigPtr;

static millisDelay heapTimer;

//==================================================

void setup() {

  Serial.begin(115200);
#ifdef DEBUG
  for (int i = 3; i > 0; i--) {
    Serial.print(i); Serial.print(' ');
    delay(1000);
  }
  Serial.println();
  debugPtr = initializeDebugOut(Serial); // only need to call this in setup
  // debugPtr = getDebugOut(); // other files call this to get debug stream
  debugPtr->println(" Debug running");
#endif

  initOutputs();

  startOLED();// initialize OLED display with address 0x3C for 128x64
  
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF); // force begin
  WiFi.setAutoConnect(false); // does not work for static ip see https://github.com/esp8266/Arduino/issues/2735
  WiFi.setAutoReconnect(true); // try to reconnect if we loose the connection
  initializeFS();

  delay(2000); // wifi setup delay

  wifiConfigPtr = initializeWifiConfig(); // goes into config mode called again below, also loads wifiConfig (perhaps with default settings)
  if (handleWifiConfig()) {
    return; // in config mode so skip rest of setup
  }
  if (debugPtr)
    listDir("/");
  
  FSInfo fs_info;
  LittleFS.info(fs_info);
  listDir("/", Serial);
  Serial.printf(" totalBytes: %i\n", fs_info.totalBytes);
  Serial.printf(" usedBytes: %i\n", fs_info.usedBytes);
  Serial.printf(" blockSize: %i\n", fs_info.blockSize);
  Serial.printf(" pageSize: %i\n\n", fs_info.pageSize);

  Serial.printf("sizeof timerConfig = %i\n", sizeof(timerConfig_struct));
  Serial.printf("sizeof control = %i\n", sizeof(control_struct));
  Serial.printf("sizeof timer = %i\n", sizeof(timer_struct));

  cSFA(sfSSID, wifiConfigPtr->ssid);
  sfSSID.trim();
  if (sfSSID.isEmpty()) { // no SSID so start AP to set up wifi
    initializeWifiConfig(); // starts AP on second call and displays QR code for network
    // AP will exit and reboot after 5min so if un-attended
    // will try to connect to Wifi for 30sec ever 5 1/2mins
    return; // skip the rest of the setup
  }

  // ELSE
  // ======================= connect to router ===================
  // else connect to wifi and start webserver
  WiFi.mode(WIFI_STA);
  delay(2000);
 
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.begin(wifiConfigPtr->ssid, wifiConfigPtr->password);
    flasher.setOnOff(100);
    if (debugPtr) {
      debugPtr->print("   Connecting to Wifi Network ");
      debugPtr->print(wifiConfigPtr->ssid);
      debugPtr->print(" passw:");
      debugPtr->println(wifiConfigPtr->password);
    }
  } else {
    if (debugPtr) {
      debugPtr->println("   Already connected to Wifi");
    }
  }
  // Wait for connection for 30sec
  unsigned long pulseCounter = 0;
  unsigned long maxCount = (30 * 1000) / 100; // delay below
  while ((WiFi.status() != WL_CONNECTED) && (pulseCounter < maxCount)) {
    pulseCounter++;
    delay(100); // short delay to call flasher.update() often
    flasher.update();
    if (debugPtr) {
      debugPtr->print(".");
    }
  }
  if (WiFi.status() != WL_CONNECTED) {    // start AP to setup  wifi connection
    initializeWifiConfig(); //starts AP on second call
    // AP will exit and reboot after 5min so if un-attended
    // will try to connect to Wifi for 30sec every 5 1/2mins
    return; // skip the rest of the setup
  }

  String nIP = "http://" + WiFi.localIP().toString();
  if (debugPtr) {
    debugPtr->printf("\nConnected to %s as %s\n", wifiConfigPtr->ssid, nIP.c_str());
  }
  printIPconfig(nIP.c_str());

  startWebServer();
  initializeSNTP();
  showTimeTimer.start(SHOW_TIME_MS);
  showTimeDebug();
  // testPosix();
  startRebootTimer();

}

//================================================================

#ifdef DEBUG
  uint32_t lastFreeHeap = 0xffffffff;
  uint32_t myfree;
  uint32_t mymax;
  uint8_t myfrag;
#endif

void loop() {

// https://github.com/me-no-dev/ESPAsyncWebServer/pull/134

#ifdef DEBUG
  if (!heapTimer.isRunning())
    heapTimer.start(1000);
  else if (heapTimer.justFinished()) {
    ESP.getHeapStats(&myfree, &mymax, &myfrag);
    lastFreeHeap = myfree;
    Serial.printf(">free:%5d,max: %5d,frag: %3d%%\r\n", myfree, mymax, myfrag);
  }
#endif

  flasher.update();
  pushDebugOut(); // push as much buffereed debug data out as we can, does nothing if debug not initialized
  //  .. other code that MUST run all the time

  if (handleWifiConfig()) {
    flasher.setOnOff(1000); // ignored if already flashing at 1sec
    return;
  }

  // ..  other stuff that will be skipped when in wifi config mode
  //-------------------------------------------------------------

  saveConfigIfNeeded();
  
  // set LED flash rate
  if (WiFi.status() != WL_CONNECTED || missedSNTPupdate()) { // no WiFi or missed a scheduled SNTP update {
    flasher.setOnOff(100); // ignored if already flashing at 100ms
  } else {
    // else wifi connected AND have got the scheduled ntp updates so make led solid ON
    flasher.setOnOff(PIN_ON); // ignored if already ON
  }

  setOutputs(); // check temps &  set control output pins and reboot if turning off in auto

}
