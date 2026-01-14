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

// ==========================

// File functions
bool initializeFS(); // returns false if fails
void listDir(const char * dirname); // list to debugOut
int getFileSize(String name);
bool truncateFile(uint8_t ctrl, uint8_t num_timers);

bool newConfig(String name, void * buffer, uint16_t size);
bool makeNewControl(int8_t ctrl);
size_t loadConfig(String name, void * buffer, uint16_t size, uint16_t offset = 0);
bool saveConfig(String name, void * buffer, uint16_t size, uint16_t offset = 0);
bool saveSensors();
bool loadTimer(uint8_t ctrl, int8_t timer, timer_struct * buffer);
bool saveTimer(int8_t ctrl, int8_t timer, timer_struct * buffer);
uint8_t loadHeader(int8_t ctrl, header_struct * buffer);
bool saveHeader(int8_t ctrl, header_struct * buffer);
bool deleteControl(int ctrl);

void initSettings();
void initSensors();
void initSensor(int s);
void initHeader(header_struct * hdr, int8_t ctrl);
void initTimer(timer_struct * tmr);

//=================
// time functions
void initializeSNTP(); // initializes and starts SNTP server, stops it after first update
void resetDefaultTZstr(); // reset tz to default one
int haveSNTP(); // returns 1 if have sntp response else 0
String getCurrentTime_hhmm(); // returns local time as hh:mm
String getUTCTime(); // returns UTC time as hh:mm:ss
uint32_t getLocalTime_s(); // local time HH:MM:ss in sec

bool missedSNTPupdate();
unsigned int getLocalTime_mins();
void setTZfromPOSIXstr(const char* tz_str); // sets flag to save config

//===================
// IO functions
void initCtrlIO(uint8_t p);
void initSnsrIO(uint8_t s);
void initAllSnsrIO();

void setOutputs();
void readTemps();

//===================
// controls
bool isCtrlOn(int); // returns true if within any timer period in this control is ON 

//===================
// utility
#ifdef ESP32
  void pollSntp();
#endif

void startRebootTimer();

#endif
