/**

ESP32 compatible version!

A comprehensive web-based controller for managing timers, sensors and control outputs.
Supports up to 127 controls with multiple timers per control.  Each
timer can be controlled by a thermostat/switch with user-programmable states.

   V2.1.1

   2026/09/17         Added mDNS - device appears on local network as 'EXAMPLE.local' (or 
                      whatever controller name is in the settings - name now has to be 
                      domain-name compliant).  Added urldecode to POST strings. Prevent GPIO 
                      analog read if no sensor expander available (bug).
   2025/12/16         Added Mutex to all file access.  Async down/uploads will wait for
                      1 sec to acquire a mutex (and release it if the browser aborts the file 
                      transfer). Main loop file actions will wait for 6 secs (worst case max 
                      filesize down/upload times).  Uses local scopt header buffers for all Async
                      functions to avoid potential memory corruption.  Async always uses the 
                      supplied control# (i.e. main loop does not sync control# to the browser)..
   2025/12/13         Changed FS from LittleFS to SPIFFS to improve file read performance.
   2025/12/09         Uses local scope timer buffers for all Async operations and, for loop(() 
                      ops a single control buffer (isCtlrOn()) used to check control states 
                      every couple of secs (requires a read of all controls and timer 
                      values so heavy file IO).
   2025/12/05         Has single header and timer buffers for Async operations (saves
                      ~7K RAM, 800b Flash)
   2025/12/03         Allows sensors to be specified by the user.  System will restart 
                      if the number of sensors is changed.  All config files can be backed
                      up and restoed as one file.
   2025/11/10         Added weekday, date & month to on and off times in each timer.
                      Changed GET to POST for control config request - GET can't send 
                      enough data if many timers specified. Now has user-defined controls
                      specifying name, pin, num_timers, etc.
   2025/11/06         Fixed bug - saves current state in timer struct now (was saved
                      per stat).
   20205/11/04        Added with/outwith range options to less/more than using the 
                      hysteresis value (i.e active within/outwith a range of temps).
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


1.  If using a static IP and Google DNS then DNS does not work (won't resolve any site 
    names).  If using a local router DNS then external addresses are resolved OK BUT ntp 
    doesn't return any data (UDP blocked?).

    My current router has a DHCP range of 192.168.1.63-253. Gateway is 192.168.1.254.
    May be an issue with the router blocking non-DHCP IPs or UDP from gateway access?
    https://forum.arduino.cc/t/dhcp-vs-static-ip-solved/202120/4).

2.  FS uses SPIFFS instead of LittleFS.  LittleFS takes 20-30mS per file read (50-70 on ESP32!), SPIFFS takes 5mS
    The ctrlIsOn() functon reads all controls/timers to check on/off states.  With littleFS this can take
    3.7ses (8266 or 14sec for ESP32!!!!) if 126 ctrls.  SPIFFS should take around 0.64secs which is more acceptable.
    Downside of SPIFSS is that its deprecated.  

    Alternatively, keep all ctrl headers in memory - this would half the read time in isCtrlOn() - (still be 1.8s/7secs 
    worst case).  Would need a FIFO with the config updates in the loop().

    SPIFFS tests show it loads a single controls data in ~2-5mSecs (ESP32: if 126 ctrls = 0.63secs max), LittleFS takes 
    ~78mSecs (10.7secs!).

TODO:

1.  Add Tuya cloud support.

Memory use:

Release ESP32
RAM:   [==        ]  16.7% (used 54760 bytes from 327680 bytes)
Flash: [=======   ]  72.3% (used 947437 bytes from 1310720 bytes)
8266
RAM:   [=====     ]  51.5% (used 42164 bytes from 81920 bytes)
Flash: [====      ]  38.9% (used 406397 bytes from 1044464 bytes)

ESP32 debug
RAM:   [==        ]  16.7% (used 54776 bytes from 327680 bytes)
Flash: [=======   ]  74.7% (used 979581 bytes from 1310720 bytes)
8266 debug
// uses mutex &  dynamic buffers for hdrs and timers - no common buffers to avoid Async issues!
RAM:   [=====     ]  53.9% (used 44152 bytes from 81920 bytes)
Flash: [====      ]  42.5% (used 443756 bytes from 1044464 bytes)
*/

#include "main.h"

// use #define ESP_LED 2 for ESP-01S (Blue led on GPIO2)
// use #define ESP_LED 1 for ESP-01 (Blue led on TX (GPIO1), ESP-01 also has a Red power led)
#define ESP_LED 2

#ifdef ESP32
   SemaphoreHandle_t FSmutex = xSemaphoreCreateMutex();
#else
  mutex_t * FSmutex = new int32_t(1);

