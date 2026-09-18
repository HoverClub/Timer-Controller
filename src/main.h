#ifndef _CTRL_H
#define _CTRL_H

#include <Arduino.h>
// Platform-specific includes must come first to ensure definitions are available
#ifdef ESP32
  #include <mutex>
  #include <WiFi.h>
  #include <esp_sntp.h>
  #include <SPIFFS.h>
  #include <FS.h>
#else // ESP8266
//  #include <esp8266_mutex.h>
  #include <ESP8266WiFi.h>
  #include <ESPAsyncTCP.h>
  #include <sntp.h>            // sntp_servermode_dhcp()
  #include <user_interface.h> // For ETS_SEMAPHORE functions
  #include <coredecls.h>      // settimeofday_cb()
#endif

#ifdef ESP32
  #include <time.h>                       // time() ctime()
  #include <sys/time.h>                   // struct timeval
  #include "ESPmDNS.h"
#else
  #include <ESP8266mDNS.h>
  #include <WiFiClient.h>
#endif
#include <IPAddress.h>
#include "PinFlasher.h"
#include <millisDelay.h>
#include "limits.h"
#include <ESPAsyncWebServer.h>
#include <millisDelay.h>
#include <SafeString.h>
#include <SPI.h>
#include <Adafruit_ADS1X15.h>
#include "MCP23017.h"

#ifdef ESP32
  // Use standard C++ mutex for ESP32
  extern SemaphoreHandle_t FSmutex; // used to sync file rd/wr ops
#else
  typedef int32 mutex_t;
  extern mutex_t * FSmutex; // used to sync file rd/wr ops
  extern bool ICACHE_FLASH_ATTR GetMutex(mutex_t *mutex);
  extern void ICACHE_FLASH_ATTR ReleaseMutex(mutex_t *mutex);
#endif
#define oled_none 0
#define oled_96 1
#define oled_13 2

//==================================================================
//                COMPILE TIME MODIFIABLE SETTINGS
//==================================================================
/**
 * @brief Compile time options section
 *
*/

#define CONTROLLER_NAME "Example"   // Default [MAX_NAME_LEN] name for this controller.
                                    // is displayed on the home web page.

#define MAX_CONTROLS 126 // maximum # of controls (0-63 = ECU pins, 64-126 expander pins) - max 126

#define MAX_TIMERS 126  // max # of timers per control (1-126). # of user timers is MAX_TIMERS - 1 
                        // (first user-programmable timer is timer1).

#define MAX_SENSORS 126  // max # of input sensors (0-63 = ECU pins, 64-126 digital expander pins, 64-80
                         // analog expander pins).

#define OLED_TYPE oled_13 // OLED type (if connected) : oled_none, oled_96, oled_13

// how often (in mSecs) to read input sensors
// delay between reading the same sensor is = #Sensors x TEMP_READ_PERIOD
// system timer response time (output changes) are 1 minute minimum so make sure ALL sensors have been read within
// that time (60secs / # of sensors).
#define TEMP_READ_PERIOD 200

#define OUTPUT_CHECK_PERIOD 4000 // how often control output levels are checked (mSec)
                                  // a 126 control/126 timers per control system can take ~3 ecs to check all timers

/** 
 * ON and BOOST/ADV modes
 * 
 * USE_ANY_TIMER_STAT: ON mode only! Will only turn 
 * on a control if one of it's timers has an active stat
 * that is on -OR- if no timers have stats, the control
 * will turn on.
 * 
 * USE_TIMER1_STAT:  Boost/adv & ON modes.  Will only 
 * turn the control on if the timer1 stat is on.
 * 
 * Default (both commented out) is that the control turns 
 * on without any stat checks for ON and BOOST modes.
*/
//#define USE_ANY_TIMER_STAT
#define USE_TIMER1_STAT

/**
 * WiFi Network settings
 *
*/
// Replace these with your local network's SSID and password if you will only 
// use this device on that network (the network config page won't show)
// To be able move the device between networks leave empty! They are used as 
// default if nothing valid has been saved in WiFi settings.
#define wifiSSID "" 
#define wifiPassword ""

