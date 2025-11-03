#ifndef _CTRL_H
#define _CTRL_H

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <BufferedOutput.h>
#include "PinFlasher.h"
#ifndef ESP32
#include <coredecls.h>                  // settimeofday_cb()
#endif
#include <millisDelay.h>
#include <time.h>                       // time() ctime()
#include <sys/time.h>                   // struct timeval
#ifdef ESP32
  #include "esp_sntp.h"
  #include <WiFi.h>
#else
  #include <sntp.h>                       // sntp_servermode_dhcp()
  #include <ESPAsyncTCP.h>
  #include <ESP8266WiFi.h>
  #include <WiFiClient.h>
  #include <Wire.h>
#endif
#include <IPAddress.h>
#include "limits.h"
#include <ESPAsyncWebServer.h>
#include <millisDelay.h>
#include <SafeString.h>
#include <Adafruit_GFX.h>
//#include <Adafruit_SSD1306.h> //0.96" OLED
#include <Adafruit_SH1106.h>  // 1.3" OLED
#include <SPI.h>
#include <qrcode.h>
#include <Adafruit_ADS1X15.h>
#include "MCP23017.h"

//==================================================================
//                COMPILE TIME MODIFIABLE SETTINGS
//==================================================================
/**
 * @brief Compile time options section
 * 
 * CONTROLS: Add more control outputs by adding output pins to the pins[] array.
 * 
 * TIMERS: Add more timers for each control by increasing MAX_TIMERS (min. 2, max. 126).
 * 
 * THERMOSTATS: Add more input sensors by adding them to the statPins[] array
 *    NOTE: Define more stat pins STATPINnn as required below
 * 
*/

#define CONTROLLER_NAME "My_Test"   // Unique [MAX_NAME_LEN] name for this controller version.
                                    // Change if you modify pins[]/stat_pins[] or change 
                                    // MAX_TIMERS. The name is displayed on the home 
                                    // web page (followed by " Controller") and used in the 
                                    // download config filename AND must match for upload.  
                                    // Alphanumeric or underscore chrs only - underscores 
                                    // are replaced by spaces on the home page.

#define MAX_TIMERS 5 // max # of timers in each control (2-126)
                     // MUST be > 1 as timer0 is reserved for boost/advance
                     // # of user timers is MAX_TIMERS - 1 
                     // first user-programmable timer is timer1

// how often (ms) to read an input sensor (
// delay between reading the same sensor is = stat_count x TEMP_READ_PERIOD
// should be no more than 0.5 x OUTPUT_CHANGE_PERIOD 
#define TEMP_READ_PERIOD 200

// how often outputs levels are changed (mSec) - long enough to avoid relay chatterring.
#define OUTPUT_CHANGE_PERIOD 2000

/** 
 * ON and BOOST/ADV modes
 * 
 * Uncomment none or only ONE of the options!
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
 * @brief Network WiFi settings
 *
*/

// Replace these with your local network's SSID and password if you are likely
// to only use this device on that network (the network config page won't show)
// To be able move the device between networks leave empty! They are used as 
// default if nothing valid has been saved in WiFi settings.
#define wifiSSID "" 
#define wifiPassword ""

// WiFi access point settings (config web page access)
#define wifiWebConfigPASSWORD ""
#define wifiWebConfigAP (CONTROLLER_NAME "_Controller")
#define LOCAL_IP (IPAddress(10, 1, 1, 1))
#define GATEWAY_IP (IPAddress(10, 1, 1, 1))
#define SUBNET_IP (IPAddress(255, 255, 255, 0))

//==================================================================
//        END OF COMPILE TIME MODIFIABLE SETTINGS
//==================================================================

#define MAX_NAME_LEN 12 // max control user-defined name length && SSID (
                        // must be less than 21 to coply with 32chr SSID length!
#define MAX_STAT_NAME_LEN 10 // max stat name length - if incresing then check html layout effect!

// Web page interface constraint:
#define MAX_TABS ctrl_count // maximum number of ctrl tabs across the web page (limited by 
                    // the web page design - a different design could expand this up to 
                    // 126!

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

// output pins
struct pin_struct {
  uint8_t num;  // IO pin #
  bool active;  // true if active hi, false if active low
};

// 8266 MAX ADC voltage is 1V so MUST use a 30K+ pull up to 3v3!!
// ESP32 uses 3,3V as reference
struct stat_struct {
  const uint8_t pinNum;       // input pin to read (b7 set = digital, b6 set  = expander b0-5 = expander id and channel)
  const char name[MAX_STAT_NAME_LEN + 1]; // +1 for null terminator         
  // remaining items are only for analog sensors
  const uint8_t  mplex;       // if b7 set then sensor is mplexed and b0-6 is the pin# to be gnd'ed to activate this sensor.
  const uint16_t beta;        // B value for thermistor - set to zero if no Steinhart eqn to be applied)
  const uint16_t resistance;  // thermistor resistance @ 25c div by 10 (1-65535ohm range)
  const uint16_t puRes;       // resistance div by 10 of pull up resistor on the thermistor (assumes vRef is 3.3V)
};

