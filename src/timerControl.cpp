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

// define a weak getDefaultTZ method that can be defined elsewhere if you want to set a default TZ
const char* get_ntpSupport_DefaultTZ() __attribute__((weak));

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

// Set up auto reboot to clean out any memory leaks
// if in AUTO reboot when turns off
// otherwise after ~24hrs reboot when power relay is OFF
millisDelay rebootIfOffDelay;
unsigned long REBOOT_IF_OFF_DELAY_MS = 24ul * 60 * 60 * 1000 + 10ul * 60 * 1000; // 24hrs + 10mins
bool timerWasOn = false;
bool timerTurnedOnInAuto = false;
bool rebootWhenOff = false;
extern "C" int clock_gettime(clockid_t unused, struct timespec *tp);

static bool haveSNTPresponse = false; // set true on first response
static bool haveSecondSNTPresponse = false; // got second one
static bool haveSNTPupdate = false;

static bool needToSaveConfigFlag = false;

bool ctrlIsOn(uint8_t ctrl);

void setUTCconfigTime();

unsigned int getLocalTime_mins(); // hh:mm in mins

Adafruit_ADS1015 * ADCexpander[4] = {nullptr}; // array of ADC1115 ADC drivers
MCP23017 * DIGexpander[4] = {nullptr}; // array of digital expanders

