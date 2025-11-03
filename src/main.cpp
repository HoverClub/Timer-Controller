/**

ESP32 compatible version!

A comprehensive web-based controller for managing timers, sensors and control outputs.
Supports up to 127 controls with multiple timers per control (compile options).  Each
timer can be controlled by a thermostat/switch with user-programmable states.

   V2.0.1
   2025/10/26         Simplified logic for initializeWifiConfig(). Also for control and
                      pins arrays (now auto-sized). Added ESP32 build options (changes 
                      to timer logic, etc.).  Added support for I2C ADC1115 modules (ADC 
                      extenders) and for MCP23017 digital IO expanders.  Hysteresis
                      now available for sensor inputs.

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
    names).  If using a local router DNS then external addresses are resolved OK BUT ntp 
    doesn't return any data (UDP blocked?).

    My current router has a DHCP range of 192.168.1.63-253. Gateway is 192.168.1.254.
    May be an issue with the router blocking non-DHCP IPs or UDP from gateway access?
    https://forum.arduino.cc/t/dhcp-vs-static-ip-solved/202120/4).


The web interface loads each control tab html content into the home page on demand (user clicks 
control tab)   If the user selects another then it is loaded onto the page at that time.  
This considerably reduces the size of served html (there is limited serving capacity on 
ESP8266/32) and allows the number of controls to be increased up to a (theoretical) 126 without 
any noticeable performance hit (other than increased RAM usage - ~10K max. which is perfectly 
feasible for either supported device).

Timers are currently fixed at 4 per control max. (MAX_TIMERS) BUT it is possible to increase this 
significantly by also serving timer html on demand as per control html.  This device will usually 
operate on a local network so multiple html requests per page won't normally be a significant issue.


TODO:
1. Add median buffers to temperature inputs to avoid output glitching in noisy environments.
2. Serve timers to web page on-demand to reduce size of index.html file.
3. 
4. Add compile time names for sensors?  Not very flexible if device is used for different purposes?  
   Could be user-defined in settings page?  


Memory use:

Release ESP32
RAM:   [=         ]  14.8% (used 48404 bytes from 327680 bytes)
Flash: [=======   ]  70.5% (used 924021 bytes from 1310720 bytes)
8266
RAM:   [====      ]  43.5% (used 35608 bytes from 81920 bytes)
Flash: [====      ]  36.7% (used 383009 bytes from 1044464 bytes)

ESP32 debug
RAM:   [=         ]  14.8% (used 48604 bytes from 327680 bytes)
Flash: [=======   ]  72.7% (used 952733 bytes from 1310720 bytes)8266 debug
// with ADC1115 I2C
RAM:   [=         ]  14.8% (used 48636 bytes from 327680 bytes)
Flash: [=======   ]  73.1% (used 958409 bytes from 1310720 bytes)
// with hysteresis
RAM:   [=         ]  14.8% (used 48484 bytes from 327680 bytes)
Flash: [=======   ]  73.1% (used 958705 bytes from 1310720 bytes)

8266 debug
RAM:   [=====     ]  46.2% (used 37868 bytes from 81920 bytes)
Flash: [====      ]  40.3% (used 421324 bytes from 1044464 bytes)
// with MCP23017 &  ADC1115 I2C support
RAM:   [=====     ]  46.4% (used 38000 bytes from 81920 bytes)
Flash: [====      ]  40.6% (used 423568 bytes from 1044464 bytes)
// with hysteresis
RAM:   [=====     ]  46.3% (used 37928 bytes from 81920 bytes)
Flash: [====      ]  40.6% (used 424184 bytes from 1044464 bytes)



   Includes code from) Matthew Ford,  2021/12/06
   (c)2021-2022 Forward Computing and Control Pty. Ltd.
   https://www.forward.com.au/pfod/HomeAutomation/PowerTimer/index.html   
   NSW, Australia  www.forward.com.au
*/


#include "main.h"

// use #define ESP_LED 2 for ESP-01S (Blue led on GPIO2)
// use #define ESP_LED 1 for ESP-01 (Blue led on TX (GPIO1), ESP-01 also has a Red power led)
#define ESP_LED 2

#include "TZ.h"  // for list of pre-generated TZ POSIX strings
// set a compiled default TZ here. Can be overrided/edited later by webpage.
#define DEFAULT_TZ TZ_Europe_London

static char _default_tz_[50]; // to hold the default TZ loaded from file by get_ntpSupport_DefaultTZ

// set a compiled default TZ here. Can be overrided/edited later by webpage.
// a method to give ntpSupport access to the default tz
const char *get_ntpSupport_DefaultTZ() { // magic name picked up by ntpSupport.cpp
  strncpy_P(_default_tz_, DEFAULT_TZ, sizeof(_default_tz_));
  _default_tz_[sizeof(_default_tz_) - 1] = '\0'; // terminate it incase DEFAULT_TZ was too long
  return _default_tz_; // TZ.h has #define TZ_Etc_GMTm0 PSTR("GMT0")  same as <+0>0
}

// uses ESP-LED define at the top of this file
PinFlasher flasher(ESP_LED, true); // GPIO2, invert i.e. active LOW

static millisDelay showTimeTimer;
static unsigned long SHOW_TIME_MS = 120000; // 2min

struct Wifi_CONFIG_storage_struct* wifiConfigPtr;

static millisDelay heapTimer;

//==================================================

