/*
   timerControl.cpp
   by Matthew Ford,  2021/12/06
   (c)2021 Forward Computing and Control Pty. Ltd.
   NSW, Australia  www.forward.com.au
   This code may be freely used for both private and commerical use.
   Provide this copyright is maintained.
*/

// see https://github.com/arduino/esp8266/blob/master/cores/esp8266/sntp-lwip2.c
// and https://github.com/arduino/esp8266/blob/master/cores/esp8266/time.c
// and https://github.com/esp8266/Arduino/issues/4637 etc

//   Note: carefully ESP8266 TZ env uses -ve tz offset, i.e. Sydney EST is +10 but TZ str is EST-10....

// timezone offsets range from  UTC−12:00 to UTC+14:00 in down to 15min segments
// so just use current time to set offset in range -12 to +12 this will be a day off for those tz that are +13 and +14
// +hhmm are SUBTRACTED!! fromo UTC to get local time so take UTC and subtract user's Current Time to get TZ envirmental variable tzoffset rounded to 15mins
// offsets in range -12 < offset <= +12  i.e. -11:45 is the smallest offset and +12:00 is the largest
// e.g. UTC  = 14:00,  LC (localTime) UTC=04:00  tzoffset = +10:00
//      UTC = 08:00  LC = 22:00  tzoffset =  -14 => <=-12 so add 24,  -14+24 = +10
//      UTC = 14:00  LC = 22:00  tzoffset = -8:00
//      UTC = 20:00  LC = 4:00   tzoffset = 16:00 => >12 so subtract 24,  16-24 = -8:00

#include "main.h"

/*
struct timeval {
  time_t      tv_sec;
  suseconds_t tv_usec;
};
 time_t is an integral type that holds number of seconds elapsed since 00:00 hours, Jan 1, 1970 UTC (i.e., a unix timestamp).

struct tm;  
  Defined in header <time.h>
  Structure holding a calendar date and time broken down into its components.
  Member objects
  int tm_sec  seconds after the minute – [0, 61] (until C99)[0, 60] (since C99)[for leap second]
  int tm_min minutes after the hour – [0, 59]
  int tm_hour hours since midnight – [0, 23]
  int tm_mday day of the month – [1, 31]
  int tm_mon months since January – [0, 11]
  int tm_year years since 1900
  int tm_wday days since Sunday – [0, 6]
  int tm_yday days since January 1 – [0, 365]
  int tm_isdst Daylight Saving Time flag. The value is positive if DST is in effect, zero if not and negative if no information is available

  The Standard mandates only the presence of the aforementioned members in either order.
  The implementations usually add more data-members to this structure.
*/
static millisDelay ntpUpdateCheck;
static const unsigned long NTP_NOT_UPDATED_MS = 70ul * 60 * 60 *1000; //70mins  or testing 10ul *1000; // 10sec

// OLED display interface default pins
// SDA = 4; // D2 on D1 mini pcb
// SCL = 5; // D1
// on ESP32 SDA is 21, SCL 22

// LED
// LEDpin = 2; // D4 on mini pcb

// programming pin
// #define progPin 0 // D0 on MINI PCB - used to flash device


//----------------------------------
// memory based control data structure:
settings_struct settings;   // controller config data and timezone settings

// used by Async functions to modify and read control/timer/sensor values
volatile uint8_t tmrCount = 1;   // # of timers in ctrl currently loaded by Async setConfig

volatile sensor_struct sensors[MAX_SENSORS];  // all current sensor data
volatile uint8_t snsrCount = 0;  // # of sensors found

uint16_t volatile ctrlOnMode[MAX_CONTROLS]; // copy of control mode and current output state - avoids getStatus having to read ALL control header files 
                                            // b15 = control on/off state, b0-3 set control mode, b4-7 previous mode if curr mode is BOOST/ADV
                                            // b15 set by setOuputs and read by Async /getStatus handler.
int16_t volatile currSnsrVal[MAX_SENSORS];   // last sensor value read
uint8_t tmrSnsrState[MAX_CONTROLS][(MAX_TIMERS + 7 ) >> 3] = {0}; // one bit for each timers last on/off state (used to apply hysteresis)

volatile uint8_t ctrlCount = 1;  // # of control_xx.bin files found

// used by setOutputs()/isCtrlOn() when checking/setting output status:
static header_struct chkHeader; // temp buffer for control header
static timer_struct chkTimer;   // temp buffer for one timer

// Set up auto reboot to clean out any memory leaks
// if in AUTO reboot when turns off
// otherwise after ~24hrs reboot when power relay is OFF
millisDelay rebootIfOffDelay;
unsigned long REBOOT_IF_OFF_DELAY_MS = 24ul * 60 * 60 * 1000 + 10ul * 60 * 1000; // 24hrs + 10mins
bool rebootWhenOff = false;
extern "C" int clock_gettime(clockid_t unused, struct timespec *tp);

static bool haveSNTPresponse = false; // set true on first response
static bool haveSecondSNTPresponse = false; // got second one
static bool haveSNTPupdate = false;

unsigned int getLocalTime_mins(); // hh:mm in mins

Adafruit_ADS1115 * ADCexpander[4] = {nullptr}; // array of ADC1115 ADC drivers
MCP23017 * DIGexpander[4] = {nullptr}; // array of digital expanders

//=============================
static bool FS_initialized = false;

bool initializeFS() {
  if (FS_initialized) {
    return FS_initialized;
  }

  debugln("Mount SPIFFS");
  if (!SPIFFS.begin()) {
    debugln("SPIFFS mount failed");
    return FS_initialized;
  }
  // else
  FS_initialized = true;
  listDir("/");
  return FS_initialized;
}