// WiFi access point settings (configuration web page IP access)
#define wifiWebConfigPASSWORD ""
#define wifiWebConfigAP ("Timer_Controller")
#define wifiDNSName ("TMRCTRL")
#define LOCAL_IP (IPAddress(10, 1, 1, 1))
#define GATEWAY_IP (IPAddress(10, 1, 1, 1))
#define SUBNET_IP (IPAddress(255, 255, 255, 0))

//==================================================================
//        END OF COMPILE TIME MODIFIABLE SETTINGS
//==================================================================

/**
 * STATS: pin# used to read input stats or switches
 * 
 * NOTE: MUST have same number of pins as stat_count above!!!
 * 
 * ESP8266:
 * 1. Only has one ADC (A0) - code supports multiplexing by grounding then 
 *    reading each sensor in turn using an output pin (all 2nd sensor wires 
 *    are common'ed with all other sensors to the ADC input.
 * 2. IO availability is limited (also further limited by dev board type)
 *      Outputs: GPIO 4/5/12-14 OK
 *                 15/16 & 2 OK but mustn't be held low at startup (2 is 
 *                 onboard LED on some)
 *      Inputs:  GPIO 4/5/12-14 OK
 *               A0 is analog input (0-1V )
 *     
 *      GPIO4/5 (SDA/SCL) are used by OLED & ADC1115 but could be avail for 
 *              IO if neither are required.
 * 
 * 3.ESP8266 4x relay board 
 *  Add jumpers to to connect IO12-14 & 16 to relays
 *  IO15, IO2 (onboard LED) are output only, IO4/5 
 *  used by I2C SCL/SDA, IO1/3 rx/tx for serial
 * 
 * ESP32:
 * 1. Only ADC1 GPIO can be used for analog sensors.  OR use an external 
 *    ADC1115 board via I2C (recommend option as ESP32 ADC is notoriously
 *    innaccurate!).
 * 2. IO availability:
 *      Outputs: GPIO 2 (onboard LED),
 *                4, 5 (outputs pwm at boot),
 *                12, 13-33 OK
 *      Inputs:   GPIO 2 (must float at boot)
 *                4/5,
 *                12 (boot fails mif high)
 *                13-39 OK
 * 
 *      ADC1 inputs GPIO0, 32-39 (0-3.3V)
 *      I2C interface on GPIO 21 (SDA) and 22 (SCL)
 *      I2S interface on GPIO 27 for BCK, GPIO 25 for WS, and GPIO 26 for DATA
 *          but can use any suitable GPIO pins 
 * 
 * Output Pin definitions:
 * The max. GPIO pin# available is 39 (ESP32).  The pin# encoding in the pins[] array is:
 *    b7 = set if the pin is active LOW
 *    b6 = set if pin is on an I2C expander, clr if a GPIO pin
 *    b5-0 = pin ID
 *     if a GPIO pin then it is the hardware pin#
 *    if Expander:
 *      b5-4 - expander #
 *      b3-0 - channel on expander
 * 
 * I2C devices:
 *  OLED display - addr 0x3C
 *  ADC1115 adc expander - addrs 0x48-0x4B (4 max.)
 *  MCP20137 IO expanders - addrs 0x20-0x23 (4 max.) 
 * 
 */

#define OLED_WIDTH 128
#define OLED_HEIGHT 64

#define MIN_CTRLS 1     // minimum # of controls - must be at least 1
#define MIN_TIMERS 2    // default # of timers per control (incls timer 0 for boost/adv)

#define MAX_SYS_NAME_LEN 15 // max system name length (displayed on web page).  +1 must be multiple of 4 to get same alignment on 8255 & ESP32
#define MAX_NAME_LEN 15 // max control name length && SSID length
                        // must be less than 21 to coply with 32chr SSID length!
#define MAX_STAT_NAME_LEN 15 // max stat name length