void setup() {

  Serial.begin(115200);
#ifdef DEBUGPORT
  for (int i = 3; i > 0; i--) {
    Serial.print(i); Serial.print(' ');
    delay(1000);
  }
  Serial.println();
  debugln(" Debug running");
#endif

  setInitialtimerConfig(); // init config with correct pin#'s
  initGPIO ();
  startOLED(); // initialize OLED display with address 0x3C for 128x64
  
#ifndef ESP32
  WiFi.persistent(false);
#endif
  WiFi.mode(WIFI_OFF); // force begin
  WiFi.setAutoConnect(false); // does not work for static ip see https://github.com/esp8266/Arduino/issues/2735
  WiFi.setAutoReconnect(true); // try to reconnect if we loose the connection
  initializeFS();

  delay(2000); // wifi setup delay

  wifiConfigPtr = initializeWifiConfig(); // goes into config mode called again below, also loads wifiConfig (perhaps with default settings)
  if (handleWifiConfig()) {
    return; // in config mode so skip rest of setup
  }

  listDir("/");
  #ifdef ESP32
    Serial.printf(" totalBytes: %i\n", LittleFS.totalBytes());
    Serial.printf(" usedBytes: %i\n", LittleFS.usedBytes());
  #else
    FSInfo fs_info;
    LittleFS.info(fs_info);
    Serial.printf(" totalBytes: %i\n", fs_info.totalBytes);
    Serial.printf(" usedBytes: %i\n", fs_info.usedBytes);
    Serial.printf(" blockSize: %i\n", fs_info.blockSize);
    Serial.printf(" pageSize: %i\n\n", fs_info.pageSize);
  #endif

  Serial.printf("sizeof timerConfig = %i\n", sizeof(timerConfig_struct));
  Serial.printf("sizeof control = %i\n", sizeof(control_struct));
  Serial.printf("sizeof timer = %i\n", sizeof(timer_struct));

  cSFA(sfSSID, wifiConfigPtr->ssid);
  sfSSID.trim();
  if (sfSSID.isEmpty()) { // no SSID so start AP to set up wifi
    startConfigAP(); // starts AP and displays QR code for network
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
    debugf("   Connecting to Wifi Network %s passw %s\n", wifiConfigPtr->ssid, wifiConfigPtr->password);
  } else {
    debugln("   Already connected to Wifi");
  }
  // Wait for connection for 30sec
  unsigned long pulseCounter = 0;
  unsigned long maxCount = (30 * 1000) / 100; // delay below
  while ((WiFi.status() != WL_CONNECTED) && (pulseCounter < maxCount)) {
    pulseCounter++;
    delay(100); // short delay to call flasher.update() often
    flasher.update();
    debug(".");
  }
  if (WiFi.status() != WL_CONNECTED) {    // start AP to setup  wifi connection
    startConfigAP(); //starts AP
    // AP will exit and reboot after 5min so if un-attended
    // will try to connect to Wifi for 30sec every 5 1/2mins
    return; // skip the rest of the setup
  }

  String nIP = "http://" + WiFi.localIP().toString();
  debugf("\nConnected to %s as %s\n", wifiConfigPtr->ssid, nIP.c_str());
  printIPconfig(nIP.c_str());

  startWebServer();
  initializeSNTP();
  showTimeTimer.start(SHOW_TIME_MS);
  showTimeDebug();
  // testPosix();
  startRebootTimer();

}

#ifdef DEBUGPORT
  uint32_t lastFreeHeap = UINT32_MAX;
 #ifndef ESP32
    uint32_t myfree;
    uint32_t mymax;
    uint8_t myfrag;
 #endif
#endif


//==================================================
void loop() {

// https://github.com/me-no-dev/ESPAsyncWebServer/pull/134

#ifdef DEBUGPORT
  if (!heapTimer.isRunning())
    heapTimer.start(1000);
  else if (heapTimer.justFinished()) {
  #if ESP32
    if (ESP.getMinFreeHeap() < lastFreeHeap) {
      lastFreeHeap = ESP.getMinFreeHeap();
      debugf("Heap total: %u, free: %u, max free: %u\n", ESP.getHeapSize(), ESP.getFreeHeap(), ESP.getMinFreeHeap())
    }
  #else
    ESP.getHeapStats(&myfree, &mymax, &myfrag); // esp8266 only
    if (myfree < lastFreeHeap) {
      lastFreeHeap = myfree;
      debugf(">free:%5d, maxAlloc: %5d, %3d%% frag\r\n", myfree, mymax, myfrag);
    }
  #endif
    heapTimer.repeat();
  }
#endif

  flasher.update();

  if (handleWifiConfig()) {
    flasher.setOnOff(1000); // ignored if already flashing at 1sec
    return; // ..  other stuff that will be skipped when in wifi config mode
  } 
  //-----------------------------------
  
  saveConfigIfNeeded();
  
  // set LED flash rate
  if (WiFi.status() != WL_CONNECTED || missedSNTPupdate()) { // no WiFi or missed a scheduled SNTP update {
    flasher.setOnOff(100); // ignored if already flashing at 100ms
  } else {
    // else wifi connected AND have got the scheduled ntp updates so make led solid ON
    flasher.setOnOff(PIN_ON); // ignored if already ON
  }

  setOutputs(); // check temps, set control output pins and reboot if turning off in auto mode

  #ifdef ESP32
    pollSntp(); // check for sntp time updates
  #endif

}