void listDir(const char * dirname) {
  if (!FS_initialized) {
    debugln("FS not initialized yet");
    return;
  }

  debugf("Listing directory: %s\n", dirname);
#ifdef ESP32
  if (xSemaphoreTake(FSmutex, (4800 / portTICK_PERIOD_MS)) != pdTRUE) return; // couldn't get file access for 1sec
  File root = SPIFFS.open(dirname);
  if (root.isDirectory()) {
    File file;
    while (file = root.openNextFile()) {
      if (file.isDirectory()) {
        debugf("  DIR : %s\n", file.name());
      } else 
        debugf("  FILE: %s SIZE: %i\n", file.name(), file.size());
    }
  }
  xSemaphoreGive(FSmutex);
#else
  uint32_t mTimer = millis(); while (!GetMutex(FSmutex)) { if ((millis() - mTimer) > 6000) return;  delay(1); }
  Dir root = SPIFFS.openDir(dirname);
  while (root.next()) {
    File file = root.openFile("r");
    debugf("  FILE: %s  SIZE: %i \n", root.fileName().c_str(), root.fileSize());
    file.close();
  }
  ReleaseMutex(FSmutex);
#endif
}

int getFileSize(String name) {
  if (!FS_initialized) {
    debugln("FS not initialized yet");
    return false;
  }
#ifdef ESP32
  if (xSemaphoreTake(FSmutex,(4800 / portTICK_PERIOD_MS)) != pdTRUE) return 0; // couldn't get file access for 1sec
#else
  uint32_t mTimer = millis(); while (!GetMutex(FSmutex)) { if ((millis() - mTimer) > 6000) return 0;  delay(1); }
#endif
  int size = 0;
  if (SPIFFS.exists(name.c_str())) {
    File f = SPIFFS.open(name.c_str(), "r");
    size = f.size();
    f.close();
  }
#ifdef ESP32
  xSemaphoreGive(FSmutex);
#else
  ReleaseMutex(FSmutex);
#endif
  return size;
}

/// @brief 
/// @param ctrl 
/// @param num_timers 
/// @return 
bool truncateFile(uint8_t ctrl, uint8_t num_timers) {
  if (!FS_initialized) {
    debugln("FS not initialized yet");
    return false;
  }
  String name = "/control_" + String(ctrl) + ".bin";

#ifdef ESP32 // ESP32 SPIFFS doesn't support truncate function so copy shorter file
debugf("trunc ctrl %i %s, tmrs:%i\n", ctrl, name.c_str(), num_timers);
  header_struct hdrBuffer;
  timer_struct newTimer;
  const char* tempFileName = "/trunc.tmp";
  if (!loadHeader(ctrl, &hdrBuffer)) return false;; 
  if (!newConfig(tempFileName, &hdrBuffer, sizeof(header_struct))) return false;
  for (uint8_t i = 0; i < num_timers; i++) {
    if (!loadTimer(ctrl, i, &newTimer)) return false;
    // saveTimer expects a control index, but we need to write to the temp file
    if (!saveConfig(tempFileName, &newTimer, sizeof(timer_struct), sizeof(header_struct) + (i * sizeof(timer_struct)))) return false;
  }
  bool rtn = SPIFFS.remove(name);
  if (rtn) rtn = SPIFFS.rename(tempFileName, name);
  else SPIFFS.remove(tempFileName); // Cleanup if rename failed
  return rtn;
#else
  uint32_t mTimer = millis(); while (!GetMutex(FSmutex)) { if ((millis() - mTimer) > 6000) return 0;  delay(1); }
  bool rtn = false;
  File f = SPIFFS.open(name.c_str(), "r+");
  if (f) rtn = f.truncate(sizeof(header_struct) + (num_timers * sizeof(timer_struct)));
  f.close();
  ReleaseMutex(FSmutex);
  return rtn;
#endif
}

//--------------------------------------------
//   CONFIG file handlers