// try to get a mutex
// returns true if successful, false if mutex not free
// as the esp8266 doesn't support the atomic S32C1I instruction
// we have to make the code uninterruptable to produce the
// same overall effect
bool ICACHE_FLASH_ATTR GetMutex(mutex_t *mutex) {
	int iOld = 1, iNew = 0;
	asm volatile (
		"rsil a15, 1\n"    // read and set interrupt level to 1
		"l32i %0, %1, 0\n" // load value of mutex
		"bne %0, %2, 1f\n" // compare with iOld, branch if not equal
		"s32i %3, %1, 0\n" // store iNew in mutex
		"1:\n"             // branch target
		"wsr.ps a15\n"     // restore program state
		"rsync\n"
		: "=&r" (iOld)
		: "r" (mutex), "r" (iOld), "r" (iNew)
		: "a15", "memory"
	);
	return (bool)iOld;
}

// release a mutex
void ICACHE_FLASH_ATTR ReleaseMutex(mutex_t *mutex) {
	*mutex = 1;
}

#endif

#include "TZ.h"  // for list of pre-generated TZ POSIX strings
// set a compiled default TZ here. Can be overrided/edited later by webpage.
#define DEFAULT_TZ TZ_Europe_London

static char _default_tz_[50]; // to hold the default TZ loaded from file by get_ntpSupport_DefaultTZ

// set a compiled default TZ here. Can be overrided/edited later by webpage.
const char *get_ntpSupport_DefaultTZ() {
  strncpy_P(_default_tz_, DEFAULT_TZ, sizeof(_default_tz_));
  _default_tz_[sizeof(_default_tz_) - 1] = '\0'; // terminate it incase DEFAULT_TZ was too long
  return _default_tz_; // TZ.h has #define TZ_Etc_GMTm0 PSTR("GMT0")  same as <+0>0
}

// uses ESP-LED define at the top of this file
PinFlasher flasher(ESP_LED, true); // GPIO2, invert i.e. active LOW

#if OLED_TYPE != oled_none
  // 128x64 OLED on I2C SDA - IO4, SCL - IO5 (D1 & D2 on D1 Mini board!)
  #if (OLED_TYPE == oled_96)
    Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, -1); // 0.96" OLED, -1 = no reset pin
  #elif (OLED_TYPE == oled_13)
    Adafruit_SH1106 oled(-1);     // 1.3" OLED, -1 = use I2C Wire interface
  #endif
#endif

static millisDelay showTimeTimer;
static unsigned long SHOW_TIME_MS = 120000; // 2min

struct Wifi_CONFIG_storage_struct* wifiConfigPtr;

static millisDelay heapTimer;

static int totTimers = 0;

//=================================
void systemInit() {
#ifdef ESP8266
    Wire.setClockStretchLimit(40000); // Set timeout to ~500us (at 80MHz) to prevent WDT reset on stuck I2C
#endif
  // if settings exists
  if (!SPIFFS.exists("/settings.bin") || loadConfig("/settings.bin", &settings, sizeof(settings_struct), 0) != sizeof(settings_struct)) {
    initSettings();
    newConfig("/settings.bin", &settings, sizeof(settings_struct));
  } else {
    // Ensure null termination after loading from file
    settings.name[MAX_SYS_NAME_LEN] = '\0';
    settings.tzStr[MAX_TZ_LEN] = 0;
  }

  if (!SPIFFS.exists("/sensors.bin")) {
    initSensors();
    newConfig("/sensors.bin", (sensor_struct *)sensors, 0); // create new empty file
  } else
    loadConfig("/sensors.bin", (sensor_struct *)sensors, sizeof(sensors), 0); // load any size file!
  snsrCount = getFileSize("/sensors.bin") / sizeof(sensor_struct); 
  initAllSnsrIO();

  // read every available control header and setup ctrlOnModes values, 
  // init GPIO, count timers and controls
  header_struct tmpHeader;
  ctrlCount = 0;
  while (ctrlCount < MAX_CONTROLS && SPIFFS.exists(String("/control_") + String(ctrlCount) + ".bin")) {
    uint8_t tmrCount = loadHeader(ctrlCount, &tmpHeader); // Load the specified control context, rtns tmrCount
    if (tmrCount) {
      ctrlOnMode[ctrlCount] = tmpHeader.modes & 0x0F; // b0-7. b15 clr is ctrl is currently off
      initCtrlIO(tmpHeader.pinNum);
      totTimers += tmrCount; // accumulate # of timers
      ctrlCount++;
    } else break; // no timers found
  }

  if (ctrlCount == 0) // no controls found so make a default Control0
    makeNewControl(0); // make control_0.bin & set ctrlcount = 1
}

//==================================================

