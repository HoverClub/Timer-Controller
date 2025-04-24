  /**
   webPages.cpp
   by Matthew Ford,  2021/12/06
   (c)2021 Forward Computing and Control Pty. Ltd.
   NSW, Australia  www.forward.com.au
   This code may be freely used for both private and commerical use.
   Provide this copyright is maintained.

*/
#include "webPages.h"
#include "LittleFSsupport.h"
#include "timerControl.h"
#include "DebugOut.h"
#include "tzPosix.h"
#include "wifiConfig.h"
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>

extern timerConfig_struct timerConfig;
extern stat_struct statPins[];

// normally DEBUG is commented out
//#define DEBUG
static Stream* debugPtr = NULL;  // local to this file

static AsyncWebServer server(80);
static void handleSetControl(AsyncWebServerRequest * request);
static void handleSetTime(AsyncWebServerRequest * request);

static void getJsonControl (AsyncResponseStream * request, int ctrl);
static void getJsonStatus(AsyncResponseStream * request);

static String correctedTZstr;  // empty if no problems
static String correctedTZstrDsc;
static String userInputTZstr;
static String setMsg;

static int8_t currCtrl = 0; // currently active control tab on web page (selected by user)


// only handles +v numbers
static void print2digits(String &result, uint num) {
  if (num < 10) {
    result += '0';
  }
  result += num;
}

String minToHH_mm(int mins_in) {
  String rtn;
  int hrs = mins_in / 60;
  int mins = mins_in - (hrs * 60);
  print2digits(rtn, hrs);
  rtn += ':';
  print2digits(rtn, mins);
  return rtn;
}

String getTZDstr() {
  String rtnString = "";
  if (correctedTZstrDsc.length()) {
    rtnString = correctedTZstrDsc;
    correctedTZstrDsc = ""; // finished  with this
  } else {
    rtnString = getTZstr();
  }
  String TZstr = rtnString;
  struct posix_tz_data_struct tzdata;
  posixTZDataFromStr(TZstr, tzdata);
  buildPOSIXdescription(tzdata, rtnString);
  rtnString.replace("\n", "<br>");
  return rtnString;
}


// convert 'hh:mm' to mins
// return 0-n as minutes OR -1 if conversion fails
static int convertHH_MMtoMins(const String &hh_mm) {
  int idx = hh_mm.lastIndexOf(":");
  if (hh_mm.length() < 3 || idx < 1) // make sure there is  '0:'!
    return -1;
  int hh = hh_mm.substring(0, idx).toInt();
  int mm = hh_mm.substring(idx + 1).toInt();

  if (hh < 0 || hh > 23 || mm < 0 || mm > 59)
    return -1;
  return (hh * 60) + mm;
}

/*
   process index.html template
  adding timezone data
*/
static String processor(const String& var) {
  String rtnString = "";

  // set current visible tab
  if (var == "C")
    rtnString = String(currCtrl); // default tab to open is control0

  else if (var == "MAX_CTRLS")
    rtnString = String(MAX_CTRLS);

  else if (var == "MAX_TIMERS")
    rtnString = String(MAX_TIMERS);

  else if (var == "MAX_WEB_TIMERS")
    rtnString = String(MAX_WEB_TIMERS);

  else if (var == "MAX_STATS")
    rtnString = String(MAX_STATS);

  else if (var == "STAT_TYPES") {
    for (int t = 0; t < MAX_STATS; t++) {
      if (t != 0) rtnString.concat(",");
      rtnString.concat(statPins[t].mplex == STAT_IS_DIGITAL ? "1" : "0");
    }
  }

  else if (var == "HAVE_SNTP")
    rtnString = String(haveSNTP());

  else if (var == "TI")
    rtnString = getCurrentTime_hhmm();

  else if (var == "TC") {
    if (userInputTZstr.length()) {
      rtnString = "Time Zone string has been cleaned up from<br>";
      rtnString += userInputTZstr;
      rtnString += "<br>to";
      userInputTZstr = ""; // finished  with this
    }
  }

  else if (var == "TZ") {
    if (setMsg.length()) {
      rtnString = correctedTZstr;
      correctedTZstr = ""; // finished  with this
    } else {
      rtnString = getTZstr();
    }
  }

  else if (var == "TD") {
    if (correctedTZstrDsc.length()) {
      rtnString = correctedTZstrDsc;
      correctedTZstrDsc = ""; // finished  with this
    } else {
      rtnString = getTZstr();
    }
    String TZstr = rtnString;
    struct posix_tz_data_struct tzdata;
    posixTZDataFromStr(TZstr, tzdata);
    buildPOSIXdescription(tzdata, rtnString);
    rtnString.replace("\n", "<br>");
  }
  
  else if (var == "TS") {
    rtnString = setMsg;
    setMsg = "";
  }

  else if (var == "TIME_CURRENT_HHMM") {
    rtnString =  String(getCurrentTime_hhmm());
  } 

  return rtnString;
}

void notFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not found");
}

// save upload config file
// should save to temp in case entire file doesn't get uploaded (unlikely!)
// atm if the wrong size or not written it will reset to default config
void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  
  if (!index) {
    // open the file on first call and store the file handle in the request object
    if (initializeFS())
      request->_tempFile = LittleFS.open(String(timerConfigFileName), "w");
  }

  if (len)
    request->_tempFile.write(data, len); // stream chunk to the file

  if (final) {
    request->_tempFile.close(); // upload done
    loadTimerConfig();          // reload new config into memory (if saved file was wrong size it will reset to defaults!!)
    request->redirect("/");     // reload index page to update web page data
  }
}

void startWebServer() {
#ifdef DEBUG
  debugPtr = getDebugOut();
#endif
  if (!initializeFS()) {
    if (debugPtr) {
      debugPtr->println("LittleFS failed to start");
    }
    return;
  }

/** 
 *--------------------------------------------------------------------  
 * NOTE: the response object MUST be created AND request->sent in the
 *       server.on handler or you will get memory leaks!
 *-------------------------------------------------------------------- 
*/
  // home page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {request->send(LittleFS, "/index.html", String(), false, processor);  });
  server.on("/index.html", HTTP_GET, [](AsyncWebServerRequest * request) {request->send(LittleFS, "/index.html", String(), false, processor);  });
  
  // returns JSON data for requested control tab
  // GET 'ctrl=' paramter specifed the ctrl# 
  server.on("/getControl", HTTP_GET, [](AsyncWebServerRequest * request) { \
    if (request->hasParam("ctrl")) { \
      int ctrl = request->getParam("ctrl")->value().toInt(); \
      if (ctrl >= 0 && ctrl < MAX_CTRLS) { \
        AsyncResponseStream *response = request->beginResponseStream("application/json"); \
        getJsonControl(response, ctrl); request->send(response); } \
    } else notFound(request); });
  // return JSON status for all ctrls (on or off) plus current time
  server.on("/getStatus", HTTP_GET, [](AsyncWebServerRequest * request) \
    { AsyncResponseStream *response = request->beginResponseStream("application/json"); \
      getJsonStatus(response); request->send(response); });
  // Load style.css file
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest * request) { request->send(LittleFS, "/style.css", "text/css"); });
  // Load script.css file
  server.on("/timer.js", HTTP_GET, [](AsyncWebServerRequest * request) { request->send(LittleFS, "/timer.js", "text/javascript"); });

  // get control/timer confirguration file
  server.on("/download", HTTP_GET, [](AsyncWebServerRequest * request) { request->send(LittleFS, String(timerConfigFileName), "application/octet-stream"); });
  // upload new control settings file
  server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *request) { request->send(200); }, handleUpload);
  
  // update control settings
  // GET parameters have new settings
  server.on("/setControl", HTTP_GET, [](AsyncWebServerRequest * request) { handleSetControl(request); });
  // handle time zone settings
  server.on("/setTime", HTTP_GET, [](AsyncWebServerRequest * request) { handleSetTime(request);});

  server.onNotFound(notFound);
  
  server.begin();
}

/* returns JSON control data for requested control tab
  NOTE the HTML IDs used in the elements of the control tabe MUST match the variable 
  names used in the JSON response!!!
  {
    "id": 0,
    "time": "54292",
    "state": false,
    "name": "Control ",
    "setting": "F",
    "timers": [
      {
        "id": 2,
        "onTime": 0,
        "offTime": 0,
        "temp": 20,
        "lessthan": "L"
      },
      {
        "id": 3,
        "onTime": 0,
        "offTime": 0,
        "temp": 20,
        "lessthan": "L",
        "stat": -1,
      }
    ]
  }
  GET param "ctrl" = control number
 
  Limited to 4K max. response!!! 
*/
void getJsonControl(AsyncResponseStream * response, int ctrl) {

  currCtrl = ctrl; // update to new ctrl

  response->print("{");
  response->printf("\"C\":%i,", ctrl);
  response->printf("\"T\":\"%i\",", getLocalTime_s());
  response->printf("\"I\":%i,", isCtrlOn(ctrl));
  response->printf("\"N\":\"%s\",", getCtrlName(ctrl));
  response->printf("\"A\":\"%c\",\"timers\":[null", getSetting(ctrl));
  for (int t = 1; t < MAX_TIMERS; t++) { // timers
    response->printf(",{\"O\":\"%s\",", minToHH_mm(getOnTime_mins(ctrl, t)).c_str());
    response->printf("\"F\":\"%s\",", minToHH_mm(getOffTime_mins(ctrl, t)).c_str());
    response->printf("\"T\":%i,", getTemp(ctrl, t));
    response->printf("\"L\":\"%c\",", getLess(ctrl, t));
    response->printf("\"S\":%i}", getStat(ctrl, t));
  }
  response->print("]}");
}

