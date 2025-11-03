
#include "main.h"
/**
 * STATS: pin# used to read input stats or switches
 * 
 * NOTE: MUST have same number of pins as stat_count above!!!
 * 
 * ESP8266:
 * 1. Only has one ADC (A0) - code supports multiplexing by grounding 
 *    each sensor in return using an output pin (defined in statPins.mplex)
 *    with all 2nd sensor wires common'ed with all other sensors to the 
 *    ADC input.
 * 2. IO availability is limited (also further limited by dev board type)
 *      Outputs: GPIO 4/5/12-14 OK
 *                 15/16 & 2 OK but mustn't be held low at startup (2 is 
 *                 onboard LED on some)
 *      Inputs:  GPIO 4/5/12-14 OK
 *               A0 is analog input
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
 * 1. Only ADC2 GPIO can be used for analog sensors.  OR use an external 
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
 *      ADC2 inputs GPIO0, 2, 4, 12-15, 25-27
 *      I2C interface on GPIO 21 9SDA) and 22 (SCL)
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
#ifdef ESP32
  #define STATPIN0 32 // ADC 2 only (ADC1 used by WiFi!)
  #define STATPIN1 25 | STAT_DIGITAL
  #define STATPIN2 26
  #define STATPIN3 27
#else
  #define STATPIN0 A0
  #define STATPIN1 15 | STAT_DIGITAL // MUST be pulled high at boot by stat !
  #define STATPIN2 0x13 | STAT_DIGITAL | STAT_EXPANDER // digital inp on MCP23017 expander #1, channel 3
#endif

const stat_struct statPins[] = {
#ifdef STATPIN0
  stat_struct{STATPIN0, "room1", !STAT_MPLEX, 3950, 1000, 3000} // thermistor input
#endif
#ifdef STATPIN1
  ,stat_struct{STATPIN1, "door2"} // digital input pin
#endif
#ifdef STATPIN2
  ,stat_struct{STATPIN2, "window7"}
#endif
#ifdef STATPIN3
  ,stat_struct{STATPIN3, ""}
#endif
// ... extend this to add more stat inputs ...
};
const int stat_count = sizeof(statPins) / sizeof(stat_struct);
StatState currentStatState[stat_count] = {STAT_OFF};

// not included in stat_Pins (which is const) - (statVals are rd/wr)
int16_t currStatVal[stat_count] = {0}; // current value for each stat in statPins

timerConfig_struct timerConfig; // contains all the control and timers data