void setup() {

#ifndef ESP32
//   CreateMutux(FSmutex); // Create a binary semaphore for ESP8266
// setup a new mutex
	*FSmutex = int32_t(1);
//}
#endif

  Serial.begin(115200);
#ifdef DEBUGPORT
  for (int i = 3; i > 0; i--) {
    Serial.print(i); Serial.print(' ');
    delay(1000);
  }
  Serial.println();
  debugln(" Debug running");
#endif

#if (OLED_TYPE != oled_none)
  startOLED(); // initialize OLED display with address 0x3C for 128x64
  oled.setTextSize(1);         // set text size
  oled.setTextColor(WHITE);    // set text color
  oled.setCursor(0, 0);       // set position to display (10 cosl x 8 rows)
  oled.println("Timer Controller");
  oled.println("Initialising ... ");
  oled.display();              // display on OLED
#endif

  if (!initializeFS()) {   // Call it once here.
    debugln("FATAL ERROR - filesystem failed to initialize");
#if (OLED_TYPE != oled_none)
    oled.println("FATAL ERROR - filesystem failed");
    oled.display();              // display on OLED
#endif
    delay(4000);
    ESP.restart(); // reboot to try again?
  }

  #ifdef ESP32
    debugf(" totalBytes: %i\n", SPIFFS.totalBytes());
    debugf(" usedBytes: %i\n", SPIFFS.usedBytes());
  #else
    FSInfo fs_info;
    SPIFFS.info(fs_info);
    debugf(" totalBytes: %i\n", fs_info.totalBytes);
    debugf(" usedBytes: %i\n", fs_info.usedBytes);
    debugf(" blockSize: %i\n", fs_info.blockSize);
  #endif

  systemInit(); // init file system, load config, setup hardware

printSettings();

debugf("sizeof settings = %i\n", sizeof(settings_struct));
debugf("sizeof sensors = %i\n", sizeof(sensors));
debugf("sizeof header = %i\n", sizeof(header_struct));
debugf("sizeof timer = %i\n", sizeof(timer_struct));
debugf("sizeof control = %i\n", sizeof(control_struct));
debugf("  ctrlCount:%i, totTimers:%i, snsrCount:%i\n\n", ctrlCount, totTimers, snsrCount);

#ifndef ESP32
  WiFi.persistent(false);
#endif
  WiFi.mode(WIFI_OFF); // force begin
  WiFi.setAutoConnect(false); // does not work for static ip see https://github.com/esp8266/Arduino/issues/2735
  WiFi.setAutoReconnect(true); // try to reconnect if we loose the connection

  delay(2000); // wifi setup delay

  wifiConfigPtr = initializeWifiConfig(); // goes into config mode called again below, also loads wifiConfig (perhaps with default settings)
  if (handleWifiConfig()) {
    return; // in config mode so skip rest of setup
  }

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
#if (OLED_TYPE != oled_none)
    oled.println("Connecting to network");
    oled.println(wifiConfigPtr->ssid);
    oled.display();              // display on OLED
#endif
  } else {
    debugln("   Already connected to Wifi");
  }
  // Wait for connection for 30sec
  unsigned long pulseCounter = (30 * 1000) / 100; // count down to zero 30 secs in 0.1sec
  while ((WiFi.status() != WL_CONNECTED) && pulseCounter) {
    pulseCounter--;
    delay(100); // short delay to call flasher.update() often
    flasher.update();
    debug(".");
#if (OLED_TYPE != oled_none)
    if ((pulseCounter & 0x0F) == 0) { // 18 dots in 30sec
      oled.print('.');
      oled.display();              // display on OLED
    }
#endif
  }
  if (WiFi.status() != WL_CONNECTED) {    // start AP to setup  wifi connection
    startConfigAP(); //starts AP
    // AP will exit and reboot after 5min so if un-attended
    // will try to connect to Wifi for 30sec every 5 1/2mins
    return; // skip the rest of the setup
  }

  String nIP = "http://" + WiFi.localIP().toString();
  debugf("\nConnected to %s as %s\n", wifiConfigPtr->ssid, nIP.c_str());
  if (!MDNS.begin(settings.name)) { // Start the mDNS responder
    debugln("Error setting up MDNS responder!");
  } else {
    debugf("MDNS responder started at http://%s\n", settings.name);
    MDNS.addService("http", "tcp", 80);
  }

#if (OLED_TYPE != oled_none)
  printIPconfig(nIP.c_str());
#endif
  startWebServer();
  initializeSNTP();
  showTimeTimer.start(SHOW_TIME_MS);
//  showTimeDebug();
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
//    if (myfree < lastFreeHeap) {
//      lastFreeHeap = myfree; // checks for leaks as it'll keep going down!
    if (mymax < lastFreeHeap) {
      lastFreeHeap = mymax; // max contiguous space avail.
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