// load file data from 'name' to buffer starting at offset - up to maxSize in length
// returns # of bytes read if successful or zero if fail
// max size = 65535bytes!
size_t loadConfig(String name, void * buffer, uint16_t maxSize, uint16_t offset) {
//uint32_t tmr = millis();
//debugf("loading %s maxsize %i offs:%i\n", name.c_str(), maxSize, offset);
  int bytesIn = 0;
#ifdef ESP32
  if (xSemaphoreTake(FSmutex, (4800 / portTICK_PERIOD_MS)) != pdTRUE) return 0; // couldn't get file access for 1sec
#else
  uint32_t mTimer = millis(); while (!GetMutex(FSmutex)) { if ((millis() - mTimer) > 6000) return 0;  delay(1); }
#endif
  if (SPIFFS.exists(name)) { 
    File f = SPIFFS.open(name.c_str(), "r");
    if (f) {
      if (f.size() >= offset) { // make sure we can seek there!
        if (offset) f.seek(offset, SeekSet);
        bytesIn = f.read((uint8_t*)(buffer), maxSize);
      }
      f.close();
    }
  }
#ifdef ESP32
  xSemaphoreGive(FSmutex);
#else
  ReleaseMutex(FSmutex);
#endif
//debugf("  loadedCfg:%lu\n", millis() - tmr);
  return bytesIn;
}
// load one timer data from ctrl to a buffer
bool loadTimer(uint8_t ctrl, int8_t timer, timer_struct * buffer) {
  return loadConfig(
            String("/control_" + String(ctrl) + ".bin"), 
            buffer, 
            sizeof(timer_struct), 
            sizeof(header_struct) + (sizeof(timer_struct) * timer)
          ) == sizeof(timer_struct); // get timer
}
// loads control header into a buffer and returns # of timers in control file
// - zero if load failed.
uint8_t loadHeader(int8_t ctrl, header_struct * buffer) {
  String name = "/control_" + String(ctrl) + ".bin";
  uint8_t num_timers = 0;
#ifdef ESP32
  if (xSemaphoreTake(FSmutex, (4800 / portTICK_PERIOD_MS)) != pdTRUE) return 0; // couldn't get file access for 1sec
#else
  uint32_t mTimer = millis(); while (!GetMutex(FSmutex)) { if ((millis() - mTimer) > 6000) return 0;  delay(1); }
#endif
  if (SPIFFS.exists(name)) { 
    File f = SPIFFS.open(name.c_str(), "r");
    if (f) {
      int bytesIn = f.read((uint8_t*)buffer, sizeof(header_struct));
      if (bytesIn == sizeof(header_struct))
        num_timers = (f.size() - sizeof(header_struct)) / sizeof(timer_struct); // #timers in the file
    }
    f.close();
  }
#ifdef ESP32
  xSemaphoreGive(FSmutex);
#else
  ReleaseMutex(FSmutex);
#endif
//debugf("loadHdr %s tmrCount:%i :: ", name.c_str(), num_timers); printControlHdr(ctrl, buffer);
  return num_timers;
}

// save the config file
bool saveConfig(String name, void * buffer, uint16_t size, uint16_t offset) {
debugf("Save %s size %i offset:%i\n", name.c_str(), size, offset);
  // open for read-write, starting at beginning of file. Create if it doesn't exist.
  bool rtn = false;
#ifdef ESP32
  if (xSemaphoreTake(FSmutex, (4800 / portTICK_PERIOD_MS)) != pdTRUE) return false; // couldn't get file access for 1sec
#else
  uint32_t mTimer = millis(); while (!GetMutex(FSmutex)) { if ((millis() - mTimer) > 6000) return false;  delay(1); }
#endif
  File f = SPIFFS.open(name.c_str(), "r+"); // opened rd/wr file so can seek - 'r+' doesn't truncate or create file!
  if (f) {
    if (offset) f.seek(offset, SeekSet);
    int bytesOut = f.write((uint8_t*)(buffer), size);
    rtn = bytesOut == size;
    f.close();
  }
#ifdef ESP32
  xSemaphoreGive(FSmutex);
#else
  ReleaseMutex(FSmutex);
#endif
  return rtn;
}
// create a new config file and write buffer to start
bool newConfig(String name, void * buffer, uint16_t size) {

//if (name.startsWith("control")) { debug("newControl: "); printControlHdr(ctrlCount - 1, (header_struct *)buffer); }
  bool rtn = false;
#ifdef ESP32
  if (xSemaphoreTake(FSmutex, (4800 / portTICK_PERIOD_MS)) != pdTRUE) return false; // couldn't get file access for 1sec
#else
  uint32_t mTimer = millis(); while (!GetMutex(FSmutex)) { if ((millis() - mTimer) > 6000) return false;  delay(1); }
#endif

  File f = SPIFFS.open(name.c_str(), "w"); // creates new, empty file if it doesn't exist for writing and truncates it if is does exist!
  if (f) {
    int bytesOut = f.write((uint8_t*)(buffer), size);
debugf("NewConfig %s bytesOut:%i\n", name.c_str(), bytesOut);
    rtn = bytesOut == size; 
    f.close(); 
  }
#ifdef ESP32
  xSemaphoreGive(FSmutex);
#else
  ReleaseMutex(FSmutex);
#endif
  return rtn;
}

bool makeNewControl(int8_t ctrl) {  // make new control_xx.bin file
  header_struct hdrBuffer;
  initHeader(&hdrBuffer, ctrl);
  timer_struct newTimer;
  initTimer(&newTimer);
  if (newConfig(String("/control_" + String(ctrl) + ".bin"), &hdrBuffer, sizeof(header_struct))
   && saveTimer(ctrl, 0, &newTimer) 
   && saveTimer(ctrl, 1, &newTimer)
  ) { // write hdr and two timers
    ctrlCount++; // add a ctrl
    return true;
  }
  return false;
}
bool saveSensors() {
debugf("snsr saved cnt:%i\n", snsrCount);
    return saveConfig("/sensors.bin", (sensor_struct *)sensors, sizeof(sensor_struct) * snsrCount, 0); // save sensors file
}
// save timer data from buffer to control file
bool saveTimer(int8_t ctrl, int8_t timer, timer_struct * buffer) {
  return saveConfig(String("/control_" + String(ctrl) + ".bin"), buffer, sizeof(timer_struct), sizeof(header_struct) + (sizeof(timer_struct) * timer)); // save timer
}

bool saveHeader(int8_t ctrl, header_struct * buffer) {
  return saveConfig(String("/control_" + String(ctrl) + ".bin"), buffer, sizeof(header_struct), 0);
}
//=============================

// call cleanUpfirst
void setTZfromPOSIXstr(const char* tz_str) {
  settings.utcTime = (int64_t)time(nullptr);
  strlcpy(settings.tzStr, tz_str, sizeof(settings.tzStr));
debugf("setTZfromPOSIXstr:%s\n", settings.tzStr);
  configTzTime(tz_str, "pool.ntp.org"); // Re-initialize with the new TZ string
  // save updated TZ string
  saveConfig("/settings.bin", &settings, sizeof(settings_struct), 0);
}