#define STAT_DIGITAL 0x80
#define STAT_EXPANDER 0x40
#define STAT_MPLEX 0x80

// mplex values in stat table
#define IS_STAT_DIGITAL(s) (statPins[s].pinNum >> 7)
#define IS_STAT_ANALOG(s) (!IS_STAT_DIGITAL(s))
#define IS_STAT_EXPANDER(s) (statPins[s].pinNum >> 6)
#define IS_STAT_MPLEX(s) (IS_STAT_ANALOG(s) && (statPins[s].mplex >> 7))
#define IS_STAT_STEINHART(s) (statPins[s].beta != 0)
#define GET_STAT_MPLEX_PIN(s) (statPins[s].mplex & ~(STAT_MPLEX))
#define GET_STAT_PIN(s) (statPins[s].pinNum & ~(STAT_DIGITAL | STAT_EXPANDER))

#define GET_STAT_EXP_ID(s) (GET_STAT_PIN(s) >> 4) // 0-3 ID
#define GET_STAT_EXP_CH(s) (statPins[s].pinNum & 0x0F)  // 0-15

//==================================================================

// temperature switch condition & hysteresis
#define LESSTHAN 0x80

#define IS_TMR_LESSTHAN(c, t) (timerConfig.controls[c].timers[t].lessHyst >> 7)
#define GET_TMR_HYST(c, t) (timerConfig.controls[c].timers[t].lessHyst & ~LESSTHAN)
#define GET_TMR_LESSTHAN(c, t) (timerConfig.controls[c].timers[t].lessHyst & LESSTHAN)

// control status
enum timerSettingEnum {
  OFF = 'F', 
  ON = 'N', 
  AUTO = 'A',
  BOOST = 'B',
  ADV = 'V'
};

// thermostat state for hysteresis
enum StatState {
  STAT_OFF,
  STAT_ON,
};

#define PIN_NOT_AVAIL 0xFF  // if a control or stat pin# isn't valid
#define NO_STAT 0xFF // no stats used by a timer (can't then use digital expander 3 channel 15!)

struct timer_struct {
  int onTime = 0;             // in min
  int offTime = 0;            // in min
  int16_t temp = 0;           // trigger set signed temperature
  uint8_t lessHyst = LESSTHAN;  // b7 set if lessthan, clr if morethan. b0-6 are hysteresis
  int8_t statIdx = NO_STAT;   // thermostat #, -1 = disabled, 0-127 = active
};

// controls definition
// with boost, adv (both share timer0) & manual override.
// boost/adv timer is not exposed to the user.
// if MAX_TIMERS incresead above 4 then add more timer sections to index.html!
struct control_struct {
  char name[MAX_NAME_LEN + 1];        // +1 for null terminator
  uint8_t pinNum = PIN_NOT_AVAIL;     // output pin#  (b7 set if activeLow, b6 set if on I2C expander), b0-5 pin# (b4-5 = expander ID, b0-3 channel#)
  char state = OFF;                   //  F = OFF, O = ON, A = AUTO, B = boost, V = advance - default is OFF
  char prevState = OFF;               //  previous state restored after a boost or adv period
  timer_struct timers[MAX_TIMERS];    // array of timers for this control (timer0 = boost/advance)
};

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

// Array of unique output pins - one per control!  b7 set if pin is activeLow
const uint8_t pins[] = {CTRLPIN0, CTRLPIN1, CTRLPIN2, CTRLPIN3};
const int ctrl_count = (126 < sizeof(pins)) ? 126 : sizeof(pins); // no more than 126


#define IS_CTRL_EXPANDER(c) (timerConfig.controls[c].pinNum & PIN_EXPANDER)
#define GET_CTRL_EXP_ID(c) ((timerConfig.controls[c].pinNum & 0x30) >> 4) // 0- 3
#define GET_CTRL_EXP_CH(c) (timerConfig.controls[c].pinNum & 0x0F)  // 0- 15

#define GET_CTRL_PIN_NUM(c) (timerConfig.controls[c].pinNum & ~(PIN_ACTIVELO | PIN_EXPANDER))
#define GET_CTRL_ON_LVL(c) ((timerConfig.controls[c].pinNum & PIN_ACTIVELO) ? LOW : HIGH) // pin active level

extern const stat_struct statPins[];
extern StatState currentStatState[];
extern const uint8_t pins[];
extern int16_t currStatVal[];
extern const int stat_count;
extern void setInitialtimerConfig();

struct timerConfig_struct {
  time_t utcTime; // sec since 1/1/1970  = if not yet set by SNTP or timezonedb.com
  char tzStr[50]; // POSIX tz eg AEST-10AEDT,M10.1.0,M4.1.0/3  if empty then skip setting tzStr and just use user set local time and sntp utc to calculate tz offset
  control_struct controls[ctrl_count];
};
extern timerConfig_struct timerConfig;

#include "LittleFSsupport.h"
#include "timerControl.h"
#include "webPages.h"
#include "tzPosix.h"
#include "wifiConfig.h"

// sense checks
#if (MAX_TIMERS < 2) || (MAX_TIMERS > 126)
  #error "Must have at least 2 and no more than 126 timers per control."
#endif


#endif
