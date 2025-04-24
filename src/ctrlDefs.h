#ifndef _CTRL_H
#define _CTRL_H

#include <Arduino.h>

//==================================================================
//                COMPILE TIME MODIFIABLE SETTINGS
//==================================================================

/**
 * @brief Compile time options:
 * 
 * CONTROLS: Add more control outputs by increasing MAX_CTRLS
 *    NOTE: You also need to define more output pins (CTRLPINxx below)
 * 
 * 
 * TIMERS: Add more timers to each control by increasing MAX_TIMERS
 *    NOTE: You will need to add more timer blocks to index.html and 
 *       increase MAX_WEB_TIMERS
 * 
 * THERMOSTATS: Add more input sensors by increasing MAX_STATS
 *    NOTE: Define more stat pins STATPINnn) as required below
 * 
*/


#define MAX_CTRLS 10  // max # of controls enabled (limited by MAX_TABS below OR 127)
#define MAX_TIMERS 4 // max # of timers in each control (2-6)
                     // MUST be >= 2 - timer0 is reserved for boost & advance 
                     // - first user timer is timer1

//------------------------
// Web page interface constraints:
#define MAX_TABS 10 // maximum number of ctrl tabs across the web page (limited by 
                    // the web page design - a different design could expand this 
                    // to the control code limit of 127!  This value limits the 
                    // number of controls you can have (MAX_CTRLS)

#define MAX_WEB_TIMERS 4 // maximum timer html blocks currently included in index.html
                        // you can add more by modifying index.html to add additional 
                        // timer html blocks.
//------------------------

#define MAX_NAME_LEN 12 // max control or timer user-defined name length

// ADC temp probe pins
#define MAX_STATS 4 // maximum # of thermostat/digital inputs available - set to 0 if none

// how often (ms) to read an input sensor (delay between each sensor read)
#define TEMP_READ_PERIOD 200

// how often outputs are changed (mSec)
#define OUTPUT_CHANGE_PERIOD 2000

/**
 * CONTROLS: output pins driven by each control 
 *
 * NOTE: MUST have the same number of pins as MAX_CTRLS above!!
 * 
 * For ESP8266 usable output pins are: 5-7 & 12-14
 * 16 & 4 with care
 * 
 *      ESP8266 4x relay board 
 * Add jumpers to to connect IO12-14 & 16 to relays
 * IO15, IO2 (onboard LED) are output only, IO4/5 
 * used by OLED SCL/SDA, IO1/3 rx/tx for serial
 */
#define CTRLPIN0 16
#define CTRLPIN1 14
#define CTRLPIN2 12
#define CTRLPIN3 13
#define CTRLPIN4 PIN_NOT_AVAIL
//#define CTRLPIN5 PIN_NOT_AVAIL
//#define CTRLPIN7 PIN_NOT_AVAIL
//#define CTRLPIN7 PIN_NOT_AVAIL
//#define CTRLPIN8 PIN_NOT_AVAIL
//#define CTRLPIN9 PIN_NOT_AVAIL
// ...

/**
 * STATS: pin# used to read input stats or switches
 * 
 * NOTE: MUST have same number of pins as MAX_STATS above!!!
 * 
 * ESP8266 only has one ADC (A0) but you can multiplex it by grounding  
 * each sensor return using an output pin (with all other sensor wires
 * common'ed to ADC ). 
 * 
 */
#define STATPIN0 A0
#define STATPIN1 6
#define STATPIN2 2  // MUST be pulled high at boot by stat !
#define STATPIN3 15 // MUST be pulled low at boot
//#define STATPIN4 PIN_NOT_AVAIL
//#define STATPIN5 PIN_NOT_AVAIL
//#define STATPIN6 PIN_NOT_AVAIL
//#define STATPIN7 PIN_NOT_AVAIL
//#define STATPIN8 PIN_NOT_AVAIL
//#define STATPIN9 PIN_NOT_AVAIL
//#define STATPIN10 PIN_NOT_AVAIL
// ...