void startRebootTimer() {
  rebootIfOffDelay.start(REBOOT_IF_OFF_DELAY_MS);
}

// OPTIONAL: change SNTP update delay
// a weak function is already defined and returns 1 hour
uint32_t sntp_update_delay_MS_rfc_not_less_than_15000() {
  if (haveSecondSNTPresponse) {
    return (60UL * 60 * 60) * 1000; // 60mins
  } else {
    return 15000; // 15 sec
  }
}

// used when config file does not exist or is invalid
void initSettings() {
  strlcpy(settings.name, CONTROLLER_NAME, sizeof(settings.name));
  settings.utcTime = (int64_t)time(nullptr);
  strlcpy(settings.tzStr, get_ntpSupport_DefaultTZ(), sizeof(settings.tzStr));
debugf("BtzStr:%s\n", settings.tzStr);
  // clean up
  cleanUpPosixTZStr(settings.tzStr, sizeof(settings.tzStr));
debugf("AtzStr:%s\n", settings.tzStr);
}
void initHeader(header_struct * hdr, int8_t ctrl) {
  snprintf(hdr->name, MAX_NAME_LEN, "Control %i", ctrl);
  hdr->pinNum = PIN_NOT_AVAIL;
  hdr->modes = (uint8_t)CtrlMode::OFF;
}
void initTimer(timer_struct * tmr) {
  tmr->duration = 0;
  tmr->onTime = 0;
  tmr->onMonth = 0;
  tmr->onDate = 0;
  tmr->onWeekDay = 0;
  tmr->temp = 20;
  tmr->lessHyst = LESSTHAN;
  tmr->statIdx = NO_STAT;
}
void initSensor(int s) {
  sensors[s].pinNum = PIN_NOT_AVAIL;
  sensors[s].mplex = 0;
  sensors[s].beta = 0;
  sensors[s].resistance = 0;
  sensors[s].puRes = 0;
  snprintf((char *)sensors[s].name, sizeof(sensors[s].name), "Sensor %i", s);
}
void initSensors() {
  for (int s = 0; s < MAX_SENSORS; s++) {
    initSensor(s);
  }
}

void resetDefaultTZstr() {
  char tzStr[MAX_TZ_LEN];
  tzStr[0] = '\0';
  strlcpy(tzStr, get_ntpSupport_DefaultTZ(), sizeof(settings.tzStr));
  setTZfromPOSIXstr(tzStr); // cleans up and set save flag as well
}

bool deleteControl(int ctrl) {
  if (ctrl < 0 || ctrl >= MAX_CONTROLS) return false;
  String filename = "/control_" + String(ctrl) + ".bin";

  if (SPIFFS.exists(filename))
    return SPIFFS.remove(filename);
  return false;
}

/* optional parameter can be used with ESP8266 Core 3.0.0*/
static void time_is_set(bool from_sntp) {
  debugf("Time from %s. UTC: %s, Local: %s\n", from_sntp ? "SNTP" : "USER", getUTCTime().c_str(), getCurrentTime_hhmm().c_str());

  if (from_sntp) {
    if (haveSNTPresponse) {
      haveSecondSNTPresponse = true;
    }
    haveSNTPresponse = true;
    haveSNTPupdate = true;
    ntpUpdateCheck.restart(); // do not timeout
  }
}

/**
 * return true if missed sntp update and timer timed out.
 */
bool missedSNTPupdate() {
  if (ntpUpdateCheck.justFinished()) {
    haveSNTPupdate = false;
  }
  return !haveSNTPupdate;
}


#ifdef ESP32
void pollSntp() {
  if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
//    if (!haveSNTPresponse) time_is_set(true);
    time_is_set(true);
  }
}
#endif

// start sntp server and updates time and then stops server
void initializeSNTP() {
  debug("initializeSNTP"); debugln();

#ifndef ESP32 // ESP32 does not have settimeofday_cb, we poll for time sync status in main loop
  // ** optional boolean in callback function is true when triggered by SNTP **
  // install callback - called when settimeofday is called (by SNTP or user)
  // once enabled (by DHCP), SNTP is updated every hour by default
  settimeofday_cb(time_is_set);
#endif
  static timeval tv;
  tv.tv_sec = (time_t)settings.utcTime;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
  ntpUpdateCheck.start(NTP_NOT_UPDATED_MS); // start monitor for SNTP updates
  // handle POSIX tz start
  cleanUpPosixTZStr(settings.tzStr, MAX_TZ_LEN);
  configTzTime(settings.tzStr, "pool.ntp.org"); // << this starts sntp 0.pool.ntp.org does not not work??
  yield();
}

// only handles +v numbers
static void print2digits(String & result, uint num) {
  if (num < 10) {
    result += '0';
  }
  result += num;
}

String getHHMMss(struct tm * tmPtr) {
  String result; // hh:mm:ss
  print2digits(result, tmPtr->tm_hour);
  result += ':';
  print2digits(result, tmPtr->tm_min);
  result += ':';
  print2digits(result, tmPtr->tm_sec);
  return result;
}

String getHHMM(struct tm * tmPtr) {
  String result; // hh:mm:ss
  print2digits(result, tmPtr->tm_hour);
  result += ':';
  print2digits(result, tmPtr->tm_min);
  return result;
}