#define MAX_TZ_LEN 51 // max timezone string length - again multiple of 4 for alignment! 

#define MAX_FILENAME_LEN 31 // max length of any filename - includes any '/' and terminating zero

#if OLED_TYPE != oled_none
  #include <Adafruit_GFX.h>
  #include <qrcode.h>
  #if OLED_TYPE == oled_96
    #include <Adafruit_SSD1306.h>
    extern Adafruit_SSD1306 oled;
  #elif OLED_TYPE == oled_13
    #include <Adafruit_SH1106.h>
    extern Adafruit_SH1106 oled;
  #endif
#endif

// DEBUGPORT # is a build_flag in in platformio.ini
#ifdef DEBUGPORT
  #if (DEBUGPORT==0)
    #define debugf(P, ...) {Serial.printf(P, __VA_ARGS__  );}
    #define debugln(P) {Serial.println(P);}
    #define debug(P) {Serial.print(P);}
  #elif (DEBUGPORT==1)
    #define debugf(P, ...) {Serial1.printf(P, __VA_ARGS__);}
    #define debugln(P) {Serial1.println(P);}
    #define debug(P) {Serial1.print(P);}
  #elif (DEBUGPORT==2)
    #define debugf(P, ...) {Serial2.printf(P, __VA_ARGS__);}
    #define debugln(P) {Serial2.println(P);}
    #define debug(P) {Serial2.print(P);}
  #endif
#else
  #define debugf(P, ...)
  #define debugln(P)
  #define debug(P)
#endif

// stat switch conditions
// (0 -3) << 14 must match HTML <select> options
enum lessthan {
  LESSTHAN = 0x0000,
  MORETHAN = 0x4000,  
  WITHIN = 0x8000,
  OUTWITH = 0xC000
};
#define LESSTHAN_MASK 0xC000

// control modes - off/on/auot/...
enum class CtrlMode : uint8_t {
  OFF = 0, 
  ON = 1, 
  AUTO = 2,
  BOOST = 3,
  ADV = 4
};
// Current Timer states
enum class TmrState : uint8_t {
  STAT_OFF,
  STAT_ON,
};
#define PIN_NOT_AVAIL 0xFF  // if a control or stat pin# isn't valid
#define NO_STAT -1 // no stats used by a timer (can't then use digital expander 3 channel 15!)
#define ANY 0x00; // any (or all) day/weekday/month

struct timer_struct {
  uint32_t  duration = 0;             // in min
  uint32_t  onDate = 0;               // bit for every date enabled 1-31
  int16_t   temp = 20;                // setpoint temperature signed
  uint16_t  onTime = 0;               // in min 0 - 1440
  uint16_t  onMonth = 0;              // bit for every month enabled 1-12bits
  uint16_t  lessHyst = LESSTHAN;      // b14-15 is encoded less/more/within/outwith setting, b0-13 hysteresis/range
  uint8_t   onWeekDay = 0;            // bit is set for every day enabled 1-7bits
  int8_t    statIdx = NO_STAT;        // input sensor index in sensors[], NO_STAT = -1 = disabled, 0-126 = active
};

// control header
struct header_struct {
  char      name[MAX_NAME_LEN + 1]; // +1 for null terminator - name MUST BE 1st element in struct!!!
  uint8_t   pinNum = PIN_NOT_AVAIL; // output pin#  (b7 set if activeLow, b6 set if on I2C expander), b0-5 pin# (b4-5 = expander ID, b0-3 channel#)
  uint8_t   modes = (uint8_t)CtrlMode::OFF;            // b0-3 = current mode, b4-7 previous mode (restored after boost/adv completes)
};

// /control_xx.bin file 
struct control_struct {
  header_struct header;
  timer_struct timers[MAX_TIMERS];
};