// call cleanUpfirst
void setTZfromPOSIXstr(const char* tz_str) {
  time_t now = time(nullptr);
  timerConfig.utcTime = now;
  strlcpy(timerConfig.tzStr, tz_str, sizeof(timerConfig.tzStr));
  debug("setTZfromPOSIXstr:"); debugln(timerConfig.tzStr);
  configTzTime(tz_str, "pool.ntp.org"); // Re-initialize with the new TZ string
  needToSaveConfigFlag = true;
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

// used when timerConfigFileName file does not exist or is invalid
void setInitialtimerConfig() {
  for (int c = 0; c < ctrl_count; c++) {
    memset(timerConfig.controls[c].name, 0, MAX_NAME_LEN + 1); // Initialize name
    timerConfig.controls[c].pinNum = pins[c]; // Initialize pin from available list
    timerConfig.controls[c].state = OFF; // Initialize state
    timerConfig.controls[c].prevState = OFF; // Initialize previous state
    for (int t = 0; t < MAX_TIMERS; t++) {
      timerConfig.controls[c].timers[t].offTime = 0;
      timerConfig.controls[c].timers[t].lessHyst = LESSTHAN;
      timerConfig.controls[c].timers[t].temp = 20; // deg C default trigger temp
      timerConfig.controls[c].timers[t].statIdx = -1;
    }
  }

  timerConfig.utcTime = 0;
  if (get_ntpSupport_DefaultTZ) {
    strlcpy(timerConfig.tzStr, get_ntpSupport_DefaultTZ(), sizeof(timerConfig.tzStr));
  }
  // clean up
  cleanUpPosixTZStr(timerConfig.tzStr, sizeof(timerConfig.tzStr));
}

void resetDefaultTZstr() {
  char tzStr[sizeof(timerConfig.tzStr)];
  tzStr[0] = '\0';
  if (get_ntpSupport_DefaultTZ) {
    strlcpy(tzStr, get_ntpSupport_DefaultTZ(), sizeof(tzStr));
  }
  setTZfromPOSIXstr(tzStr); // cleans up and set save flag as well
}

String getTZstr() {
  return String(timerConfig.tzStr);
}

void printtimerConfig() {
  for (int c = 0; c < ctrl_count; c++) {
    debugf("\nCtrl%i state: %c:%c\n",
      c, 
      timerConfig.controls[c].state, 
      timerConfig.controls[c].prevState);
    for (int t = 0; t < MAX_TIMERS; t++) {
      debugf("  Timer%i : %i->%i, stat:%i when: %c %i +/-%i\n", 
      t,
      timerConfig.controls[c].timers[t].onTime,
      timerConfig.controls[c].timers[t].offTime,
      timerConfig.controls[c].timers[t].statIdx,
      IS_TMR_LESSTHAN(c,t) ? '<' : '>',
      timerConfig.controls[c].timers[t].temp,
      GET_TMR_HYST(c, t));
    }
  }
}

// load the last time saved before shutdown/reboot
// returns pointer to timerConfig
timerConfig_struct* loadTimerConfig() {
  setInitialtimerConfig();
  if (!initializeFS()) {
    debugln("FS failed to initialize");
    return &timerConfig; // returns default if cannot open FS
  }
  if (!LittleFS.exists(timerConfigFileName)) {
    debug(timerConfigFileName); debugln(" missing.");
    saveTimerConfig(timerConfig);
    return &timerConfig; // returns default if missing
  }
  // else load config
  File f = LittleFS.open(timerConfigFileName, "r");
  if (!f) {
    debug(timerConfigFileName); debug(" did not open for read.");
    LittleFS.remove(timerConfigFileName);
    saveTimerConfig(timerConfig);
    return &timerConfig; // saves & returns default
  }
  if (f.size() != sizeof(timerConfig)) {
    debug(timerConfigFileName); debug(" wrong size.");
    f.close();
    saveTimerConfig(timerConfig);
    return &timerConfig; // returns default wrong size
  }
  int bytesIn = f.read((uint8_t*)(&timerConfig), sizeof(timerConfig));
  if (bytesIn != sizeof(timerConfig)) {
    debug(timerConfigFileName); debug(" wrong size read in.");
    setInitialtimerConfig(); // again
    f.close();
    saveTimerConfig(timerConfig);
    return &timerConfig;
  }
  f.close();
  // else return settings
  // clean up tz and return
  cleanUpPosixTZStr(timerConfig.tzStr, sizeof(timerConfig.tzStr));
  
  debugln("Loaded config");
  printtimerConfig();
  String desc = timerConfig.tzStr;
  struct posix_tz_data_struct posixTz;
  posixTZDataFromStr(desc,posixTz);
  buildPOSIXdescription(posixTz, desc);
  debugln("TZ description");
  debugln(desc);

  return &timerConfig;
}

// load the last time saved before shutdown/reboot
bool saveTimerConfig(timerConfig_struct& timerConfig) {
  if (!initializeFS()) {
    debugln("FS failed to initialize");
    return false;
  }
  // else save config
  File f = LittleFS.open(timerConfigFileName, "w"); // create/overwrite
  if (!f) {
    debug(timerConfigFileName); debug(" did not open for write.");
    return false; // returns default wrong size
  }
  setUTCconfigTime(); // update utc time
  int bytesOut = f.write((uint8_t*)(&timerConfig), sizeof(timerConfig_struct));
  if (bytesOut != sizeof(timerConfig_struct)) {
    debug(timerConfigFileName); debug(" write failed.");
    return false;
  }
  // else return settings
  f.close(); // no rturn
  debug(timerConfigFileName); debugln(" config saved.");
  printtimerConfig();
  return true;
}

/* optional parameter can be used with ESP8266 Core 3.0.0*/
static void time_is_set(bool from_sntp) {
  debug("time_is_set from "); debugln(from_sntp ? "SNTP" : "USER");
  debug("UTC   "); debugln(getUTCTime());
  debug("Local "); debugln(getCurrentTime_hhmm());

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
 * return false if missed sntp update and timer timed out.
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
    if (!haveSNTPresponse) time_is_set(true);
  }
}
#endif

// start sntp server and updates time and then stops server
void initializeSNTP() {
  debug("initializeSNTP"); debugln();
  loadTimerConfig(); // load timerConfig global and cleans up tzStr

  // ESP32 does not have settimeofday_cb, we poll for time sync status in main loop
#ifndef ESP32
  // ** optional boolean in callback function is true when triggered by SNTP **
  // install callback - called when settimeofday is called (by SNTP or user)
  // once enabled (by DHCP), SNTP is updated every hour by default
  settimeofday_cb(time_is_set);
#endif
  static timeval tv;
  tv.tv_sec = timerConfig.utcTime;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
  ntpUpdateCheck.start(NTP_NOT_UPDATED_MS); // start monitor for SNTP updates
  // handle POSIX tz start
  cleanUpPosixTZStr(timerConfig.tzStr, sizeof(timerConfig.tzStr));
  configTzTime(timerConfig.tzStr, "pool.ntp.org"); // << this starts sntp 0.pool.ntp.org does not not work??
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
  return getHHMM(tmPtr);
}