// local time HH:MM:ss in sec
unsigned int getLocalTime_mins() {
  time_t now = time(nullptr);
  struct tm* tmPtr = localtime(&now);
  unsigned int rtn = (tmPtr->tm_hour);
  rtn = rtn * 60 + tmPtr->tm_min;
  return rtn;
}

int haveSNTP() {
  if (haveSNTPresponse) {
    return 1;
  }
  // else
  return 0;
}

// local time HH:MM:ss in sec
uint32_t getLocalTime_s() {
  time_t now = time(nullptr);
  struct tm* tmPtr = localtime(&now);
  uint32_t rtn = (tmPtr->tm_hour);
  rtn = rtn * 60 + tmPtr->tm_min;
  rtn = rtn * 60 + tmPtr->tm_sec;
  return rtn;
}

// small String <=10char) in ESP8266/ESP32 use built in char[]
String getCurrentTime_hhmm() {
  //  gettimeofday(&tv, nullptr);
  //  clock_gettime(0, &tp);
  time_t now = time(nullptr);
  struct tm* tmPtr = localtime(&now);
  String rtn = getHHMM(tmPtr);
  return rtn;
}

String getUTCTime() {
  //  gettimeofday(&tv, nullptr);
  //  clock_gettime(0, &tp);
  time_t now = time(nullptr);
  struct tm* tmPtr = gmtime(&now);
  return getHHMMss(tmPtr);
}

//============================================
// output handling

// pin = io pin#, active = true if active hi, false if active low
void setPinOff(uint8_t p) {
  if (IS_PIN_AVAIL(p)) {
    if (IS_PIN_EXPANDER(p)){
      DIGexpander[GET_PIN_EXP_ID(p)]->digitalWrite(GET_PIN_EXP_CH(p), !GET_PIN_ON_LVL(p)); // OFF
    } else {
      digitalWrite(GET_PIN_NUM(p), !GET_PIN_ON_LVL(p)); // ON
    }
  }
}
void setPinOn(uint8_t p) { // Takes pin num
  if (IS_PIN_AVAIL(p)) {
    if (IS_PIN_EXPANDER(p)){
      DIGexpander[GET_PIN_EXP_ID(p)]->digitalWrite(GET_PIN_EXP_CH(p), GET_PIN_ON_LVL(p)); // ON
    } else {
      digitalWrite(GET_PIN_NUM(p), GET_PIN_ON_LVL(p)); // OFF
    }
  }
}

// setup control output pin
void initCtrlIO(uint8_t p) {
debugf("init ctrlp:%i\n", p);
  if (IS_PIN_AVAIL(p)) {
    if (IS_PIN_EXPANDER(p)) {
      // add a driver object to the DIGexpander array - if not already there
      uint8_t id = GET_PIN_EXP_ID(p);
      if (DIGexpander[id] == nullptr) { // if not enabled yet
        DIGexpander[id] = new MCP23017(id);
        DIGexpander[id]->begin(); // only init each expander once!
      }
      DIGexpander[id]->pinMode(GET_PIN_EXP_CH(p), OUTPUT);
    } else {
      pinMode(GET_PIN_NUM(p), OUTPUT); // Configure the pin as an output
    }
    setPinOff(p); // Set the pin to OFF state AFTER expander setup!
  }
}

void initSnsrIO(uint8_t s) {
debugf("init snsr:%i\n", sensors[s].pinNum);
  if (IS_STAT_PIN_AVAIL(s)) {
    if (IS_STAT_DIGITAL(s)) {
      if (IS_STAT_EXPANDER(s)) { // got an I2C input?
        // add a driver object to the DIGexpander array - ony if not already there
        uint8_t id = GET_STAT_EXP_ID(s);
        if (DIGexpander[id] == nullptr) { // if not enabled yet
          DIGexpander[id] = new MCP23017(id);
          DIGexpander[id]->begin(); // only init each expander once!
        }
        DIGexpander[id]->pinMode(GET_STAT_EXP_CH(s), INPUT);
      } else {
        pinMode(GET_STAT_PIN(s), INPUT);
      }
    
    } else { // ADC
      if (IS_STAT_EXPANDER(s)) { // got an I2C input?
        // add a driver object to the ADCexpander array - only if not already there
        uint8_t id = GET_STAT_EXP_ID(s);
        if (ADCexpander[id] == nullptr) { // if not enabled yet - ADC I2C addresses are 0x48-0x4B
          ADCexpander[id] = new Adafruit_ADS1115;
          if (!ADCexpander[id]->begin(id + 0x48)) // only init each ADC once!
            ADCexpander[id] = nullptr; // can't find device
//debugf("AnlgExp: 0x%02X init:%i\n", id + 0x48, ADCexpander[id]->begin(id + 0x48));
        }
      } else { // analog input pin
        if ( IS_STAT_MPLEX(s)) { // got mplex output?
          pinMode(GET_STAT_MPLEX_PIN(s), INPUT); // disable any multiplex outputs
        }
        pinMode(GET_STAT_PIN(s), INPUT);
      }
    }
  }
}
// init all sensor inputs
void initAllSnsrIO() {
  // init any sensor input pins
  for (int s = 0; s < snsrCount; s++)
    initSnsrIO(s);
}

