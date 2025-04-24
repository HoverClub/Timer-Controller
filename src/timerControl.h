#ifndef _NTP_H
#define _NTP_H
/*   
   nptSupport.h
   by Matthew Ford,  2021/12/06
   (c)2021 Forward Computing and Control Pty. Ltd.
   NSW, Australia  www.forward.com.au
   This code may be freely used for both private and commerical use.
   Provide this copyright is maintained.
*/

#include <Arduino.h>
#include "ctrlDefs.h"

const char timerConfigFileName[] = "/timerCfg.bin";  // binary file

timerConfig_struct* loadTimerConfig();
void initializeSNTP(); // initializes and starts SNTP server, stops it after first update
String getTZstr(); // get the current tz string
void resetDefaultTZstr(); // reset tz to default one
void showTimeDebug();
int haveSNTP(); // returns 1 if have sntp response else 0
String getCurrentTime_hhmm(); // returns local time as hh:mm
String getUTCTime(); // returns UTC time as hh:mm:ss
uint32_t getLocalTime_s(); // local time HH:MM:ss in sec
String getTZvalue(); // the current tz value
String getTZstr();

bool missedSNTPupdate();
unsigned int getLocalTime_mins();
bool saveTimerConfig(timerConfig_struct& timerConfig);
void setTZfromPOSIXstr(const char* tz_str); // sets flag to save config
bool saveConfigIfNeeded(); // saves any TZ config changes returns true if save happened

void initOutputs();
void setOutputs();
void readTemps();

// control
bool isCtrlOn(int); // returns true if within any timer period  in this control is ON 
bool isOffSelected(int);
bool isOnSelected(int);
bool isAutoSelected(int);
bool isBoostAdv(int c);
void clrBoostAdv();
void setOff(int);
void setOn(int);
void setAuto(int);
void setBoost(int);
void setAdv(int);
void saveCtrlName(const int c, const char * name);
char * getCtrlName(int c);

// timer set
void setOnTime(int c, int t, int val);
void setOffTime(int c, int t, int val);
void setTemp(int c, int t, int val);
void setLess(int c, int t, char val);
void setStat(int c, int t, int val);

char * getCtrlName(int c);
int getOnTime_mins(int c, int t);
int getOffTime_mins(int c, int t);
int8_t getTemp(int c, int t);
int8_t getCurrTemp(int c, int t);
char getLess(int c, int t);
int8_t getStat(int c, int t);
char getSetting(int c);
void startRebootTimer();

#endif
