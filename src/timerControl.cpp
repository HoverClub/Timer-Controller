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

#include "timerControl.h"
#include "DebugOut.h"
#include "LittleFSsupport.h"
#include "tzPosix.h"
#include <coredecls.h>                  // settimeofday_cb()
#include <millisDelay.h>
#include <time.h>                       // time() ctime()
#include <sys/time.h>                   // struct timeval
#include <sntp.h>                       // sntp_servermode_dhcp()
#include <IPAddress.h>

// define a weak getDefaultTZ method that can be defined elsewhere if you want to set a default TZ
const char* get_ntpSupport_DefaultTZ() __attribute__((weak));

// normally DEBUG is commented out
//#define DEBUG
static Stream* debugPtr = NULL;  // local to this file

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

// arrays of controls and timers
timerConfig_struct timerConfig;

// output IO pins
// max pin drive (source/HIGH) is 12mA. , max sink/LOW current is 20mA
// Drive capacity current of all GPIO pins total can be 16 x 12 mA.
// add 1000uF capacitor across pins 1(+ve) and 2(-ve) of the opto-isolator on the relay board to prevent the relay turning on 
//  momentarily on power up/reboot
// BE CAREFUL which output pins you use!!  Some will cause WDT errors
//  https://randomnerdtutorials.com/esp8266-pinout-reference-gpios/
// pin#s are GPIO numbers - NOT numbers on PCB!

// # of entries MUST MATCH the number of controls (MAX_CTRLS)!!!

const pin_struct pins[MAX_CTRLS] = {
#ifdef CTRLPIN0
  {CTRLPIN0, true}
#endif
#ifdef CTRLPIN1
  ,{CTRLPIN1, true}
#endif
#ifdef CTRLPIN2
  ,{CTRLPIN2, true}
#endif
#ifdef CTRLPIN3
  ,{CTRLPIN3, true}
#endif
#ifdef CTRLPIN4
  ,{CTRLPIN4, true}
#endif
#ifdef CTRLPIN5
  ,{CTRLPIN5, true}
#endif
#ifdef CTRLPIN6
  ,{CTRLPIN6, true}
#endif
#ifdef CTRLPIN7
  ,{CTRLPIN7, true}
#endif
#ifdef CTRLPIN8
  ,{CTRLPIN8, true}
#endif
#ifdef CTRLPIN9
  ,{CTRLPIN9, true}
#endif

// ... extend to match MAX_CTRLS ...

};

// temperature sensors
int8_t currTemps[MAX_STATS] = {0};

// adc pins - index
stat_struct statPins[MAX_STATS] = {
#ifdef STATPIN0
  {STATPIN0, STAT_NOT_MPLEX, 3950, 1000, 3000}
#endif
#ifdef STATPIN1
  ,{STATPIN1, STAT_IS_DIGITAL, 0, 0, 0} // digital input pin
#endif
#ifdef STATPIN2
  ,{STATPIN2, STAT_IS_DIGITAL, 1}
#endif
#ifdef STATPIN3
  ,{STATPIN3, STAT_IS_DIGITAL, 1}
#endif
#ifdef STATPIN4
  ,{STATPIN4, STAT_NOT_MPLEX, 1}
#endif
#ifdef STATPIN5
  ,{STATPIN5, STAT_NOT_MPLEX, 1}
#endif
#ifdef STATPIN6
  ,{STATPIN6, STAT_NOT_MPLEX, 1}
#endif
#ifdef STATPIN7
  ,{STATPIN7, STAT_NOT_MPLEX, 1}
#endif
#ifdef STATPIN8
  ,{STATPIN8, STAT_NOT_MPLEX, 1}
#endif
#ifdef STATPIN9
  ,{STATPIN9, STAT_NOT_MPLEX, 1}
#endif
};

// OLED display interface default pins
// SDA = 4; // D2 on D1 mini pcb
// SCL = 5; // D1

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