void printTimer(uint8_t t, timer_struct * timer) {
  debugf("- TMR %i, dur:0x%08X, Date:0x%04X, temp:0x%02X, on:0x%04X, Mth:0x%02X, Wdy:0x%02X, hyst:0x%02X, stat:0x%02X\n", 
    t,
    timer->duration, 
    timer->onDate, 
    timer->temp, 
    timer->onTime, 
    timer->onMonth,
    timer->onWeekDay, 
    timer->lessHyst,
    timer->statIdx
  );
}
void printControlHdr(int8_t c, header_struct * buffer) {
  debugf("ctrl%i %s pin:0x%02X mode:0x%02X\n", c, buffer->name, buffer->pinNum, buffer->modes);
}
void printSettings() {
  debugf("/Settings.bin = Name: %s UTC:%llu TZ: %s\n", settings.name, settings.utcTime, settings.tzStr);
}
void printStat(int8_t(s)) {
  debugf("stat%i %s pin:0x%02X\n", s, sensors[s].name, sensors[s].pinNum);
}


//--------------------------------------
// check if the timer 't' in chkTimer is ON - depends on stat value and lessthan settings
// saves state in tmrSnsrState bit array for use by hysteresis (uses control & timer# to find bit#).
#define IS_CHKTMR_LESSTHAN() ((chkTimer.lessHyst & LESSTHAN_MASK) == LESSTHAN)
#define IS_CHKTMR_MORETHAN() ((chkTimer.lessHyst & LESSTHAN_MASK) == MORETHAN)
#define IS_CHKTMR_WITHIN() ((chkTimer.lessHyst & LESSTHAN_MASK) == WITHIN)
#define IS_CHKTMR_OUTWITH() ((chkTimer.lessHyst & LESSTHAN_MASK) == OUTWITH)
#define GET_CHKTMR_HYST() (chkTimer.lessHyst & ~LESSTHAN_MASK)
#define GET_CHKTMR_LESSTHAN() ((chkTimer.lessHyst & LESSTHAN_MASK) >> 14) // 0=3
#define TMRSTATTEST(c, t) (tmrSnsrState[c][t >> 3] & (1 << (t & 0x7)))   // test on/off bit for timer
#define TMRSTATSET(c, t) (tmrSnsrState[c][t >> 3] |= (1 << (t & 0x7)))    // set bit for timer stat state
#define TMRSTATCLR(c, t) (tmrSnsrState[c][t >> 3] &= (~(1 << (t & 0x7)))) // clr bit for timer

// load one timer data from open file
bool loadTimer(File &f, int8_t timer, timer_struct * buffer) {
  if (f.seek(sizeof(header_struct) + (sizeof(timer_struct) * timer), SeekSet)) {
    return f.read((uint8_t*)buffer, sizeof(timer_struct)) == sizeof(timer_struct);
  }
  return false;
}

TmrState getTmrState(int c, int t) {
  const int8_t statIndex = chkTimer.statIdx;
//if (c == 1 && t == 1) {
// printStat(statIndex);
// printTimer(t, &chkControl.timers[t]); }

  if (statIndex == NO_STAT || sensors[statIndex].pinNum == PIN_NOT_AVAIL) // no stat selected or invalid pin so timer is always on 
    return TmrState::STAT_ON;

  TmrState newState = TMRSTATTEST(c, t) ? TmrState::STAT_ON : TmrState::STAT_OFF; // last stat state - assume no state change

  if (IS_STAT_DIGITAL(statIndex)) { // Handle digital stats
    // STAT_ON if (isLessThan AND isLow) OR (isMoreThan AND isHigh)
    // This is equivalent to a XNOR operation.
    newState = (IS_CHKTMR_LESSTHAN() == (TMRSTATTEST(c, t) == LOW)) ? TmrState::STAT_ON : TmrState::STAT_OFF;
  
  } else { // Handle analog stats with hysteresis
    const int16_t currentVal = currSnsrVal[statIndex]; // saved by readTemps()
    const int16_t setpoint   = chkTimer.temp;
    const int16_t hysteresis = GET_CHKTMR_HYST();
    const int16_t lowerBound = setpoint - hysteresis;
    const int16_t upperBound = setpoint + hysteresis;
//if (c == 1 && t == 1)
// debugf("newS: %i cval:%i setP: %i hyst:%i lB:%i uB:%i ltht:0x%04X lt:%i\n",
// newState == TmrState::STAT_ON, currentVal, setpoint, hysteresis, lowerBound, upperBound, chkTimer.lessHyst, IS_CHKTMR_LESSTHAN());    
    if (IS_CHKTMR_LESSTHAN()) { // Heating mode logic
      if (currentVal >= upperBound) {
        newState = TmrState::STAT_OFF;
      } else if (currentVal < lowerBound) {
        newState = TmrState::STAT_ON;
      } // else inside dead zone so no state change

    } else if (IS_CHKTMR_MORETHAN()) { // Cooling mode logic (more than)
      if (currentVal > upperBound) {
        newState = TmrState::STAT_ON;
      } else if (currentVal <= lowerBound) {
        newState = TmrState::STAT_OFF;
      } // else Inside dead zone so no state change

    } else if (IS_CHKTMR_WITHIN()) { // inclusive internal range check
      // No hysteresis band here, just check if within the range
      newState = (currentVal >= lowerBound && currentVal <= upperBound) ? TmrState::STAT_ON : TmrState::STAT_OFF;

    } else if (IS_CHKTMR_OUTWITH()) { // exclusive external range check
      // No hysteresis band here, just check if outside the range
      newState = (currentVal < lowerBound || currentVal > upperBound) ? TmrState::STAT_ON : TmrState::STAT_OFF;
    }
  }
  newState == TmrState::STAT_ON ? TMRSTATSET(c,t) : TMRSTATCLR(c,t);
  return newState;
}