/* return a JSON format status message
   with control on/off state and current setting
   plus current time and current reading for each timer sensor
{
  T:<time>,
  {
  "C":[
      {"I": <on/off state>, "A":"<current setting O/F/A/B/V>"}, // ctrl 1
      {"I": <on/off state>, "A":"<current setting O/F/A/B/V>"}, // ctrl 2
       etc ...
  ],
  "V":[null, "1":<val>, "2":<val>,"3": < val>, etc.  ]
} */
void getJsonStatus(AsyncResponseStream * response) {
  response->print("{");
  response->printf("\"T\":\"%i\",\"C\":[", getLocalTime_s());
  
  for (int c = 0; c < MAX_CTRLS; c++) {
    response->print( c == 0 ? "" : ",");
    response->printf("{\"I\":%i,", isCtrlOn(c));
    response->printf( "\"A\":\"%c\"}", getSetting(c));
  }
  int c = currCtrl == -1 ? 0 : currCtrl; // default to ctrl0 for temps if we are viewing timezone
  response->print("],\"V\":[0");
  for (int t = 1; t < MAX_TIMERS; t++) {
    response->printf(",%i", getCurrTemp(c, t));
  }
  response->print("]}");
}

/*
  Set time and time zone
  also resets time zone
*/
static void handleSetTime(AsyncWebServerRequest * request) {
  currCtrl = -1;
  int params = request->params();
  String newTZ;
  for (int i = 0; i < params; i++) {
    AsyncWebParameter *p = request->getParam(i);
    if (p->name().equals("RESET")) {
        resetDefaultTZstr();
      break;
    }
    // not reset so set new time and/or tz
    if (strcmp(p->name().c_str(), "TZ_INPUT_STR") == 0) {
      userInputTZstr = "";
      correctedTZstr = "";
      correctedTZstrDsc = "";
      setMsg = "";
      if (debugPtr) {
        debugPtr->printf("Tz input string Value: %s\n", p->value().c_str());
      }
      String inputStr = p->value();
      inputStr.trim();
      String cleanStr = p->value();
      cleanUpPosixTZStr(cleanStr);
      if (inputStr != cleanStr) {
        userInputTZstr = inputStr;
        correctedTZstr = cleanStr;
        correctedTZstrDsc = cleanStr;
        setMsg = "Select the Set TZ String button again to set<br>this cleaned up time zone string";
      } else {
        newTZ = cleanStr;
        correctedTZstrDsc = cleanStr;
      }
      if (!setMsg.length()) {
        // set new tz
        setTZfromPOSIXstr(newTZ.c_str()); // cleans up and set save flag as well
      }
    }
    if (strcmp(p->name().c_str(), "TIME") == 0) {
      if (debugPtr)
        debugPtr->printf("Time Value: %s\n", p->value().c_str());
      int dayMins = convertHH_MMtoMins(p->value());
      if (dayMins != -1) {
        // get utc time
        time_t now = time(nullptr);
        struct tm* tmPtr = gmtime(&now);
        int utcDayMins = tmPtr->tm_hour * 60 + tmPtr->tm_min;
        int tzDiffMins = (utcDayMins - dayMins);
        if (debugPtr) {
          debugPtr->print("tzDiff :"); debugPtr->print(tzDiffMins); debugPtr->println();
        }
        // round to 5min
        int sign = 1;
        if (tzDiffMins < 0) {
          sign = -1;
        }
        int tzDiffMinRoundedUnsigned = (( tzDiffMins * sign * 60 + 150) / 300 * 300) / 60;
        if (debugPtr) {
          debugPtr->print("tzDiffRounded :"); debugPtr->print(tzDiffMinRoundedUnsigned); debugPtr->println();
        }
        int hr_offset = tzDiffMinRoundedUnsigned / 60;
        int min_offset = tzDiffMinRoundedUnsigned - hr_offset * 60;
        tzDiffMins = tzDiffMinRoundedUnsigned * sign;
        hr_offset *= sign;
        if (debugPtr) {
          debugPtr->print("offset "); debugPtr->print(hr_offset); debugPtr->print(':'); debugPtr->print(min_offset); debugPtr->println();
        }
        // offsets in range -12 < offset <= +12  i.e. -11:45 is the smallest offset and +12:00 is the largest
        //         mins in range -720 < offsetMin <= +720
        // e.g. LT (localTime) = 14:00,  UTC=04:00  tzoffset = +10:00
        //      LT = 08:00  UTC = 22:00  tzoffset =  -14 => <=-12 so add 24,  -14+24 = +10
        //      LT = 14:00  UTC = 22:00  tzoffset = -8:00
        //      LT = 20:00  UTC = 4:00   tzoffset = 16:00 => >12 so subtract 24,  16-24 = -8:00
        if (tzDiffMins <= -720) {
          tzDiffMins += (24 * 60);
        } else if (tzDiffMins > 720) {
          tzDiffMins -= (24 * 60);
        }
        sign = 1;
        if (tzDiffMins < 0) {
          sign = -1;
        }
        tzDiffMinRoundedUnsigned = tzDiffMins * sign;
        hr_offset = tzDiffMinRoundedUnsigned / 60;
        min_offset = tzDiffMinRoundedUnsigned - hr_offset * 60;
        hr_offset *= sign;
        if (debugPtr) {
          debugPtr->print("offset (+/-12)"); debugPtr->print(hr_offset); debugPtr->print(':'); debugPtr->print(min_offset); debugPtr->println();
        }
        setTZoffsetInMins(tzDiffMins); // update in min
      }
    }
  }
  request->redirect("/index.html");
}