#define NO_STAT -1 // no stats used by a timer


//==================================================================
//        END OF COMPILE TIME MODIFIABLE SETTINGS
//==================================================================





//==================================================================
// sense checks

#if (MAX_TIMERS < 2) || MAX_TIMERS > (1 + MAX_WEB_TIMERS) || !MAX_CTRLS || (MAX_CTRLS > MAX_TABS || MAX_WEB_TIMERS == 0)
  #error "Must have 1 - 127 controls and 2 - 6 timers per control (timer0 is boost, timer1 is advance)"
#endif


//==================================================================

// temperature switch condition
enum tempSettingEnum {
  LESSTHAN = 'L',
  MORETHAN = 'M'
};

// control status
enum timerSettingEnum {
  OFF = 'F', 
  ON = 'N', 
  AUTO = 'A',
  BOOST = 'B',
  ADV = 'V'
};
const char allSettings[] = "FNABV"; // all timer settings values
const char allLessthanVals[] = "LM"; // html timer lessthan value range
const char allTimerNames[] = "NFTLS"; // html name="" for all timer html values (onTime, offTime, temp, lessthan, stat

#define PIN_NOT_AVAIL 0xFF  // if a control or stat pin# isn't valid

// output pins
struct pin_struct {
  uint8_t num = PIN_NOT_AVAIL;  // IO pin #
  bool active = true;  // true if active hi, false if active low
};

#define STAT_NOT_MPLEX 0xFF  // if not a multiplexed sensor
#define STAT_IS_DIGITAL 0x80 // if a digital input pin (not an ADC)
                             // if < 0x80 then it is a multiplexed pin

// stat/sensor input pins 
// uses Steinhart-Hart equation for thermistors
// 8266 MAX ADC voltage is 1V so MUST use a 30K+ pull up to 3v3!!
struct stat_struct {
  uint8_t pin = PIN_NOT_AVAIL;    // ADC pin to read
  uint8_t mplex = STAT_NOT_MPLEX; // if multiplexed, this is the output pin (0-127) that is gnd'ed to activate this sensor
                                  // if STAT_IS_DIGITAL then pin is a digital input pin polarity determined by lessthan setting
  uint16_t beta = 3950;           // B value for thermistor
  uint16_t resistance = 1000;     // thermistor resistance @ 25c div by 10 (1-65535ohm range)
  uint16_t puRes = 3000;          // resistance div by 10 of pull up resistor on the thermistor (assumes vRef is 3.3V)
};

struct timer_struct {
  int onTime = 0;           // in min
  int offTime = 0;          // in min
  int8_t temp = 0;          // trigger set signed temperature
  char lessthan = LESSTHAN; 
  int8_t stat = NO_STAT;    // thermostat #, -1 = disabled, 0-127 = active
};

// control output (controlled by two timers with boost, adv & manual override)
// boost/adv are not exposed to the user.
// ADD TIMERS: add timers to each control here (timerC, timerD, etc.) and modify getTimerPtr() to include them
// and add more timer sections to index.html
struct control_struct {
  char name[MAX_NAME_LEN + 1] = {0}; // +1 for null terminator
  timer_struct timer[MAX_TIMERS]; // array of timers for this control (timer0 = boost/advance)
  char setting = OFF;     //  F = OFF, O = ON, A = AUTO, B = boost, V = advance - default isOFF
  char prevState = OFF;   //  previous state restored after a boost or adv period
};

// controls 
struct timerConfig_struct {
  time_t utcTime; // sec since 1/1/1970  = if not yet set by SNTP or timezonedb.com
  char tzStr[50]; // POSIX tz eg AEST-10AEDT,M10.1.0,M4.1.0/3  if empty then skip setting tzStr and just use user set local time and sntp utc to calculate tz offset
  control_struct control[MAX_CTRLS];
};

#endif