// returns true if the current time (in chkTimer) is within the on/off timer times
// AND any dayweek is current AND any date is now AND any month is this month.
// Note, will turn off if any dayweek/date/month is set and on+duration crosses it.
bool inTimerPeriod() {
  if (chkTimer.duration == 0) return false;

  time_t now = time(nullptr);
  struct tm* tm_local = localtime(&now);

  // Get the start time of the timer as a time_t for today
  struct tm tm_on = *tm_local;
  tm_on.tm_hour = chkTimer.onTime / 60;
  tm_on.tm_min = chkTimer.onTime % 60;
  tm_on.tm_sec = 0;
  time_t on_time_today = mktime(&tm_on);

  // Check if the timer's start day is valid according to the schedule
  bool wday_ok = (chkTimer.onWeekDay == 0) || (chkTimer.onWeekDay & (1 << (tm_on.tm_wday + 1)));
  bool date_ok = (chkTimer.onDate == 0) || (chkTimer.onDate & (1 << tm_on.tm_mday));
  bool month_ok = (chkTimer.onMonth == 0) || (chkTimer.onMonth & (1 << (tm_on.tm_mon + 1)));

  // Calculate the off time based on the duration
  time_t off_time = on_time_today + (chkTimer.duration * 60);

  // valid day and within time period
  return (wday_ok || date_ok || month_ok) && (now >= on_time_today && now < off_time);
}


/* Returns true if:
     control is set ON
            OR 
     (     control is not OFF
       AND onTime != offTime
       AND current time hh_mm is between onTime and onTime+duration 
       AND, if stat available for timer, stat is in active state
       AND haveSNTPresponse
     )
  Reads the timer file for the control to access the timer settings
  sets control.isOn. 
  NOTE- all ops are tested on chkControl buffer
*/
#define GET_CHKMODE (chkHeader.modes & 0x0F)
#define IS_CHKMODE_ON (GET_CHKMODE == (uint8_t)CtrlMode::ON)
#define IS_CHKMODE_AUTO (GET_CHKMODE == (uint8_t)CtrlMode::AUTO)
#define IS_CHKMODE_BOOST (GET_CHKMODE == (uint8_t)CtrlMode::BOOST)
#define IS_CHKMODE_ADV (GET_CHKMODE == (uint8_t)CtrlMode::ADV)
#define IS_CHKMODE_BOOSTADV (IS_CHKMODE_BOOST || IS_CHKMODE_ADV)

bool isCtrlOn(File &f, int c, uint8_t num_timers) {
  bool rtn = false; // default to off
  if (haveSNTPresponse && IS_CHKMODE_ON) {
#ifdef USE_ANY_TIMER_STAT
    bool noStat = true;
    int timer = 1; // start at timer 1!
    bool rtn = false;
    int8_t timer = 1; // start at timer 1
    while (timer < num_timers) {
      if (loadTimer(f, timer, &chkTimer) && chkTimer.statIdx != NO_STAT) {
        noStat = false; // has a stat
        if (getTmrState(c, timer)) {
          rtn = true; // timer stat is active
          break;
        }
      }
      timer++;
    }
    if (noStat)
      rtn = noStat; // if no stats then always ON, if stats but none active then OFF
#elif defined(USE_TIMER1_STAT)
    if (loadTimer(f, 1, &chkTimer)) {
      rtn = getTmrState(c, 1) == TmrState::STAT_ON;
    }
#else
    rtn = true; // no stat so always on
#endif

  } else if (IS_CHKMODE_BOOSTADV) {
//debugf("ctr;: %i boostadv - inPer0: %i tmr1State: %i\n", c, inTimerPeriod(c, 0), getTmrState(c, 1));
    if (loadTimer(f, 0, &chkTimer) && inTimerPeriod()) { // timer0 in period?
#ifdef USE_TIMER1_STAT
      if (loadTimer(f, 1, &chkTimer)) {
        rtn = getTmrState(c, 1) == TmrState::STAT_ON;
      }
#else
      rtn = true;
#endif
    } else {
      // boost/adv period completed so cancel boost/adv & reset control to previous mode
      chkHeader.modes >>= 4; // restore previous setting, clears boost/adv
      SET_CTRLON_MODE(c, chkHeader.modes); // save current mode
      f.seek(0, SeekSet);
      f.write((uint8_t*)&chkHeader, sizeof(header_struct)); // update header for control
    }

  } else if (IS_CHKMODE_AUTO) { // auto mode so check if any timer is in period AND stat meets active conditions
    int timer = 1; // start at timer 1 & stop if any timer period is active!
    while (timer < num_timers) {
      if (loadTimer(f, timer, &chkTimer) && inTimerPeriod() && getTmrState(c, timer) == TmrState::STAT_ON) {  
        rtn = true;
        break;
      }
      timer++;
    }
  }
  if (rtn) ctrlOnMode[c] |= 0x8000; // save curr ctrl state in b15
  else ctrlOnMode[c] &= ~(0x8000);
  return rtn;
}