// /settings.bin
///  NOTE: time_t is uint32_t on ESP32 but int64_t on ESP8266 so we convert to/from time_t before using the saved int64 value
struct settings_struct {
  char name[MAX_SYS_NAME_LEN + 1]; // multiple of 4 to align same on 8266 as on esp32!!!. Controller name (on Web page)
  int64_t utcTime; // sec since 1/1/1970  = if not yet set by SNTP or timezonedb.com
  char tzStr[MAX_TZ_LEN + 1]; // POSIX tz eg AEST-10AEDT,M10.1.0,M4.1.0/3  if empty then skip setting tzStr and just use user set local time and sntp utc to calculate tz offset
};

// in /sensors.bin file:
// 8266 MAX ADC voltage is 1V.  ESP32 and ADC1115 I2C expander use 3.3V as reference
struct sensor_struct {
  uint8_t pinNum;       // input pin to read (0xff = no pin, b7 set = digital, b6 set = expander, b0-5 = pin# or expander id and channel)
  char name[MAX_STAT_NAME_LEN + 1]; // +1 for null terminator         
  // remaining items are only for analog sensors
  uint8_t  mplex;       // if b7 set then sensor is mplexed and b0-6 is the pin# to be gnd'ed to activate this sensor.
  uint16_t beta;        // B value for thermistor - set to zero if no Steinhart eqn to be applied)
  uint16_t resistance;  // thermistor resistance @ 25c div by 10 (1-65535ohm range)
  uint16_t puRes;       // resistance div by 10 of pull up resistor on the thermistor (assumes vRef is 3.3V)
};

#define STAT_DIGITAL 0x80
#define STAT_EXPANDER 0x40
#define STAT_EXPANDERID_MASK 0x30 // b5-4 are expander ID# 0-3
#define STAT_MPLEX 0x80

// mplex values in stat table
#define IS_STAT_PIN_AVAIL(s) (sensors[s].pinNum != PIN_NOT_AVAIL)
#define IS_STAT_DIGITAL(s) ((sensors[s].pinNum >> 7) & 1)
#define IS_STAT_ANALOG(s) (!IS_STAT_DIGITAL(s))
#define IS_STAT_EXPANDER(s) ((sensors[s].pinNum >> 6) & 1)
#define IS_STAT_MPLEX(s) (IS_STAT_ANALOG(s) && (sensors[s].mplex >> 7))
#define IS_STAT_STEINHART(s) (sensors[s].beta != 0)
#define GET_STAT_MPLEX_PIN(s) (sensors[s].mplex & ~(STAT_MPLEX))
#define GET_STAT_PIN(s) (sensors[s].pinNum & ~(STAT_DIGITAL | STAT_EXPANDER))

#define GET_STAT_EXP_ID(s) ((GET_STAT_PIN(s) >> 4) & 0x03)// 0-3 ID
#define GET_STAT_EXP_CH(s) (sensors[s].pinNum & 0x0F)  // 0-15

//==================================================================

#define GET_TMR_HYST(buffer) (buffer.lessHyst & ~LESSTHAN_MASK)
#define GET_TMR_LESSTHAN(buffer) ((buffer.lessHyst & LESSTHAN_MASK) >> 14) // 0=3

/**
 * CONTROLS: output pins driven by each control 
 *
 * NOTE: To add a control, define an ouput pin and add it to the pins[] array
 * 
 * For ESP8266 usable output pins are: 5-7 & 12-14
 * 16 & 4 with care.  Pin#s are GPIO numbers - NOT numbers on PCB!
 * 
 * ESP8266 4x relay board: 
 * Add jumpers to to connect IO12-14 & 16 to relays
 * IO15, IO2 (onboard LED) are output only, IO4/5 
 * used by OLED SCL/SDA, IO1/3 rx/tx for serial
 * 
 * Max pin drive (source/HIGH) is 12mA. , max sink/LOW current is 20mA
 * Drive capacity current of all GPIO pins total can be 16 x 12 mA.
*/
#define PIN_ACTIVELO 0x80 // b7 set if active low output pin
#define PIN_EXPANDER 0x40 // b6 set if output pin is on an I2C expander
#define PIN_EXPANDERID_MASK 0x30 // b5-4 are expander ID# 0-3