// call cleanUpfirst
void setTZfromPOSIXstr(const char* tz_str) {
  time_t now = time(nullptr);
  timerConfig.utcTime = now;
  strlcpy(timerConfig.tzStr, tz_str, sizeof(timerConfig.tzStr));
  if (debugPtr) {
    debugPtr->print("setTZfromPOSIXstr:"); debugPtr->println(timerConfig.tzStr);
  }
  setTZ(tz_str);
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
  for (int c = 0; c < MAX_CTRLS; c++) {
    memset(timerConfig.control[c].name, 0, MAX_NAME_LEN + 1);
    timerConfig.control[c].setting = OFF;
    timerConfig.control[c].prevState = OFF;
    for (int t = 0; t < MAX_TIMERS; t++) {
      timerConfig.control[c].timer[t].offTime = 0;
      timerConfig.control[c].timer[t].offTime = 0;
      timerConfig.control[c].timer[t].lessthan = LESSTHAN;
      timerConfig.control[c].timer[t].temp = 20; // deg C
      timerConfig.control[c].timer[t].stat = NO_STAT;
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

void printtimerConfig(Stream & out) {
  for (int c = 0; c < MAX_CTRLS; c++) {
    out.printf("\nCtrl%i state: %c:%c", c, timerConfig.control[c].setting, timerConfig.control[c].prevState);
    for (int t = 0; t < MAX_TIMERS; t++) {
      out.printf("\n  Timer%i : %i->%i, stat:%i when: %c %i", 
      t,
      timerConfig.control[c].timer[t].onTime,
      timerConfig.control[c].timer[t].offTime,
      timerConfig.control[c].timer[t].stat,
      timerConfig.control[c].timer[t].lessthan,
      timerConfig.control[c].timer[t].temp
      );
    }
  }
}

// load the last time saved before shutdown/reboot
// returns pointer to timerConfig
timerConfig_struct* loadTimerConfig() {
#ifdef DEBUG
  debugPtr = getDebugOut();
#endif
  setInitialtimerConfig();
  if (!initializeFS()) {
    if (debugPtr) {
      debugPtr->println("FS failed to initialize");
    }
    return &timerConfig; // returns default if cannot open FS
  }
  if (!LittleFS.exists(timerConfigFileName)) {
    if (debugPtr) {
      debugPtr->print(timerConfigFileName); debugPtr->println(" missing.");
    }
    saveTimerConfig(timerConfig);
    return &timerConfig; // returns default if missing
  }
  // else load config
  File f = LittleFS.open(timerConfigFileName, "r");
  if (!f) {
    if (debugPtr) {
      debugPtr->print(timerConfigFileName); debugPtr->print(" did not open for read.");
    }
    LittleFS.remove(timerConfigFileName);
    saveTimerConfig(timerConfig);
    return &timerConfig; // returns default wrong size
  }
  if (f.size() != sizeof(timerConfig)) {
    if (debugPtr) {
      debugPtr->print(timerConfigFileName); debugPtr->print(" wrong size.");
    }
    f.close();
    saveTimerConfig(timerConfig);
    return &timerConfig; // returns default wrong size
  }
  int bytesIn = f.read((uint8_t*)(&timerConfig), sizeof(timerConfig));
  if (bytesIn != sizeof(timerConfig)) {
    if (debugPtr) {
      debugPtr->print(timerConfigFileName); debugPtr->print(" wrong size read in.");
    }
    setInitialtimerConfig(); // again
    f.close();
    saveTimerConfig(timerConfig);
    return &timerConfig;
  }
  f.close();
  // else return settings
  // clean up tz and return
  cleanUpPosixTZStr(timerConfig.tzStr, sizeof(timerConfig.tzStr));
  
  if (debugPtr) {
    debugPtr->println("Loaded config");
    printtimerConfig(*debugPtr);
    String desc = timerConfig.tzStr;
    struct posix_tz_data_struct posixTz;
    posixTZDataFromStr(desc,posixTz);
    buildPOSIXdescription(posixTz, desc);
    debugPtr->println("TZ description");
    debugPtr->println(desc);
  }

  return &timerConfig;
}

// load the last time saved before shutdown/reboot
bool saveTimerConfig(timerConfig_struct& timerConfig) {
  if (!initializeFS()) {
    if (debugPtr) {
      debugPtr->println("FS failed to initialize");
    }
    return false;
  }
  // else save config
  File f = LittleFS.open(timerConfigFileName, "w"); // create/overwrite
  if (!f) {
    if (debugPtr) {
      debugPtr->print(timerConfigFileName); debugPtr->print(" did not open for write.");
    }
    return false; // returns default wrong size
  }
  setUTCconfigTime(); // update utc time
  int bytesOut = f.write((uint8_t*)(&timerConfig), sizeof(timerConfig_struct));
  if (bytesOut != sizeof(timerConfig_struct)) {
    if (debugPtr) {
      debugPtr->print(timerConfigFileName); debugPtr->print(" write failed.");
    }
    return false;
  }
  // else return settings
  f.close(); // no rturn
  if (debugPtr) {
    debugPtr->print(timerConfigFileName); debugPtr->println(" config saved.");
    printtimerConfig(*debugPtr);
  }
  return true;
}

/* optional parameter can be used with ESP8266 Core 3.0.0*/
static void time_is_set(bool from_sntp /* <= this parameter only avail in 2.0is optional */) {
  if (debugPtr) {
    debugPtr->print("time_is_set from "); debugPtr->println(from_sntp ? "SNTP" : "USER");
    debugPtr->print("UTC   "); debugPtr->println(getUTCTime());
    debugPtr->print("Local "); debugPtr->println(getCurrentTime_hhmm());
  }

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

// start sntp server and updates time and then stops server
void initializeSNTP() {
#ifdef DEBUG
  debugPtr = getDebugOut();
#endif
  if (debugPtr) {
    debugPtr->print("initializeSNTP"); debugPtr->println();
  }
  loadTimerConfig(); // load timerConfig global and cleans up tzStr

  // install callback - called when settimeofday is called (by SNTP or user)
  // once enabled (by DHCP), SNTP is updated every hour by default
  // ** optional boolean in callback function is true when triggered by SNTP **
  settimeofday_cb(time_is_set);
  static timeval tv;
  tv.tv_sec = timerConfig.utcTime;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
  ntpUpdateCheck.start(NTP_NOT_UPDATED_MS); // start monitor
  // handle POSIX tz start
  cleanUpPosixTZStr(timerConfig.tzStr, sizeof(timerConfig.tzStr));
  configTime(timerConfig.tzStr, "pool.ntp.org"); // << this starts sntp 0.pool.ntp.org does not not work??
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
  debugPtr->print(" " #w "="); \
  debugPtr->print(tm->tm_##w);

void printTm(const char* what, const tm * tm) {
  debugPtr->print(what);
  PTM(isdst); PTM(yday); PTM(wday);
  PTM(year);  PTM(mon);  PTM(mday);
  PTM(hour);  PTM(min);  PTM(sec);
}

void showTimeDebug() {
  if (!debugPtr) {
    return;
  }
  debugPtr->println("ShowTimeDebug");
  timeval tv;
  timespec tp;
  time_t now;
  uint32_t now_ms, now_us;

  gettimeofday(&tv, nullptr);
  clock_gettime(0, &tp);
  now = time(nullptr);
  now_ms = millis();
  now_us = micros();

  debugPtr->println();
  printTm("localtime:", localtime(&now));
  debugPtr->println();
  printTm("gmtime:   ", gmtime(&now));
  debugPtr->println();

  // time from boot
  debugPtr->print("clock:     ");
  debugPtr->print((uint32_t)tp.tv_sec);
  debugPtr->print("s + ");
  debugPtr->print((uint32_t)tp.tv_nsec);
  debugPtr->println("ns");

  // time from boot
  debugPtr->print("millis:    ");
  debugPtr->println(now_ms);
  debugPtr->print("micros:    ");
  debugPtr->println(now_us);

  // EPOCH+tz+dst
  debugPtr->print("gtod:      ");
  debugPtr->print((uint32_t)tv.tv_sec);
  debugPtr->print("s + ");
  debugPtr->print((uint32_t)tv.tv_usec);
  debugPtr->println("us");

  // EPOCH+tz+dst
  debugPtr->print("time:      ");
  debugPtr->println((uint32_t)now);

  // timezone and demo in the future
  debugPtr->printf("timezone:  %s\n", getenv("TZ") ? : "(none)");

  // human readable
  debugPtr->print("ctime:     ");
  debugPtr->print(ctime(&now));

  // lwIP v2 is able to list more details about the currently configured SNTP servers
  for (int i = 0; i < SNTP_MAX_SERVERS; i++) {
    IPAddress sntp = *sntp_getserver(i);
    const char* name = sntp_getservername(i);
    if (sntp.isSet()) {
      debugPtr->printf("sntp%d:     ", i);
      if (name) {
        debugPtr->printf("%s (%s) ", name, sntp.toString().c_str());
      } else {
        debugPtr->printf("%s ", sntp.toString().c_str());
      }
      debugPtr->printf("- IPv6: %s - Reachability: %o\n",
                       sntp.isV6() ? "Yes" : "No",
                       sntp_getreachability(i));
    }
  }

  debugPtr->println();

  // show subsecond synchronisation
  timeval prevtv;
  time_t prevtime = time(nullptr);
  gettimeofday(&prevtv, nullptr);

  while (true) {
    gettimeofday(&tv, nullptr);
    if (tv.tv_sec != prevtv.tv_sec) {
      debugPtr->printf("time(): %u   gettimeofday(): %u.%06u  seconds are unchanged\n",
                       (uint32_t)prevtime,
                       (uint32_t)prevtv.tv_sec, (uint32_t)prevtv.tv_usec);
      debugPtr->printf("time(): %u   gettimeofday(): %u.%06u  <-- seconds have changed\n",
                       (uint32_t)(prevtime = time(nullptr)),
                       (uint32_t)tv.tv_sec, (uint32_t)tv.tv_usec);
      break;
    }
    prevtv = tv;
    delay(1);
  }

  debugPtr->println();
}

//============================================
// output handling

// pin = io pin#, active = true if active hi, false if active low
void setPinOff(pin_struct p) {
  if (p.num != PIN_NOT_AVAIL)
    digitalWrite(p.num, p.active ? LOW : HIGH); // OFF
}
void setPinOn(pin_struct p) {
  if (p.num != PIN_NOT_AVAIL)
    digitalWrite(p.num, p.active ? HIGH : LOW); // ON
}


void initOutputs() {
    // set control output pins
  for (int p = 0; p < MAX_CTRLS; p++) {
    if (pins[p].num != PIN_NOT_AVAIL) {
      pinMode(pins[p].num, OUTPUT);
      setPinOff(pins[p]);
    }
  }

  // set sensor input pins
  for (int t = 0; t < MAX_STATS; t++) {
    if (statPins[t].mplex != STAT_NOT_MPLEX) {
      pinMode(statPins[t].mplex, INPUT); // disable any multiplex output
    } else // adc OR digital input
      pinMode(statPins[t].pin, INPUT);
  }
}

//--------------------------------------
// set new control state if different to current state
void setOff(int c) {
  if (timerConfig.control[c].setting != OFF) {
    timerConfig.control[c].setting = OFF;
    timerConfig.control[c].timer[0].offTime = 0;
    timerConfig.control[c].timer[0].onTime = 0;// clr any boost/adv
    needToSaveConfigFlag = true;
  }
}
void setOn(int c) {
  if (timerConfig.control[c].setting != ON) {
    timerConfig.control[c].setting = ON;
    timerConfig.control[c].timer[0].offTime = 0;
    timerConfig.control[c].timer[0].onTime = 0; // clr any boost/adv
    needToSaveConfigFlag = true;
  }
}
void setAuto(int c) {
  if (timerConfig.control[c].setting != AUTO) {
    timerConfig.control[c].setting = AUTO;
    timerConfig.control[c].timer[0].offTime = 0;
    timerConfig.control[c].timer[0].onTime = 0; // clr any boost/adv
    needToSaveConfigFlag = true;
  }
}
void setBoost(int c) { // timer0 is boost, start timer[0] is now, offtime now + 60mins, uses temp/stat from timer1
  if (!isBoostAdv(c))
    timerConfig.control[c].prevState = timerConfig.control[c].setting; //save previous setting
  timerConfig.control[c].setting = BOOST;
  timerConfig.control[c].timer[0].onTime = getLocalTime_mins();
  timerConfig.control[c].timer[0].offTime = (timerConfig.control[c].timer[0].onTime + 60 + 1440U) % 1440U;
  needToSaveConfigFlag = true;
}
void setAdv(int c) { // start timer0 now, timer 0 offtime/temp copied from timer1
  if (timerConfig.control[c].setting != ADV) {
    if (timerConfig.control[c].timer[1].onTime != timerConfig.control[c].timer[1].offTime) { // only set if there is a valid timer1 time set!
      if (!isBoostAdv(c))
        timerConfig.control[c].prevState = timerConfig.control[c].setting; // save previous setting
      timerConfig.control[c].setting = ADV;
      timerConfig.control[c].timer[0].onTime = getLocalTime_mins();
      timerConfig.control[c].timer[0].offTime = timerConfig.control[c].timer[1].offTime;
      needToSaveConfigFlag = true;
    }
  }
}
//--------------------------------------

bool isOffSelected(int c) {
  return timerConfig.control[c].setting == OFF;
}
bool isOnSelected(int c) {
  return timerConfig.control[c].setting == ON;
}
bool isAutoSelected(int c) {
  return timerConfig.control[c].setting == AUTO;
}
bool isBoostAdv(int c) {
  return (timerConfig.control[c].setting == BOOST) || (timerConfig.control[c].setting == ADV);
}

//--------------------------------------
// set new timer values IF they ae different to existing values
// includes basiv validatiion od new value

// timer set
void setOnTime(int c, int t, int val){
  if (val != -1 && val != timerConfig.control[c].timer[t].onTime) {
    timerConfig.control[c].timer[t].onTime = val;
    needToSaveConfigFlag = true;
  }
}
void setOffTime(int c, int t, int val) {
  if (val != -1 && val != timerConfig.control[c].timer[t].offTime) {
    timerConfig.control[c].timer[t].offTime = val;
    needToSaveConfigFlag = true;
  }
}
void setTemp(int c, int t, int val) {
  if (val > -127 && val < 127 && val != timerConfig.control[c].timer[t].temp) {
    timerConfig.control[c].timer[t].temp = val;
    needToSaveConfigFlag = true;
  }
};
void setLess(int c, int t, char val) {
  if ((val == 'L' || val == 'M') && val != timerConfig.control[c].timer[t].lessthan) {
    timerConfig.control[c].timer[t].lessthan = val;
    needToSaveConfigFlag = true;
  }
};
void setStat(int c, int t, int val) {
  if (val >= NO_STAT && val < MAX_STATS && val != timerConfig.control[c].timer[t].stat) {
    timerConfig.control[c].timer[t].stat = val;
    needToSaveConfigFlag = true;
  }
};
//--------------------------------------

// check if timer has a stat and, if so, check stat value is ON
// returns true for ON, false for OFF
bool checkStat(int c, int t) {
#if MAX_STATS
  if (timerConfig.control[c].timer[t].stat != -1) { // got a stat on this timer (-1 = not used)?
    if (    
          // adc pins
          ( statPins[timerConfig.control[c].timer[t].stat].mplex != STAT_IS_DIGITAL 
            &&  
            (
              (
                timerConfig.control[c].timer[t].lessthan == LESSTHAN
                && currTemps[timerConfig.control[c].timer[t].stat] < timerConfig.control[c].timer[t].temp
              )
              ||
              ( 
                timerConfig.control[c].timer[t].lessthan == MORETHAN
                && currTemps[timerConfig.control[c].timer[t].stat] > timerConfig.control[c].timer[t].temp
              )
            )
          )
          ||
          // digital pins
          ( statPins[timerConfig.control[c].timer[t].stat].mplex == STAT_IS_DIGITAL
            &&
            (
              (timerConfig.control[c].timer[t].lessthan == LESSTHAN && currTemps[timerConfig.control[c].timer[t].stat] == LOW )
                ||
              (timerConfig.control[c].timer[t].lessthan == MORETHAN && currTemps[timerConfig.control[c].timer[t].stat] == HIGH)
            ) 
          )
        )
      return true; // ON
    else  
      return false; // OFF
  }
#endif
  return true; // no stats defined = ON
}

// returns true if the current time is within the on/off timer times
bool inTimerPeriod(int c, int t) {

  if (timerConfig.control[c].timer[t].offTime != timerConfig.control[c].timer[t].onTime) {
    int current_mins = getLocalTime_mins();
    if (timerConfig.control[c].timer[t].offTime > timerConfig.control[c].timer[t].onTime) {  // onTime then offTime
      if ((current_mins >= timerConfig.control[c].timer[t].onTime) && (current_mins < timerConfig.control[c].timer[t].offTime))
        return true;
    } else {    // offTime then onTime
      if (!((current_mins >= timerConfig.control[c].timer[t].offTime) && (current_mins < timerConfig.control[c].timer[t].onTime)))
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
  if ((timerConfig.control[c].setting == OFF) || (!haveSNTPresponse))
    return false;
  if (timerConfig.control[c].setting == ON)
    return true;
  if (isBoostAdv(c)) { // only check timer 0 if boost or adv active
    if (inTimerPeriod(c, 0)) {
      return checkStat(c, 1); // use timer1 stat for boost/adv!!
    } else {
      // If in boost/adv mode and the timer is OFF then return to previous mode
      timerConfig.control[c].setting = timerConfig.control[c].prevState; // restore previous setting
      timerConfig.control[c].timer[0].offTime = timerConfig.control[c].timer[0].onTime; // cancel timer0
      needToSaveConfigFlag = true;
    }
  } else { // auto mode so check timer1..n
    for (int t = 1; t < MAX_TIMERS; t++) {
      if (inTimerPeriod(c, t) && checkStat(c, t))
        return true;
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
    for (int c = 0; c < MAX_CTRLS; c++) {
      if (isCtrlOn(c)) {
        aCtrlIsOn = true;
        // set output pin and reboot if turning off in auto
        setPinOn(pins[c]); // ON
        if (!timerWasOn) {
          timerWasOn = true;
          timerTurnedOnInAuto = isAutoSelected(c) || isBoostAdv(c);
        }
      } else {
        setPinOff(pins[c]); // OFF
        if (timerWasOn) {
          timerWasOn = false;
          if ((isAutoSelected(c) || isBoostAdv(c)) && timerTurnedOnInAuto) // is was ON in select Auto outside timer this reboots
            // reboot when turned off in auto, to clear memory leaks (if any)
            ESP.restart();
        }
        timerTurnedOnInAuto = false;
      }
    }

    if (rebootIfOffDelay.justFinished()) {
      // reboot every 24hrs if off to clear memory leaks (if any)
      if (debugPtr) {
        debugPtr->println("24hr reboot triggered");
      }
      rebootWhenOff = true;
    }
    if (rebootWhenOff && !aCtrlIsOn) {
      // off so safe to reboot
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
    strncpy(timerConfig.control[c].name, name, strlen(name) + 1); 
    needToSaveConfigFlag = true;
  } 
}

char * getCtrlName(int c) {
  return timerConfig.control[c].name;
}

int getOnTime_mins(int c, int t) {
  return timerConfig.control[c].timer[t].onTime;
}
int getOffTime_mins(int c, int t) {
  return timerConfig.control[c].timer[t].offTime;
}
int8_t getTemp(int c, int t) {
  return timerConfig.control[c].timer[t].temp;
}
char getLess(int c, int t) {
  return timerConfig.control[c].timer[t].lessthan;
}
int8_t getStat(int c, int t) {
  return timerConfig.control[c].timer[t].stat;
}
int8_t getCurrTemp(int c, int t) {
  if (timerConfig.control[c].timer[t].stat != -1)
    return currTemps[timerConfig.control[c].timer[t].stat];
  else
    return 0;
}
char getSetting(int c) {
  return timerConfig.control[c].setting;
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
    if (s >= MAX_STATS)
      s = 0; // reset to stat#0 again
    
    // read the ADC
    int temp = 0;
    if (statPins[s].pin != PIN_NOT_AVAIL) {
      if (statPins[s].mplex == STAT_IS_DIGITAL)
        temp = digitalRead(statPins[s].pin);
      else {
        if (statPins[s].mplex == STAT_NOT_MPLEX) {
          temp = analogRead(statPins[s].pin);
        } else {
          digitalWrite(statPins[s].mplex, LOW);
          pinMode(statPins[s].pin, OUTPUT); // enable sensor
          delay(1);   // short delay;
          temp = analogRead(statPins[s].pin);
          pinMode(statPins[s].pin, INPUT); // disable sensor
        }
        // Steinhart–Hart equation
        float ratio = (float)temp/1024.0;
        float resistance = statPins[s].puRes * ratio / (1 - ratio);
        // Calculate temperature from resistance
        temp = (1 / ((1 / (25 + 273.15)) + ((log(resistance / statPins[s].resistance)) / statPins[s].beta))) - 273.15;
//Serial.printf("T = %i\n", temp);
      }
    }
    currTemps[s] = temp; // save temp
  }
}