void setOutputs() {
  static millisDelay outputChangeTimer;

uint32_t setOutTmr = millis();

  readTemps();

  // start timer if not running
  if (!outputChangeTimer.isRunning())
    outputChangeTimer.start(OUTPUT_CHECK_PERIOD);

  if (outputChangeTimer.justFinished()) {
    outputChangeTimer.repeat();

    bool aCtrlIsOn = false;
    for (int c = 0; c < ctrlCount; c++) {
      yield(); // Allow other tasks to run

uint32_t setIO = millis();
      uint8_t num_timers = 0;
      String name = "/control_" + String(c) + ".bin";

#ifdef ESP32
      if (xSemaphoreTake(FSmutex, (2000 / portTICK_PERIOD_MS)) != pdTRUE) continue;
#else
      uint32_t mTimer = millis(); while (!GetMutex(FSmutex)) { if ((millis() - mTimer) > 200) break;  delay(1); }
      if ((millis() - mTimer) > 200) continue;
#endif

      File f = SPIFFS.open(name.c_str(), "r+");
      if (f) {
        if (f.read((uint8_t*)&chkHeader, sizeof(header_struct)) == sizeof(header_struct)) {
          num_timers = (f.size() - sizeof(header_struct)) / sizeof(timer_struct);
          if (num_timers) {
            if (isCtrlOn(f, c, num_timers)) {
              aCtrlIsOn = true;
              setPinOn(chkHeader.pinNum); // ON
            } else {
              setPinOff(chkHeader.pinNum); // OFF
            }
          }
        }
        f.close();
      }
#ifdef ESP32
      xSemaphoreGive(FSmutex);
#else
      ReleaseMutex(FSmutex);
#endif
//debugf("chkCtrl%i num_tmr: %i %lumS\n", c, num_timers, millis() - setIO); // time to handle request
    }

//debugf("+++ rd temps + set all outputs: %lumS\n", millis() - setOutTmr); // totoal time for all 

    if (rebootIfOffDelay.justFinished()) {
      // reboot every 24hrs to clear memory leaks (if any)
      debugln("24hr reboot triggered");
      rebootWhenOff = true;
    }
    if (rebootWhenOff && !aCtrlIsOn) { // no active ctrls so safe to reboot
      settings.utcTime = (int64_t)time(nullptr); // update current time
      saveConfig("/settings.bin", &settings, sizeof(settings_struct), 0);
      ESP.restart();
    }
  }
}

//==================================================

// read one sensor at a time every tempTimer delay
void readTemps() {
  static millisDelay tempTimeTimer;
  static uint8_t s = 0; // sensor idx to read
  
  if (snsrCount) { // got any sensors to read?

    if (!tempTimeTimer.isRunning())
      tempTimeTimer.start(TEMP_READ_PERIOD);

    if (tempTimeTimer.justFinished()) {
      tempTimeTimer.repeat();
      s++;
      if (s >= snsrCount)
        s = 0; // reset to stat#0 again
      
      // read the stat
      if (IS_STAT_PIN_AVAIL(s)) {
//debugf("read stat %02X pin:0x%02X = ", s, sensors[s].pinNum);
        int16_t temp = 0;
        if (IS_STAT_DIGITAL(s)) {
          if (IS_STAT_EXPANDER(s)) { // MCP23017 expander enabled?
            if (DIGexpander[GET_STAT_EXP_ID(s)]) temp = DIGexpander[GET_STAT_EXP_ID(s)]->digitalRead(GET_STAT_EXP_CH(s));
          } else {
            temp = digitalRead(sensors[s].pinNum);
          }

        } else { // analog
          if (IS_STAT_EXPANDER(s)) {
            if (ADCexpander[GET_STAT_EXP_ID(s)]) // got this adc1115 expander?
              temp = ADCexpander[GET_STAT_EXP_ID(s)]->readADC_SingleEnded(GET_STAT_EXP_CH(s));

          } else if (IS_STAT_MPLEX(s)) {
            digitalWrite(GET_STAT_MPLEX_PIN(s), LOW);
            pinMode(GET_STAT_MPLEX_PIN(s), OUTPUT); // enable sensor
            delay(1);   // short delay;
            temp = analogRead(GET_STAT_PIN(s));
            pinMode(GET_STAT_MPLEX_PIN(s), INPUT); // disable sensor

          } else { // GPIO pin
            temp = analogRead(GET_STAT_PIN(s));
          }
//debugf("anlg val:%i ", temp);

          if (IS_STAT_ANALOG(s) && IS_STAT_STEINHART(s) && sensors[s].resistance > 0 && sensors[s].puRes > 0) { // apply eqn if enabled and valid params
            float vPu = 3.3;  // Thermistor pull-up Voltage
            float maxAdc;;
            float vMeas;
            if (IS_STAT_EXPANDER(s) && ADCexpander[GET_STAT_EXP_ID(s)]) { 
              maxAdc = 16383.0;
              vMeas = ADCexpander[GET_STAT_EXP_ID(s)]->computeVolts(temp);
            } else {
              float vRef;
#ifdef ESP32
              maxAdc = 4095.0;
              vRef = 1.1; // ADC Reference Voltage
#else
              maxAdc = 1023.0; // ESP8266 ADC read value range
              vRef = 3.2; // Wemos D1 mini is 3.2v, ESP8266 module is 1V
#endif
              if (temp >= maxAdc) temp = maxAdc - 1; // avoid divide by zero
              vMeas = ((float)temp / maxAdc) * vRef; // convert to voltage value
            }
            if (temp == 0) temp = 1; // avoid log(0)
            float resistance = sensors[s].puRes * vMeas / (vPu - vMeas);
//debugf(" Vmeas: %.3f res: %.2f ", vMeas, resistance);

            // Calculate temperature from resistance
            temp = (1 / ((1 / (25 + 273.15)) + ((log(resistance / sensors[s].resistance)) / sensors[s].beta))) - 273.15;
          }
        }
//debugf(" fnl val = %i\n", temp);
        currSnsrVal[s] = temp; // save temp or state
      }
    }
  }
}