// physical pin numbers available on WeMos D1 Mini ESP32 board
//#define PIN_MAP {0, 2, 4, 5, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33, 34, 35}
#ifdef ESP32
  #define CTRLPIN0 16 | PIN_ACTIVELO // an active low output!
  #define CTRLPIN1 17
  #define CTRLPIN2 18
  #define CTRLPIN3 19
#else
  #define CTRLPIN0 16 | PIN_ACTIVELO // an active low output!
  #define CTRLPIN1 14
  #define CTRLPIN2 12
  #define CTRLPIN3 0x2A | PIN_ACTIVELO | PIN_EXPANDER // on expander #2 channel 10
#endif
  #define CTRLPIN4 PIN_NOT_AVAIL

#define IS_PIN_AVAIL(p) (p != PIN_NOT_AVAIL)
#define IS_PIN_EXPANDER(p) (p & PIN_EXPANDER)
#define IS_PIN_ACTIVELO(p) ((p & PIN_ACTIVELO) >> 7)
#define IS_PIN_DIGITAL(p) ((p & STAT_DIGITAL) >> 7)
#define GET_PIN_EXP_ID(p) ((p & 0x30) >> 4) // 0-3
#define GET_PIN_EXP_CH(p) (p & 0x0F)  // 0-15
#define GET_PIN_NUM(p) (p & ~(PIN_ACTIVELO | PIN_EXPANDER))
#define GET_PIN_ON_LVL(p) ((p & PIN_ACTIVELO) ? LOW : HIGH) // pin active level

#define GET_MODE(b) (b.modes & 0x0F)
#define GET_PREV_STATE(b) ((b.modes >> 4) & 0x0F)
#define SET_MODE(b, v) (b.modes = (b.modes & 0xF0) | v)
#define SET_CTRLON_MODE(c, v) (ctrlOnMode[c] = (ctrlOnMode[c] & 0x7FF0) | v) // clrs ON b15!
#define IS_MODE_ON(b) (GET_MODE(b) == (uint8_t)CtrlMode::ON)
#define IS_MODE_OFF(b) (GET_MODE(b) == (uint8_t)CtrlMode::OFF)
#define IS_MODE_AUTO(b) (GET_MODE(b) == (uint8_t)CtrlMode::AUTO)
#define IS_MODE_BOOST(b) (GET_MODE(b) == (uint8_t)CtrlMode::BOOST)
#define IS_MODE_ADV(b) (GET_MODE(b) == (uint8_t)CtrlMode::ADV)
#define IS_MODE_BOOSTADV(b) (IS_MODE_BOOST(b) || IS_MODE_ADV(b))

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

#define SETTINGS_TAB -1

extern void systemInit();

extern settings_struct settings;
extern volatile sensor_struct sensors[MAX_SENSORS];

extern volatile int16_t currSnsrVal[MAX_SENSORS];   // last sensor value read for each sensor
extern uint8_t tmrSnsrState[MAX_CONTROLS][(MAX_TIMERS + 7 ) >> 3]; // one bit for each timer indicating last on/off state (used to apply hysteresis)
extern volatile uint16_t ctrlOnMode[MAX_CONTROLS];

extern volatile uint8_t ctrlCount;  // # of control_xx.bin files found
extern volatile uint8_t snsrCount;  // # of sensors found

extern size_t loadConfig(String name, void * buffer, uint16_t size, uint16_t offset);
extern bool saveConfig(String name, void * buffer, uint16_t size, uint16_t offset);
extern const char *get_ntpSupport_DefaultTZ();

extern void printTimer(uint8_t t, timer_struct * timer);
extern void printControlHdr(int8_t c, header_struct * buffer);
extern void printStat(int8_t(s));
extern void printSettings();

#include "timerControl.h"
#include "webPages.h"
#include "tzPosix.h"
#include "wifiConfig.h"

// sense checks
#if (MAX_TIMERS < 2) || (MAX_TIMERS > 126)
  #error "Must have at least 2 and no more than 126 timers per control."
#endif


#endif