/*
Saves settings returned from control form.

Control # is in hidden 'tab' parameter

Control actions:
  N = user-entered control name string

  A = F   = off
  A = N   = on
  A = A   = Auto
  A = B   = Boost 1hr
  A = V   = advance

Set timers & temps for control
  TxN    // on time
  TxF   // off time
  TxL   // lessthan
  TxT    // temp
  TxS    // stat to use
*/
static void handleSetControl(AsyncWebServerRequest * request) {
  // make sure we got a valid ctrl#!
  int ctrl;
  if (request->hasParam("C")) {
    ctrl = request->getParam("C")->value().toInt();
    if (ctrl >= 0 && ctrl < MAX_CTRLS) {
      currCtrl = ctrl; // update to new ctrl
      int params = request->params();
      
      for (int i = 0; i < params; i++) {
        AsyncWebParameter *p = request->getParam(i);

        // control name string
        if (p->name() == "N") {
          if (p->value().length()) {
            String decoded = urlDecode(p->value());
            decoded.trim();
            // only allow alpha numeric chars, spaces, underscore and dash
            for (uint8_t i = 0; i < decoded.length(); i++)
              if (!isalnum(decoded.charAt(i)) && decoded.charAt(i) != ' ' && decoded.charAt(i) != '_' && decoded.charAt(i) != '-')
                decoded.setCharAt(i, '_');
            saveCtrlName(ctrl, decoded.substring(0, MAX_NAME_LEN).c_str());
          }
        }

        // control on/off/auto setting
        else if (p->name().equals("A")) {
          if (p->value().length() == 1) {
            char action = p->value()[0];
            switch (action) {
              case 'F':
                setOff(ctrl);
                break;
              case 'N':
                setOn(ctrl);
                break;
              case 'A':
                setAuto(ctrl);
                break;
              case 'B': // on for 1hr - NOT saved to config!
                setBoost(ctrl);
                break;
              case 'V': // advance timer A once - NOT saved to config!
                setAdv(ctrl);
                break;
            }
          }
        }

        // get updated timer values
        else if ((p->name()[0] == 'T') && (p->name().length() == 3)) {  // TxN/:L/F/ etc.
          int timer = p->name()[1] - '0'; // timer # - user timers start at #1!
          if (timer && timer < MAX_TIMERS && p->value().length() > 0) {
            char id = p->name()[2]; // html timer variable code letter
            if      (id == 'O')     // ontime
              setOnTime(ctrl, timer, convertHH_MMtoMins(p->value())); // on time 
            else if (id == 'F')   // offtime
              setOffTime(ctrl, timer, convertHH_MMtoMins(p->value())); // off time
            else if (id == 'T')   // temperature
              setTemp(ctrl, timer, strtol(p->value().c_str(), NULL, DEC)); // handles negative numbers!
            else if (id == 'L')   // lessthan
              setLess(ctrl, timer, p->value()[0]);
            else if (id == 'S')   // stat #
              setStat(ctrl, timer, strtol(p->value().c_str(), NULL, DEC)); // handles negative numbe (-1 = no stat)!
          }
        }
      }
    }
  }
//  request->redirect("/index.html");
    request->send(200);
}