String getUTCTime() {
  //  gettimeofday(&tv, nullptr);
  //  clock_gettime(0, &tp);
  time_t now = time(nullptr);
  struct tm* tmPtr = gmtime(&now);
  return getHHMMss(tmPtr);
}

#define PTM(w) \
  debug(" " #w "="); \
  debug(tm->tm_##w);

void printTm(const char* what, const tm * tm) {
  debug(what);
  PTM(isdst); PTM(yday); PTM(wday);
  PTM(year);  PTM(mon);  PTM(mday);
  PTM(hour);  PTM(min);  PTM(sec);
}

void showTimeDebug() {
#ifndef DEBUGPORT
    return;
#else
  debugln("ShowTimeDebug");
  timeval tv;
  timespec tp;
  time_t now;
  uint32_t now_ms, now_us;

  gettimeofday(&tv, nullptr);
  clock_gettime(0, &tp);
  now = time(nullptr);
  now_ms = millis();
  now_us = micros();

  debugln();
  printTm("localtime:", localtime(&now));
  debugln();
  printTm("gmtime:   ", gmtime(&now));
  debugln();
  // time from boot
  debugf("clock:     %isec + %ins/n",(uint32_t)tp.tv_sec, (uint32_t)tp.tv_nsec);
  // time from boot
  debugf("millis:    %imSec uSec:    %i\n", now_ms,now_us);
  // EPOCH+tz+dst
  debugf("gtod:       %i sec + %i us\n", (uint32_t)tv.tv_sec, (uint32_t)tv.tv_usec);
  // EPOCH+tz+dst
  debugf("time:      %i\n", (uint32_t)now);
  // timezone and demo in the future
  debugf("timezone:  %s\n", getenv("TZ") ? : "(none)");
  // human readable
  debugf("ctime:     %s\n", ctime(&now));
  
  // show subsecond synchronisation
  timeval prevtv;
  time_t prevtime = time(nullptr);
  gettimeofday(&prevtv, nullptr);

  while (true) {
    gettimeofday(&tv, nullptr);
    if (tv.tv_sec != prevtv.tv_sec) {
      debugf("time(): %u   gettimeofday(): %u.%06u  seconds are unchanged\n",
                       (uint32_t)prevtime,
                       (uint32_t)prevtv.tv_sec, (uint32_t)prevtv.tv_usec);
      debugf("time(): %u   gettimeofday(): %u.%06u  <-- seconds have changed\n",
                       (uint32_t)(prevtime = time(nullptr)),
                       (uint32_t)tv.tv_sec, (uint32_t)tv.tv_usec);
      break;
    }
    prevtv = tv;
    delay(1);
  }

  debugln();
#endif
}

//============================================
// output handling

// pin = io pin#, active = true if active hi, false if active low
void setPinOff(int c) { // Takes an integer index
  if (timerConfig.controls[c].pinNum != PIN_NOT_AVAIL) {
    if (IS_CTRL_EXPANDER(c)){
      DIGexpander[GET_CTRL_EXP_ID(c)]->digitalWrite(GET_CTRL_PIN_NUM(c), !GET_CTRL_ON_LVL(c)); // OFF
    } else {
      digitalWrite(GET_CTRL_PIN_NUM(c), !GET_CTRL_ON_LVL(c)); // OFF
    }
  }
}
void setPinOn(int c) { // Takes an integer index
  if (timerConfig.controls[c].pinNum != PIN_NOT_AVAIL) {
    if (IS_CTRL_EXPANDER(c)){
      DIGexpander[GET_CTRL_EXP_ID(c)]->digitalWrite(GET_CTRL_PIN_NUM(c), GET_CTRL_ON_LVL(c)); // ON
    } else {
      digitalWrite(GET_CTRL_PIN_NUM(c), GET_CTRL_ON_LVL(c)); // ON
    }
  }
}

void initGPIO () {
  // set control output pins
  for (int c = 0; c < ctrl_count; c++) { // Loop through all controls
    if (timerConfig.controls[c].pinNum != PIN_NOT_AVAIL) {
      if (IS_CTRL_EXPANDER(c)) {
        // add a driver object to the DIGexpander array - only if not already there
        uint8_t id = GET_CTRL_EXP_ID(c);
        if (DIGexpander[id] == nullptr) { // if not enabled yet
          DIGexpander[id] = new MCP23017(id);
          DIGexpander[id]->begin(); // only init each expander once!
        }
      } else {
        pinMode(GET_CTRL_PIN_NUM(c), OUTPUT); // Configure the pin as an output
      }
      setPinOff(c); // Set the pin to OFF state AFTER expander setup1
    }
  }

  // init any sensor input pins
  for (int s = 0; s < stat_count; s++) {
    if (IS_STAT_DIGITAL(s)) {
      if (IS_STAT_EXPANDER(s)) { // got an I2C input?
        // add a driver object to the DIGexpander array - ony if not already there
        uint8_t id = GET_STAT_EXP_ID(s);
        if (DIGexpander[id] == nullptr) { // if not enabled yet
          DIGexpander[id] = new MCP23017(id);
          DIGexpander[id]->begin(); // only init each expander once!
        }
      } else {
        pinMode(statPins[s].pinNum, INPUT);
      }
    
    } else { // ADC
      if (IS_STAT_EXPANDER(s)) { // got an I2C input?
        // add a driver object to the ADCexpander array - ony if not already there
        uint8_t id = GET_STAT_EXP_ID(s);
        if (ADCexpander[id] == nullptr) { // if not enabled yet - ADC I2C addresses are 0x48-0x4B
          ADCexpander[id] = new Adafruit_ADS1015;
          ADCexpander[id]->begin(id + 0x48); // only init each ADC once!
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

//--------------------------------------
// set new control state if different to current state
void setOff(int c) {
  if (timerConfig.controls[c].state != OFF) {
    timerConfig.controls[c].state = OFF;
    timerConfig.controls[c].timers[0].offTime = 0;
    timerConfig.controls[c].timers[0].onTime = 0;// clr any boost/adv
    needToSaveConfigFlag = true;
  }
}
void setOn(int c) {
  if (timerConfig.controls[c].state != ON) {
    timerConfig.controls[c].state = ON;
    timerConfig.controls[c].timers[0].offTime = 0;
    timerConfig.controls[c].timers[0].onTime = 0; // clr any boost/adv
    needToSaveConfigFlag = true;
  }
}
void setAuto(int c) {
  if (timerConfig.controls[c].state != AUTO) {
    timerConfig.controls[c].state = AUTO;
    timerConfig.controls[c].timers[0].offTime = 0;
    timerConfig.controls[c].timers[0].onTime = 0; // clr any boost/adv
    needToSaveConfigFlag = true;
  }
}
void setBoost(int c) { // timer0 is boost, start timers[0] is now, offtime now + 60mins, uses temp/stat from timer1
  if (!isBoostAdv(c))
    timerConfig.controls[c].prevState = timerConfig.controls[c].state; //save previous setting
  timerConfig.controls[c].state = BOOST;
  timerConfig.controls[c].timers[0].onTime = getLocalTime_mins();
  timerConfig.controls[c].timers[0].offTime = (timerConfig.controls[c].timers[0].onTime + 60 + 1440U) % 1440U;
  needToSaveConfigFlag = true;
}
void setAdv(int c) { // start timer0 now, timer 0 offtime/temp copied from timer1
  if (timerConfig.controls[c].state != ADV) {
    if (timerConfig.controls[c].timers[1].onTime != timerConfig.controls[c].timers[1].offTime) { // only set if there is a valid timer1 time set!
      if (!isBoostAdv(c))
        timerConfig.controls[c].prevState = timerConfig.controls[c].state; // save previous setting
      timerConfig.controls[c].state = ADV;
      timerConfig.controls[c].timers[0].onTime = getLocalTime_mins();
      timerConfig.controls[c].timers[0].offTime = timerConfig.controls[c].timers[1].offTime;
      needToSaveConfigFlag = true;
    }
  }
}
//--------------------------------------

bool isOffSelected(int c) {
  return timerConfig.controls[c].state == OFF;
}
bool isOnSelected(int c) {
  return timerConfig.controls[c].state == ON;
}
bool isAutoSelected(int c) {
  return timerConfig.controls[c].state == AUTO;
}
bool isBoostAdv(int c) {
  return (timerConfig.controls[c].state == BOOST) || (timerConfig.controls[c].state == ADV);
}

//--------------------------------------
// set new timer values IF they are different to existing values
// includes basic validation of new value

// timer set
void setOnTime(int c, int t, int val){
  if (val != -1 && val != timerConfig.controls[c].timers[t].onTime) {
    timerConfig.controls[c].timers[t].onTime = val;
    needToSaveConfigFlag = true;
  }
}
void setOffTime(int c, int t, int val) {
  if (val != -1 && val != timerConfig.controls[c].timers[t].offTime) {
    timerConfig.controls[c].timers[t].offTime = val;
    needToSaveConfigFlag = true;
  }
}
void setTemp(int c, int t, int16_t val) {
  if (val > -127 && val < 127 && val != timerConfig.controls[c].timers[t].temp) {
    timerConfig.controls[c].timers[t].temp = val;
    needToSaveConfigFlag = true;
  }
};
void setLess(int c, int t, char val) {
  uint8_t temp = val == 'L' ? 0x80 : 0;
  if (temp ^ GET_TMR_LESSTHAN(c, t)) { // value changing?
    timerConfig.controls[c].timers[t].lessHyst = temp | GET_TMR_HYST(c, t);
    needToSaveConfigFlag = true;
  }
};
void setHyst(int c, int t, uint8_t val) {
  if (val != GET_TMR_HYST(c,t)) {
    timerConfig.controls[c].timers[t].lessHyst = val | GET_TMR_LESSTHAN(c, t);
    needToSaveConfigFlag = true;
  }
};
void setStat(int c, int t, int val) {
  if (val != NO_STAT && val < stat_count && val != timerConfig.controls[c].timers[t].statIdx) {
    timerConfig.controls[c].timers[t].statIdx = val;
    needToSaveConfigFlag = true;
  }
};
//--------------------------------------

// Uses a dead zone defined by the hysteresis value for the timer.
// Returns statState (ON or OFF) or currentStatState[stat] if in the dead zone
StatState getStatState(int c, int t) {
  int8_t statIndex = timerConfig.controls[c].timers[t].statIdx;
  StatState newState = STAT_OFF;

  // If no stat is assigned to this timer, it's always considered ON.
  if (statIndex == -1) 
    newState =  STAT_ON;
  else {

    bool isLessThan = IS_TMR_LESSTHAN(c, t);

    // Handle digital stats
    if (IS_STAT_DIGITAL(statIndex)) {
      // STAT_ON if (isLessThan AND isLow) OR (isMoreThan AND isHigh)
      // This is equivalent to a XNOR operation.
      newState = (isLessThan == (getCurrStatVal(c, t) == LOW)) ? STAT_ON : STAT_OFF;
    
    } else { 
      // Handle analog stats with hysteresis
      const int16_t currentVal = currStatVal[statIndex];
      const uint8_t hysteresis = GET_TMR_HYST(c, t);
      const int16_t setpoint = timerConfig.controls[c].timers[t].temp;
      const int16_t lowerBound = setpoint - hysteresis;
      const int16_t upperBound = setpoint + hysteresis;

      // If inside the dead zone, maintain the current state to prevent chattering.
      if (currentVal >= lowerBound && currentVal <= upperBound) { // MUST incl = to catch lower == upper case! 
        newState = currentStatState[statIndex];

      } else {
        // Determine new state based on whether we are above or below the dead zone and lessthan or morethan
        newState = ((currentVal < lowerBound) != isLessThan) ? STAT_OFF : STAT_ON; // assumes morethan mode
        currentStatState[statIndex] = newState; // save last state
      }
    }
  }
  return newState;
}

  // returns true if the current time is within the on/off timer times
bool inTimerPeriod(int c, int t) {

  if (timerConfig.controls[c].timers[t].offTime != timerConfig.controls[c].timers[t].onTime) {
    int current_mins = getLocalTime_mins();
    if (timerConfig.controls[c].timers[t].offTime > timerConfig.controls[c].timers[t].onTime) {  // onTime then offTime
      if ((current_mins >= timerConfig.controls[c].timers[t].onTime) && (current_mins < timerConfig.controls[c].timers[t].offTime))
        return true;
    } else {    // offTime then onTime
      if (!((current_mins >= timerConfig.controls[c].timers[t].offTime) && (current_mins < timerConfig.controls[c].timers[t].onTime)))
        return true;
    }
  }
  return false;
}


// Returns true if:
//     control is set ON
//            OR 
//     (     control is not OFF
//       AND onTime != offTime
//       AND current time hh_mm is between onTime and offTime 
//       AND, if stat available for timer, stat is in active state
//       AND haveSNTPresponse
//     )
bool isCtrlOn(int c) {
//debugf("ctrl:%i state:%c\n", c, timerConfig.controls[c].state);
  if ((timerConfig.controls[c].state == OFF) || (!haveSNTPresponse))
    return false;
    
  else if (timerConfig.controls[c].state == ON) {  // on - only if ANY timer stat is active OR no stats!
#ifdef USE_ANY_TIMER_STAT
    bool noStat = true;
    for (int t = 1; t < MAX_TIMERS; t++) {
      if (timerConfig.controls[c].timers[t].statIdx != -1) {
        noStat = false;
        if (getStatState(c, t)) return true;
      }
    }
    return noStat; // if no stats then ON, if no stats on then false
#elif defined(USE_TIMER1_STAT)
    return getStatState(c, 1) == STAT_ON;
#else
    return true;
#endif

  } else if (isBoostAdv(c)) {
    if (inTimerPeriod(c, 0)) { // timer0
#ifdef USE_TIMER1_STAT
      return getStatState(c, 1) == STAT_ON;
#else
      return true;
#endif
    } else {
      // boost/adv period completed so cancel boost/adv & set control back to previous mode
      timerConfig.controls[c].state = timerConfig.controls[c].prevState; // restore previous setting
      timerConfig.controls[c].timers[0].offTime = timerConfig.controls[c].timers[0].onTime; // cancel timer0
      needToSaveConfigFlag = true;
    }

  } else { // auto mode so check timer1..n
    for (int t = 1; t < MAX_TIMERS; t++) {
      if (inTimerPeriod(c, t)) {
        return getStatState(c, t) == STAT_ON;
      }
    }
  }
  return false;
}

void setOutputs() {

  static millisDelay outputChangeTimer;
  
  readTemps();

  // start timer if not running
  if (!outputChangeTimer.isRunning())
    outputChangeTimer.start(OUTPUT_CHANGE_PERIOD);

  if (outputChangeTimer.justFinished()) {
    outputChangeTimer.repeat();

    bool aCtrlIsOn = false;
    for (int c = 0; c < ctrl_count; c++) {
      if (isCtrlOn(c)) {
        aCtrlIsOn = true;
        // set output pin and reboot if turning off in auto
        setPinOn(c); // ON
        if (!timerWasOn) {
          timerWasOn = true;
          timerTurnedOnInAuto = isAutoSelected(c) || isBoostAdv(c);
        }
      } else {
        setPinOff(c); // OFF
        if (timerWasOn) {
          timerWasOn = false;
          if ((isAutoSelected(c) || isBoostAdv(c)) && timerTurnedOnInAuto) // it was ON in select Auto outside timer this reboots
            // reboot when turned off in auto, to clear memory leaks (if any)
            ESP.restart();
        }
        timerTurnedOnInAuto = false;
      }
    }

    if (rebootIfOffDelay.justFinished()) {
      // reboot every 24hrs if off to clear memory leaks (if any)
      debugln("24hr reboot triggered");
      rebootWhenOff = true;
    }
    if (rebootWhenOff && !aCtrlIsOn) { // no active ctrls so safe to reboot
      ESP.restart(); // see https://github.com/esp8266/Arduino/issues/1017  seems to work here
    }
  }
}

//==================================================

String getTZvalue() { // the current tz value
  return String(timerConfig.tzStr);
}

/** returns true if config saved */
bool saveConfigIfNeeded() { // saves any config changes
  if (needToSaveConfigFlag) {
    saveTimerConfig(timerConfig);
    needToSaveConfigFlag = false;
    timerTurnedOnInAuto = false; // skip reboot until next cycle
    rebootIfOffDelay.restart(); // delay reboot for 24hrs after changes
    return true;
  }
  return false;
}

// save control name if changed
void saveCtrlName(const int c, const char * name) {
  if (strncmp(name, getCtrlName(c), MAX_NAME_LEN) != 0) {
    strncpy(timerConfig.controls[c].name, name, strlen(name) + 1); 
    needToSaveConfigFlag = true;
  } 
}

char * getCtrlName(int c) {
  return timerConfig.controls[c].name;
}

int getOnTime_mins(int c, int t) {
  return timerConfig.controls[c].timers[t].onTime;
}
int getOffTime_mins(int c, int t) {
  return timerConfig.controls[c].timers[t].offTime;
}
int8_t getTemp(int c, int t) {
  return timerConfig.controls[c].timers[t].temp;
}
uint8_t getHyst(int c, int t) {
  return GET_TMR_HYST(c,t);
}
char getLess(int c, int t) {
  return GET_TMR_LESSTHAN(c,t) == LESSTHAN ? 'L' : 'M';
}
int8_t getStat(int c, int t) {
  return timerConfig.controls[c].timers[t].statIdx;
}
int16_t getCurrStatVal(int c, int t) {
  if (timerConfig.controls[c].timers[t].statIdx != NO_STAT)
    return currStatVal[timerConfig.controls[c].timers[t].statIdx];
  else
    return 0;
}
char getSetting(int c) {
  return timerConfig.controls[c].state;
}

void setUTCconfigTime() {
  time_t now = time(nullptr);
  timerConfig.utcTime = now;
};

void readTemps() {
  static millisDelay tempTimeTimer;
  static uint8_t s = 0; // adc to read
  
  if (!tempTimeTimer.isRunning())
    tempTimeTimer.start(TEMP_READ_PERIOD);

  if (tempTimeTimer.justFinished()) {
    tempTimeTimer.repeat();
    s++;
    if (s >= stat_count)
      s = 0; // reset to stat#0 again
    
    // read the stat
    int16_t temp = 0;
    if (statPins[s].pinNum != PIN_NOT_AVAIL) {
//debugf("read stat pin %02X = \n", statPins[s].pinNum);
      if (IS_STAT_DIGITAL(s)) {
        if (IS_STAT_EXPANDER(s)) { // MCP23017 expander?
          temp = DIGexpander[GET_STAT_EXP_ID(s)]->digitalRead(GET_STAT_EXP_CH(s));
        } else {
          temp = digitalRead(statPins[s].pinNum);
        }

      } else { // analog
        if (IS_STAT_EXPANDER(s)) { // adc1115 expander
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

        if (IS_STAT_STEINHART(s)) { // if enabled
          // Steinhart–Hart equation if enabled!
          float ratio = (float)temp/1024.0;
          float resistance = statPins[s].puRes * ratio / (1 - ratio);
          // Calculate temperature from resistance
          temp = (1 / ((1 / (25 + 273.15)) + ((log(resistance / statPins[s].resistance)) / statPins[s].beta))) - 273.15;
        }
      }
//debugf("  res %i\n", temp);
    }
    currStatVal[s] = temp; // save temp or state
  }
}
