  /**
  Handle web interface

   On index.html load,the onload function constructs the control timers, ctrl tab selecter and stat options on the page.
   Then fetch()'es /getControl to get the control/timer data for the current control.  Then fetch /getStatus to get
   the status of all controls (to update tab bar.)

   During operation, fetch /getStatus is used periodically to keep the current control (and tabs for all controls) on/off status updated 
   
   If user clicks a tab then /getControl fetches the new control/timer data and inserts it into the page.

   If the timer/control data is modified by the user then it POSTs to /setConfig to save the new settings to the filesystem

   Control # -1 is the settings data, controls 1 to maxCtrls is the currently displayed control.

   */
#include "main.h"
#include <vector>

static AsyncWebServer server(80);
static String minToHH_mm(int mins_in);
String minToDDD_HH_mm(uint32_t mins_in);

static void handleSetControl(AsyncWebServerRequest * request);
static void handleSetTime(AsyncWebServerRequest * request);

static void getJsonStatus(AsyncResponseStream * request, bool btns = false);

static String processor(const String& var);
void handleRestoreUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);

static String correctedTZstr;  // empty if no problems
static String correctedTZstrDsc;
static String userInputTZstr;
static String setMsg;

void startWebServer() {
  /** 
   *--------------------------------------------------------------------  
  * NOTE: the response object MUST be created AND request->sent in the
  *       server.on handler or you will get memory leaks!
  *-------------------------------------------------------------------- 
  */
  // home page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {request->send(SPIFFS, "/index.html", String(), false, processor);  });
  server.on("/index.html", HTTP_GET, [](AsyncWebServerRequest * request) {request->send(SPIFFS, "/index.html", String(), false, processor);  });

/*  getControl can return up to 20K of JSON data so uses chunked response to reduce buffer requirements (response printf's accumulate
    and needs a buffer for the entire response!  For 8266 it takes ~150mSecs to transfer a 126 timer/50 sensor Control.     */
 server.on("/getControl", HTTP_POST, [](AsyncWebServerRequest *request) { 
    int ctrl = 0;
    if (request->hasParam("C", true)) ctrl = request->getParam("C", true)->value().toInt(); 
//debugf("Ctrl: %i\n", ctrl);
    if (ctrl < 0 || ctrl >= ctrlCount) {
      request->send(404, "text/plain", "File not found"); // invalid ctrl# or no file
    } else { 
      AsyncWebServerResponse *response = request->beginChunkedResponse("application/json", [ctrl, request](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
        static bool hdr_sent;
        static uint8_t tmr_idx; // current timer#
        static uint8_t tmrCount = 0;
        static uint8_t snsr_idx; // current sensor #
static uint32_t timeChk;

        if (index == 0) { // First call for this request, reset state
          hdr_sent = false; // true if ctrl header has been sent
          tmr_idx = 1; // starts at timer1 - timer0 isn't sent
          snsr_idx = 0;
timeChk = millis(); // start timer
        }
//debugf("Ctrl: %i maxLen: %i index: %i\n", ctrl, maxLen, index);

        if (!hdr_sent) { // send ctrl data first
//debugf("Hdr: %i\n", ctrl);
          header_struct hdrBuffer; // local copy of hdr data
          tmrCount = loadHeader(ctrl, &hdrBuffer); 
          if (tmrCount == 0) return 0; // abort if can't load header data
//printControlHdr(ctrl, &hdrBuffer);
          uint8_t pin = hdrBuffer.pinNum;
          const char * tp = "{\"C\":%i,\"I\":%i,\"SN\":\"%s\",\"A\":%i,\"SP\":%i,\"SX\":%i,\"SL\":%i,\"ST\":%i, \"timers\":[null";
          uint8_t len = snprintf(NULL, 0, tp, // get max formatted length
                          ctrl, (ctrlOnMode[ctrl] & 0x8000) >> 15, hdrBuffer.name, hdrBuffer.modes & 0x0F,
                          IS_PIN_AVAIL(pin) ? GET_PIN_NUM(pin) : -1,
                          IS_PIN_AVAIL(pin) ? GET_PIN_EXP_ID(pin) : -1,
                          IS_PIN_ACTIVELO(pin),
                          tmrCount);
          if (maxLen < len) return RESPONSE_TRY_AGAIN; // not enough space for hdr
          len = snprintf((char *)buffer, len + 1, tp,
                          ctrl, (ctrlOnMode[ctrl] & 0x8000) >> 15, hdrBuffer.name, hdrBuffer.modes & 0x0F,
                          IS_PIN_AVAIL(pin) ? GET_PIN_NUM(pin) : -1,
                          IS_PIN_AVAIL(pin) ? GET_PIN_EXP_ID(pin) : -1,
                          IS_PIN_ACTIVELO(pin),
                          tmrCount);
          hdr_sent = true;
          return len >= 0 ? len : 0;
        }

        if (tmr_idx == tmrCount) {
          if (snsr_idx == snsrCount) {
//debugf("ctrl %i timeChk: %lu\n", ctrl, millis() - timeChk);

            return 0; // all done
          } else {
            /* max len - can't use sprintf to get length as we don't know which entry has largest numeric values!
            ,{"XN":"12345678901","XT":1,"XX":12,"XM":126,"XP":126,"XB":16384,"XR":65535,"XU":65535}]}       */
            const uint8_t max_len = 92;
            uint8_t snsr_blks = (int16_t)maxLen / max_len; // # of snsr blocks we can fit into allocated buffer size
            if (snsr_blks < 1)
              return RESPONSE_TRY_AGAIN;
            else {
              uint16_t len = 0;
              // send as many sensors as will fit in buffer
              uint8_t max_blk = (snsr_idx + snsr_blks) > snsrCount ? snsrCount : (snsr_idx + snsr_blks);
              for (uint8_t snsr = snsr_idx; snsr < max_blk; snsr++) {
                len += snprintf((char *)(buffer + len), max_len, "%c{\"XN\":\"%s\",\"XT\":%i,\"XX\":%i,\"XM\":%i,\"XP\":%i,\"XB\":%i,\"XR\":%i,\"XU\":%i}%s",
                  snsr_idx == 0 ? ' ' : ',',
                  sensors[snsr_idx].name, IS_STAT_DIGITAL(snsr_idx) ? 1 : 0,
                  sensors[snsr_idx].pinNum == PIN_NOT_AVAIL ? -1 : (IS_STAT_EXPANDER(snsr_idx) ? GET_STAT_EXP_ID(snsr_idx) : -1),
                  IS_STAT_MPLEX(snsr_idx) ? GET_STAT_MPLEX_PIN(snsr_idx) : -1,
                  sensors[snsr_idx].pinNum == PIN_NOT_AVAIL ? -1 : GET_STAT_PIN(snsr_idx),
                  sensors[snsr_idx].beta, sensors[snsr_idx].resistance, sensors[snsr_idx].puRes,
                  snsr_idx == (snsrCount-1) ? "]}" : ""
                );
                snsr_idx++;
              }
              return len >= 0 ? len : 0;
            }
          }
        } else {
          // send timer data
          /* max len - can't use sprintf to get length as we don't know which entry has largest numeric values!
          ,{"O":"00:00","P":"000:00:00","T":-127,"L":1,"H":16383,"S":126,"W":255,"D":4294967295 ,"M":255}]."sensors":[   */
          const uint8_t max_len = 110;
          int8_t tmr_blks = (int16_t)maxLen / max_len; // # of timer blocks we can fit into allocated buffer size
          if (tmr_blks < 0)
            return RESPONSE_TRY_AGAIN;
          else {
            uint16_t len = 0; // send as many timers as will fit in buffer
            int8_t max_tmr = (tmr_idx + tmr_blks) > tmrCount ? tmrCount : (tmr_idx + tmr_blks); // ... or until no more timers
            timer_struct tmrBuffer;
            while (tmr_idx < max_tmr) {          
              if (!loadTimer(ctrl, tmr_idx, &tmrBuffer)) return 0; // failed so bail with no (or partial) data sent
              String onTimeStr = minToHH_mm(tmrBuffer.onTime);
              String durationStr = minToDDD_HH_mm(tmrBuffer.duration);
              len += snprintf((char *)(buffer + len), max_len, ",{\"O\":\"%s\",\"P\":\"%s\",\"T\":%i,\"L\":%i,\"H\":%i,\"S\":%i,\"W\":%i,\"D\":%i,\"M\":%i}%s",
                      onTimeStr.c_str(), durationStr.c_str(), tmrBuffer.temp,
                      GET_TMR_LESSTHAN(tmrBuffer), GET_TMR_HYST(tmrBuffer), tmrBuffer.statIdx,
                      tmrBuffer.onWeekDay, tmrBuffer.onDate, tmrBuffer.onMonth,
                      tmr_idx == (tmrCount - 1) ? (snsrCount == 0 ? "],\"sensors\":[]}" : "],\"sensors\":[") : "");
              tmr_idx++;
            }
            return len;
          }
        }
      });
    request->send(response);
    }
  });

  // send status for all controls with optional ctrl names (for tab update on page)
  server.on("/getStatus", HTTP_GET, [](AsyncWebServerRequest * request) \
    { AsyncResponseStream *response = request->beginResponseStream("application/json"); \
      getJsonStatus(response, request->hasParam("btns")); 
      request->send(response); 
    });
  
  // Load style.css file
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest * request) { request->send(SPIFFS, "/style.css", "text/css"); });
  // Load script.css file
  server.on("/timer.js", HTTP_GET, [](AsyncWebServerRequest * request) { request->send(SPIFFS, "/timer.js", "text/javascript", false, processor); });

  // update control settings incl timers & sensors
  server.on("/setConfig", HTTP_POST, [](AsyncWebServerRequest * request) { handleSetControl(request); });
  // server.on("/timer", HTTP_POST, [](AsyncWebServerRequest * request) { handleTimerUpdate(request); });
  // handle time zone settings
  server.on("/setTime", HTTP_GET, [](AsyncWebServerRequest * request) { handleSetTime(request);});

  // save/load control/timer configuration
  server.on("/restore-config", HTTP_POST, [](AsyncWebServerRequest *request){}, handleRestoreUpload);

  // download a single file containg all the .bin config files with a header for each one 
  // containing the filename and size. This locks the filesystem for the duration to prevent other requests
  // modifying a file while its being downloaded.
  // Use a shared pointer to track if the mutex is taken for this download.
  // This is captured by the onDisconnect lambda should the browser client disconnect part way through a download
  server.on("/download-all", HTTP_GET, [](AsyncWebServerRequest *request) { 
    auto mutexTaken = std::make_shared<bool>(false);
    AsyncWebServerResponse *response = request->beginChunkedResponse("application/octet-stream", [request, mutexTaken](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
      static std::vector<String> files;
      static size_t file_idx;
      static File currentFile;
      static bool header_sent;

debugf("------ downLd maxLen: %i index %i \n", maxLen, index);

      if (index == 0) { // First call for this request, reset state
#ifdef ESP32
        if (xSemaphoreTake(FSmutex, (1000 / portTICK_PERIOD_MS)) != pdTRUE) {
          // Could not get mutex, just return 0 to close.
          return 0; 
        }
#else
        if (!GetMutex(FSmutex)) return RESPONSE_TRY_AGAIN;
#endif
        *mutexTaken = true;

        request->onDisconnect([=]() {
          if (*mutexTaken) {
#ifdef ESP32
            xSemaphoreGive(FSmutex);
#else
            ReleaseMutex(FSmutex);
#endif
            *mutexTaken = false;
            if(currentFile) currentFile.close();
            debugln("Download-all aborted, FSmutex released.");
          }
        });
        files.clear();
        files.push_back(String("/settings.bin"));
        files.push_back(String("/sensors.bin"));
        for (int i = 0; i < ctrlCount; i++) {
          files.push_back("/control_" + String(i) + ".bin");
        }
        file_idx = 0;
        if (currentFile) currentFile.close();
        header_sent = false;
      }

nxtFile: 
      if (!currentFile) {
        if (file_idx >= files.size()) {
#ifdef ESP32
          if (*mutexTaken) xSemaphoreGive(FSmutex);
#else
          if (*mutexTaken) ReleaseMutex(FSmutex);
#endif
          *mutexTaken = false;
          return 0; // All files sent
        }
        
        String path = files[file_idx];
        if (SPIFFS.exists(path)) {
            currentFile = SPIFFS.open(path, "r");
            if(currentFile) {
              header_sent = false;
debugf("open: %s\n", path.c_str());
            } else {
              // File could not be opened, try next
              file_idx++;
              return RESPONSE_TRY_AGAIN; // return non-zero to be called again
            }
        } else {
            // File does not exist, try next
            file_idx++;
            return RESPONSE_TRY_AGAIN; // return non-zero to be called again
        }
      }

      if (!header_sent) {
        String filename = files[file_idx].c_str(); // usave full file path
        uint8_t filename_len = files[file_idx].length();
        uint32_t filesize = currentFile.size();
        
        if (maxLen < (size_t)(1 + filename_len + 2)) {
          return RESPONSE_TRY_AGAIN; // Not enough space in buffer, client needs to provide larger buffer.
        }

        size_t offset = 0;
        buffer[offset++] = filename_len;
        memcpy(buffer + offset, filename.c_str(), filename_len);
        offset += filename_len;
        buffer[offset++] = (filesize >> 8) & 0xFF;
        buffer[offset++] = filesize & 0xFF;
        
        header_sent = true;
        return offset;
      }
      
      size_t bytes_read = currentFile.read(buffer, maxLen);
      if (bytes_read > 0) {
debugf("read: %i\n", bytes_read);
        return bytes_read;
      } else {
debugf("close: %s\n", currentFile.name());
        currentFile.close();
        file_idx++;
debugf("file idx: %u files.size %i\n", file_idx, files.size());
        goto nxtFile; // jump to open nxt file & send header (or exit 0 if this was last file)
                      // messy but works without sending a RESPONSE_TRY_AGAIN - which seems to 
                      // stop the client requesting more data?) 
      }
    });
    response->addHeader("Content-Disposition", "attachment; filename=\"config_backup.dat\"");
    request->send(response);
  });

  server.onNotFound( [](AsyncWebServerRequest *request) { request->send(404, "text/plain", "File not found"); });

  server.begin();
}

String minToHH_mm(int mins_in) {
  char rtn[5 + 1];
  uint8_t hrs = mins_in / 60;
  uint8_t mins = mins_in - (hrs * 60);
  snprintf(rtn, sizeof(rtn), "%.2d:%.2d", hrs, mins);
  return String(rtn);
}

String minToDDD_HH_mm(uint32_t mins_in) {
  char rtn[9 + 1];
  uint16_t d = mins_in / (24 * 60);
  uint8_t h = (mins_in / 60) % 24;
  uint8_t m = mins_in % 60;
  snprintf(rtn, sizeof(rtn), "%.3d:%.2d:%.2d", d, h, m);
  return String(rtn);
}

String getTZDstr() {
  String rtnString = "";
  if (correctedTZstrDsc.length()) {
    rtnString = correctedTZstrDsc;
    correctedTZstrDsc = ""; // finished  with this
  } else {
    rtnString = settings.tzStr;
  }
  String TZstr = rtnString;
  struct posix_tz_data_struct tzdata;
  posixTZDataFromStr(TZstr, tzdata);
  buildPOSIXdescription(tzdata, rtnString);
  rtnString.replace("\n", "<br>");
  return rtnString;
}


// convert 'hh:mm' to mins
// return minutes OR 0 if conversion fails
static int convertHH_MMtoMins(const String &hh_mm) {
  int idx = hh_mm.lastIndexOf(":");
  if (hh_mm.length() < 3 || idx < 1) // make sure there is  '0:'!
    return 0;
  int hh = hh_mm.substring(0, idx).toInt();
  int mm = hh_mm.substring(idx + 1).toInt();

  if (hh < 0 || hh > 23 || mm < 0 || mm > 59)
    return 0;
  return (hh * 60) + mm;
}

// convert 'DDD:HH:MM' to mins
// returns minutes OR 0 if conversion fails
static uint32_t convertDDD_HH_MMtoMins(const String &ddd_hh_mm) {
  int idx1 = ddd_hh_mm.indexOf(":");
  int idx2 = ddd_hh_mm.lastIndexOf(":");
  if (idx1 < 1 || idx1 == idx2) // must have at least D:H:M
    return 0;

  int ddd = ddd_hh_mm.substring(0, idx1).toInt();
  int hh = ddd_hh_mm.substring(idx1 + 1, idx2).toInt();
  int mm = ddd_hh_mm.substring(idx2 + 1).toInt();

  if (ddd < 0 || ddd > 999 || hh < 0 || hh > 23 || mm < 0 || mm > 59)
    return 0;

  return (ddd * 24 * 60) + (hh * 60) + mm;
}


/*
   process index.html template
  adding timezone data
*/
static String processor(const String& var) {
  String rtnString = "";

  // set current visible tab
  if (var == "C")
    rtnString = "0"; // default tab to open is control0

  else if (var == "MAX_CONTROLS")
    rtnString = String(MAX_CONTROLS);

  else if (var == "MAX_TIMERS")
    rtnString = String(MAX_TIMERS);

  else if (var == "CTRL_COUNT")
    rtnString = String(ctrlCount);

  else if (var == "HAVE_SNTP")
    rtnString = String(!missedSNTPupdate()); // false if mised an sntp update

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
      rtnString = String(settings.tzStr);
    }
  }

  else if (var == "TD") {
    if (correctedTZstrDsc.length()) {
      rtnString = correctedTZstrDsc;
      correctedTZstrDsc = ""; // finished  with this
    } else {
      rtnString = String(settings.tzStr);
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

  else if (var == "NAME") {
    rtnString =  String(settings.name);
  }

  else if (var == "PIN_HELP") {
    #ifdef ESP32
      rtnString = "ESP32 Pins:<br>Outputs: 2,4,5,12-33<br>Inputs: 2,4,5,12-39<br>ADC: 32-39<br><br>Expander:<br>ADC pins 0-3<br>Digital pins 0-15";
    #else
      rtnString = "ESP8266 Pins:<br>Outputs: 5,12-14,16<br>Inputs: 4,5,12-14<br>ADC: A0(17)<br><br>Expander:<br>ADC pins 0-3<br>Digital pins 0-15";
    #endif
  }

  return rtnString;
}

static bool isValidPin(int pin, bool isOutput, bool isDigital) {
  if (pin == -1) return true; 
  #ifdef ESP32
    if (isOutput) { // 2,4,5,12-33
      if (pin == 2 || pin == 4 || pin == 5) return true;
      if (pin >= 12 && pin <= 33) return true;
    } else if (isDigital) { // 2,4,5,12-39
      if (pin == 2 || pin == 4 || pin == 5) return true;
      if (pin >= 12 && pin <= 39) return true;
    } else { // ADC 32-39
      if (pin >= 32 && pin <= 39) return true;
    }
  #else // ESP8266
    if (isOutput) { // 5,12-14,16
      if (pin == 5 || pin == 16) return true;
      if (pin >= 12 && pin <= 14) return true;
    } else if (isDigital) { // 4,5,12-14
      if (pin == 4 || pin == 5) return true;
      if (pin >= 12 && pin <= 14) return true;
    } else { // ADC 17
      if (pin == 17) return true;
    }
  #endif
  return false;
}

void notFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "File not found");
}

// upload one file to SPIFFS then split into config.bin files
// max individual filesize is 65K.  Mutex is applied when tmpFile is
// split and the config files re-written to avoid changes by other Async functions
// tmpFile writes aren't ptrotected as nothing else acesses that file!
void handleRestoreUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  // Use a shared pointer to track if the mutex is taken.
  // This is captured by the onDisconnect lambda.
  auto mutexTaken = std::make_shared<bool>(false);
  const char* tmpFile = "/restore.tmp";

  if (index == 0) { 
    // Open new temp file for writing, truncating if it exists
    request->_tempFile = SPIFFS.open(tmpFile, "w"); 

    // Lambda function to ensure mutex is released if client disconnects
    request->onDisconnect([mutexTaken, request, tmpFile]() {
      if (*mutexTaken) {
#ifdef ESP32
        xSemaphoreGive(FSmutex);
#else
        ReleaseMutex(FSmutex);
#endif
        *mutexTaken = false;
      }
      if(request->_tempFile) {
        request->_tempFile.close();
        SPIFFS.remove(tmpFile);
      }
      debugln("Restore upload finished, temp file closed and removed.");
    });
debugln("tmp file open wr");
  }

  if (len) {
    request->_tempFile.write(data, len);
debugf("tmp file write %i\n", len);
  }

  if (final) {
    request->_tempFile.close(); // close for writing
debugf("tmp file close %i\n", final);
    // get mutex as we are going to modify config files!
#ifdef ESP32
    if (xSemaphoreTake(FSmutex,(1000 / portTICK_PERIOD_MS)) != pdTRUE) { // couldn't get file access for 1sec
#else
    uint32_t mTimer = millis(); while (!GetMutex(FSmutex)) if ((millis() - mTimer) > 1000) { delay(1);
#endif
      request->send(503, "text/plain", "Service Unavailable");
      return;
    }
    *mutexTaken = true;
    
    // Now process the temp file
    File restoreFile = SPIFFS.open(tmpFile, "r");
    if (restoreFile) {
debugln("tmp file open rd");
      struct __attribute__((packed)) file_header {
        uint8_t filename_len;
        char filename[31]; // Max possible SPIFFS filename length (includes '/' and a terminator!)
        uint8_t size_buf[2];
      };

      while(restoreFile.available()) {
        file_header header;
        if (!restoreFile.read(&header.filename_len, 1)) break; // get filename length
        if (header.filename_len > 30) header.filename_len = 30; // truncate if too long to avoid namebuffer overflow!
        
        // saved filename always includes the '/' path chr
        if (restoreFile.read((uint8_t*)header.filename, header.filename_len) != header.filename_len) break; // get filename
        header.filename[header.filename_len] = 0; // add terminator just in case!
        if (restoreFile.read(header.size_buf, 2) != 2) break;   // file size (max 64K!)
        uint16_t filesize = (header.size_buf[0] << 8) | header.size_buf[1];
debugf("wr filename %s filesize %u\n", header.filename, filesize);
        
        // Open target file for writing
        File targetFile = SPIFFS.open(header.filename, "w");
        if (targetFile) {
          uint8_t buffer[256];  // most files will take one pass with this size buffer
                                // settings file is small - control_xx file with 12 timers - 13 sensors
          uint32_t remaining = filesize;
          while(remaining > 0) {
            size_t to_read = (remaining > sizeof(buffer)) ? sizeof(buffer) : remaining;
            size_t read_bytes = restoreFile.read(buffer, to_read);
            if (read_bytes == 0) break; // Should not happen
            targetFile.write(buffer, read_bytes);
            remaining -= read_bytes;
          }
          targetFile.close();
        }
      }
    }
    restoreFile.close();
    SPIFFS.remove(tmpFile);
#ifdef ESP32
    if (*mutexTaken) xSemaphoreGive(FSmutex);
#else
    if (*mutexTaken) ReleaseMutex(FSmutex);
#endif
    *mutexTaken = false;

//debugln("cfgs reloaded");
    // reload settings, sensors and contrtol0 files
    // init snsrIO ctrlIO
    systemInit(); // reload/init filesystem, config, hardware, etc.
  }
  request->redirect("/index.html"); // redirect to home page to reset frontend
}

/* returns a JSON status message via URL /getStatus
  NOTE- response could be up to 5.2K (if names included) and take 120mS if MAX_CTRLS & MAX_SENSORS!
  { "T":"56712", // time
  "W": sntpStatus, // true if WiFi active
  "C":[         
    {"I":0,"A":""}, // ctrl0 state, mode & optional name 
    {"I":0,"A":""}, // ctrl1 state ...
    {"I":0,"A":""}, // ctrl2...
    {"I":0,"A":""}
  ],
  "V":[0,0,0,0,0]   // sensor 0-nn current values
} */
void getJsonStatus(AsyncResponseStream * response, bool btns /*= false*/) {
// ESP32 compiler requires specific types for printf!!!
//debugf("\"T\":\"%llu\",\"W\":\"%i\"\n", time(nullptr) / 1000ULL, (int)1); // time in secs!
uint32_t handlerTimer = millis();
 struct tm timeinfo;
  time_t now = time(nullptr);
  localtime_r(&now, &timeinfo); // Use thread-safe localtime_r
  char time[6];
  strftime(time, sizeof(time), "%H:%M", &timeinfo);
  char date[11];
  strftime(date, sizeof(date), "%a %d %b", &timeinfo);
  response->printf("{\"T\":\"%s\",\"D\":\"%s\",\"W\":%i,\"C\":[", time, date, (int)(missedSNTPupdate() ? 0 : 1)); // time in secs!
  
  for (int c = 0; c < ctrlCount; c++) {
    if (c > 0) response->print(",");
    char name[MAX_NAME_LEN + 1];
    response->printf("{\"I\":%i,", (ctrlOnMode[c] & 0x8000) >> 15); // curr state on/off
    if (btns) {
      // Read names from control headers.  Only read MAX_NAME_LEN to leave room for null terminator
      if (loadConfig(String("/control_") + String(c) + ".bin", &name,  MAX_NAME_LEN, 0) == 0) {
        snprintf(name, sizeof(name), "Control %i", c); // use default name if nothing read
      }
      response->printf("\"SN\":\"%s\",", name);
    }
    response->printf( "\"A\":%i}", ctrlOnMode[c] & 0x0F); // mode
  }
  // current value for every sensor
  response->print("],\"V\":[");
  for (int s = 0; s < snsrCount; s++) {
    response->printf(s == 0 ? "%i" : ",%i", currSnsrVal[s]);
  }
  response->print("]}");
//debugf("-- statReqTime:%lu\n", millis() - handlerTimer); // time to handle request
}

/*  Set time and time zone
  also resets time zone
*/
static void handleSetTime(AsyncWebServerRequest * request) {
  int params = request->params();
  String newTZ;
  for (int i = 0; i < params; i++) {
    const AsyncWebParameter *p = request->getParam(i);
    if (p->name().equals("RESET")) {
        resetDefaultTZstr();
      break;
    }
    // not reset so set new time and/or tz
    if (p->name().equals("TZ_INPUT_STR")) {
      userInputTZstr = "";
      correctedTZstr = "";
      correctedTZstrDsc = "";
      setMsg = "";
debugf("Tz input string Value: %s\n", p->value().c_str());
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
    if (p->name().equals("TIME")) {
      debugf("Time Value: %s\n", p->value().c_str());
      int dayMins = convertHH_MMtoMins(p->value());
      if (dayMins != -1) {
        // get utc time
        time_t now_utc = time(nullptr);
        struct tm *tm_utc = gmtime(&now_utc);

        // Create a tm struct for the user-provided local time today
        struct tm tm_local = *tm_utc; // Copy year, month, day from UTC
        tm_local.tm_hour = dayMins / 60;
        tm_local.tm_min = dayMins % 60;
        tm_local.tm_sec = 0;
        tm_local.tm_isdst = -1; // Let mktime figure out DST

        time_t now_local_assumed_utc = mktime(&tm_local);
        long offset_seconds = difftime(now_utc, now_local_assumed_utc);

        // POSIX TZ offset is inverted from GMT offset.
        setTZoffsetInMins(-offset_seconds / 60); // update in min
      }
    }
  }
  saveConfig("/settings.bin", &settings, sizeof(settings_struct), 0);

  request->redirect("/index.html");
}

static String handleSensorUpdate(AsyncWebServerRequest* request);
static void handleControlAction(AsyncWebServerRequest* request, int ctrl);
static void handleControlAddDel(AsyncWebServerRequest* request);
static String handleControlSettingsUpdate(AsyncWebServerRequest* request, int ctrl);
static void handleTimerUpdate(AsyncWebServerRequest* request, int ctrl);

/*  Handles POST request from client ctrlForm.
*/
static void handleSetControl(AsyncWebServerRequest *request) {
  if (!request->hasParam("C", true)) {
    request->send(400, "text/plain", "Bad Request: Missing Control ID");
    return;
  }
uint32_t handlerTimer = millis();

  int ctrl = request->getParam("C", true)->value().toInt();
  String errorMsg = "";

  if (ctrl == SETTINGS_TAB) { // sensor modal update OR add/del ctrl?
    if (request->hasParam("SA", true)) {
      handleControlAddDel(request);
    } else if (request->hasParam("XA", true) || request->hasParam("XC", true)) { // add/del/edit sensor?
      errorMsg = handleSensorUpdate(request);
    } else if (request->hasParam("CN", true)) {
      String name = request->getParam("CN", true)->value();
      name = name.substring(0, MAX_SYS_NAME_LEN); // truncate
      if (!name.isEmpty()) {
        strlcpy(settings.name, name.c_str(), sizeof(settings.name));
        saveConfig("/settings.bin", &settings, sizeof(settings_struct), 0);
      }
    }
  } else if (ctrl >= 0 && ctrl < ctrlCount) {
    if (request->hasParam("A", true)) {
      handleControlAction(request, ctrl);
    } else if (request->hasParam("ST", true) || request->hasParam("SN", true) || request->hasParam("SP", true)) {
      errorMsg = handleControlSettingsUpdate(request, ctrl);
    } else if (request->hasParam("TMR", true)) {
      handleTimerUpdate(request, ctrl);
    }
  } else {
    request->send(404, "text/plain", "Control not found");
    return;
  }
  if (errorMsg != "")
    request->send(422, "text/plain", errorMsg);
  else
    request->send(200, "OK");
debugf("-- reqTime:%lu\n", millis() - handlerTimer); // time to handle request
}

static String handleSensorUpdate(AsyncWebServerRequest* request) {
    bool snsrChanged = false;
    if (request->hasParam("XA", true)) { // sensors add/del
//debugf("->sensor add/del %s\n", request->getParam("XA", true)->value().c_str());
      if (request->getParam("XA", true)->value().equals("A") && snsrCount < MAX_SENSORS) { // add
        initSensor(snsrCount); // init new sensor
        initSnsrIO(snsrCount); // init GPIO
        snsrCount++; // add sensor
      } else if (request->getParam("XA", true)->value().equals("D") && snsrCount > 0) { // delete
        snsrCount--; // remove last sensor in list
      }
      snsrChanged = true;
    
    } else if (request->hasParam("XC", true)) { // = closed sensor edit modal
      int8_t idx = request->getParam("XS", true)->value().toInt(); // get sensor index#
debugf("->snsr update idx:%i\n", idx);
      if (idx >= 0 && idx < snsrCount) {
        // name change?
        if (request->hasParam("XN", true)) {
          String decoded = String(request->getParam("XN", true)->value());
          decoded = decoded.substring(0, MAX_NAME_LEN); // truncate
          if (!decoded.isEmpty()) {
            if (strncmp(decoded.c_str(), (char *)&sensors[idx].name, sizeof(sensors[idx].name)) != 0) {
              strlcpy((char *)&sensors[idx].name, decoded.c_str(), sizeof(sensors[idx].name)); 
              snsrChanged = true;
            }
          }
        }
        // pin change?
        if (request->hasParam("XP", true)) {
          int16_t pin = request->getParam("XP", true)->value().toInt();
          uint8_t newPin = (sensors[idx].pinNum & (STAT_DIGITAL | STAT_EXPANDER)) | (pin & ~(STAT_DIGITAL | STAT_EXPANDER));
          if (pin == -1) {
            newPin = PIN_NOT_AVAIL;

          } else { // type
            if (request->hasParam("XT", true)) {
              if (request->getParam("XT", true)->value().toInt() == 1) {
                newPin |= STAT_DIGITAL;
              } else {
                newPin &= ~STAT_DIGITAL;
              }
            }
            if (request->hasParam("XX", true)) { // expander set?
              if (request->getParam("XX", true)->value().toInt() != -1) {
                newPin &= ~(STAT_EXPANDERID_MASK); // clr expander ID bits
                newPin |= (request->getParam("XX", true)->value().toInt() << 4) | STAT_EXPANDER; // b6 set for expander
              } else
                newPin &= ~STAT_EXPANDER; // b6 clr for no expander
            }
          }
          // validate pin
          if (newPin != PIN_NOT_AVAIL) {
             if (IS_PIN_EXPANDER(newPin)) {
                if (IS_PIN_DIGITAL(newPin)) {
                   if (GET_PIN_EXP_CH(newPin) > 15) return "Invalid Expander Channel (0-15)";
                } else {
                   if (GET_PIN_EXP_CH(newPin) > 3) return "Invalid ADC Expander Channel (0-3)";
                }
             } else {
                if (!isValidPin(GET_PIN_NUM(newPin), false, IS_PIN_DIGITAL(newPin))) return "Invalid Sensor Pin";
             }
          }
          if (sensors[idx].pinNum != newPin) {
            sensors[idx].pinNum = newPin;
            snsrChanged = true;
            initSnsrIO(idx); // re-init all GPIO as new pin may have moved to/from an expander OR need setup.
          }
        }
        // mplex change?
        if (request->hasParam("XM", true)) {
          int16_t pin = request->getParam("XM", true)->value().toInt();
          if (pin == -1)
            pin = 0;
          else {
            if (!isValidPin(pin, true, true)) return "Invalid Multiplex Pin";
            pin = pin & 0x80; //  mplex bit set
          }
          if (sensors[idx].mplex != pin){
            sensors[idx].mplex = pin;
            initSnsrIO(idx); // re-init Input for mplex
            snsrChanged = true;
          }
        }
        if (request->hasParam("XB", true)) {
          int16_t beta = request->getParam("XB", true)->value().toInt();
          if (sensors[idx].beta != beta){
            sensors[idx].beta = beta;
            snsrChanged = true;
          }
        }
        if (request->hasParam("XR", true)) {
          int16_t res = request->getParam("XR", true)->value().toInt();
          if (sensors[idx].resistance != res){
            sensors[idx].resistance = res;
            snsrChanged = true;
          }
        }
        if (request->hasParam("XU", true)) {
          int16_t pu = request->getParam("XU", true)->value().toInt();
          if (sensors[idx].puRes != pu){
            sensors[idx].puRes = pu;
            snsrChanged = true;
          }
        }
      }
    }
    if (snsrChanged) saveSensors();
    return "";
}

static void handleControlAction(AsyncWebServerRequest* request, int ctrl) {
debugf("modesel ctrl:%i\n", ctrl);
  header_struct tmpHeader;
  uint8_t tmrCount = loadHeader(ctrl, &tmpHeader); // Load the specified control context, rtns tmrCount
  if (tmrCount) {
    int action = request->getParam("A", true)->value().toInt();
    debugf("ctrl:%i action:%i\n", ctrl, action);
    bool ctrlChanged = false;
    switch (action) {
      case (int)CtrlMode::OFF: 
        if (!IS_MODE_OFF(tmpHeader)) {
          SET_CTRLON_MODE(ctrl, (uint8_t)CtrlMode::OFF);
          SET_MODE(tmpHeader, (uint8_t)CtrlMode::OFF);
          ctrlChanged = true;
        }
        break;
      case (int)CtrlMode::ON: 
        if (!IS_MODE_ON(tmpHeader)) {
          SET_CTRLON_MODE(ctrl, (uint8_t)CtrlMode::ON);
          SET_MODE(tmpHeader, (uint8_t)CtrlMode::ON);
          ctrlChanged = true;
        }
        break;
      case (int)CtrlMode::AUTO:
        if (!IS_MODE_AUTO(tmpHeader)) {
          SET_CTRLON_MODE(ctrl, (uint8_t)CtrlMode::AUTO);
          SET_MODE(tmpHeader, (uint8_t)CtrlMode::AUTO);
          ctrlChanged = true;
        }
        break;
      case (int)CtrlMode::BOOST:
        {
          timer_struct timer0;
          if (loadTimer(ctrl, 0, &timer0)) { // get timer0
            uint8_t prevState = 0;
            if (!IS_MODE_BOOSTADV(tmpHeader))
              prevState = GET_MODE(tmpHeader) << 4; // get current state and save (end of boost will restore on/off/auto states)
            tmpHeader.modes = (uint8_t)CtrlMode::BOOST | prevState;
            SET_CTRLON_MODE(ctrl, (uint8_t)CtrlMode::BOOST);
            timer0.onTime = getLocalTime_mins();
            timer0.duration = 60;
            ctrlChanged = true;
            saveTimer(ctrl, 0, &timer0);
          }
        }
        break;
      case (int)CtrlMode::ADV: 
        {
          if (!IS_MODE_ADV(tmpHeader)) {
            timer_struct timer0;
            timer_struct timer1;
            if (loadTimer(ctrl, 0, &timer0) && loadTimer(ctrl, 1, &timer1)) { // get timers 0 & 1
              if (tmrCount > 1 && timer1.onTime != 0 && timer1.duration != 0) { // only set if there is a valid timer1 time set!
                uint8_t prevState = 0;
                if (!IS_MODE_BOOST(tmpHeader))
                  prevState = GET_MODE(tmpHeader) << 4; // get current state and save (cancels any boost currently active)
                tmpHeader.modes = (uint8_t)CtrlMode::ADV | prevState;
                SET_CTRLON_MODE(ctrl, (uint8_t)CtrlMode::ADV);
                timer0.onTime = getLocalTime_mins();
                timer0.duration = timer1.duration;
                saveTimer(ctrl, 0, &timer0);
                ctrlChanged = true;
              }
            }
          }
        }
        break;

    }
    if (ctrlChanged) saveHeader(ctrl, &tmpHeader);
  }
}

static void handleControlAddDel(AsyncWebServerRequest* request) {
debugf("->add/del:%s ctrlCnt:%i\n", request->getParam("SA", true)->value().c_str(), ctrlCount);
    if (request->getParam("SA", true)->value().equals("A") && ctrlCount < MAX_CONTROLS) { // ADD
      makeNewControl(ctrlCount); // make new control file and inc ctrlCount
    } else if (request->getParam("SA", true)->value().equals("D") && ctrlCount > MIN_CTRLS) { // DELETE
      if (deleteControl(ctrlCount - 1)) { // remove last control
        ctrlCount--;
      }
    }// web page should load control0 as a the current Ctrl may have been deleted if it was last one in list!
}

static String handleControlSettingsUpdate(AsyncWebServerRequest* request, int ctrl) {
debugf("->update ctrl:%i\n", ctrl);
    // update control settings and save config file
  header_struct tmpHeader;
  uint8_t tmrCount = loadHeader(ctrl, &tmpHeader); // Load the specified control context, rtns tmrCount
  if (tmrCount) {
    bool ctrlChanged = false;
    if (request->hasParam("ST", true)) { // add/del timers?
      int num = request->getParam("ST", true)->value().toInt() + 1; // web input rtns 1-nnn timers - so i+1 to incl timer0
      if (num >= 1 && num <= MAX_TIMERS && num != tmrCount) {
        timer_struct newTimer;
        initTimer(&newTimer);
        while (num > tmrCount) { //  adding timers - init new ones to defaults
          if (saveTimer(ctrl, tmrCount, &newTimer)) // write a new timer to end of Control file
            tmrCount++;
          else 
            return "Unable to add more Timers"; // save failed
        }
        if (num < tmrCount) { // remove timers from end of file
          if (truncateFile(ctrl, num))
            tmrCount = num;
          else
            return "Unable to remove Timers";
        }
      }
    }
    // name change?
    if (request->hasParam("SN", true)) {
      if (request->getParam("SN", true)->value().length()) {
        String decoded = request->getParam("SN", true)->value();
        decoded = decoded.substring(0, MAX_NAME_LEN); // truncate
        if (!decoded.isEmpty()) {
          if (strncmp(decoded.c_str(), tmpHeader.name, MAX_NAME_LEN) != 0) {
            strlcpy(tmpHeader.name, decoded.c_str(), sizeof(tmpHeader.name)); 
            ctrlChanged = true;
          }
        }
      }
    }
    // ctrl pin change?
    if (request->hasParam("SP", true)) {
      int16_t pin = request->getParam("SP", true)->value().toInt();
      uint8_t newPin = (tmpHeader.pinNum & (PIN_ACTIVELO | PIN_EXPANDER)) | (pin & ~(PIN_ACTIVELO | PIN_EXPANDER));
      if (pin == -1) {
        newPin = PIN_NOT_AVAIL;

      } else {
        if (request->hasParam("SL", true)) {
          if (request->getParam("SL", true)->value().toInt() == 1) {
            newPin |= PIN_ACTIVELO;
          } else {
            newPin &= ~PIN_ACTIVELO;
          }
        }
        if (request->hasParam("SX", true)) { // expander set?
          if (request->getParam("SX", true)->value().toInt() != -1) {
            newPin &= ~(PIN_EXPANDERID_MASK); // clr expander ID bits
            newPin |= (request->getParam("SX", true)->value().toInt() << 4) | PIN_EXPANDER; // b6 set for expander
          } else
            newPin &= ~PIN_EXPANDER; // b6 clr for no expander
        }
      }
      // validate pin
      if (newPin != PIN_NOT_AVAIL) {
        if (IS_PIN_EXPANDER(newPin)) {
           if (GET_PIN_EXP_CH(newPin) > 15) return "Invalid Expander Channel# (0-15)";
        } else {
           if (!isValidPin(GET_PIN_NUM(newPin), true, true)) return "Invalid Control Pin#";
        }
      }
      if (tmpHeader.pinNum != newPin) {
        tmpHeader.pinNum = newPin;
        ctrlChanged = true;
        initCtrlIO(newPin); // re-init GPIO new pin may have moved to/from an expander OR need setup.
      }
    }
    if (ctrlChanged) 
      if (!saveHeader(ctrl, &tmpHeader))
        return "Unable to save updated Control name";
  }
  return "";
}

static void handleTimerUpdate(AsyncWebServerRequest* request, int ctrl) {
debugln("->tmr update\n");
  header_struct tmpHeader;
  uint8_t tmrCount = loadHeader(ctrl, &tmpHeader); // Load the specified control context, rtns tmrCount
  if (tmrCount) {
    if (request->hasParam("TMR", true)) {
      int timer = request->getParam("TMR", true)->value().toInt(); // timer to edit
      if (timer >= 0 && timer < tmrCount) { // valid timer#?
        timer_struct editTimer; // local copy of the timer
        if (loadTimer(ctrl, timer, &editTimer)) { // get the timer data
          bool timerChanged = false;
          if (request->hasParam("O", true)) {
            uint16_t val = convertHH_MMtoMins(request->getParam("O", true)->value());
            if (val != editTimer.onTime) {
              editTimer.onTime = val;
              timerChanged = true;
            }
          }
          if (request->hasParam("W", true)) {
            uint8_t val = request->getParam("W", true)->value().toInt();
            if (val != editTimer.onWeekDay) {
              editTimer.onWeekDay = val;
              timerChanged = true;
            }
          }
          if (request->hasParam("D", true)) {
            uint32_t val = strtoul(request->getParam("D", true)->value().c_str(), NULL, DEC); // 1-31 (.toInt() rtns int32 not uint32!)
            if (val != editTimer.onDate) {
              editTimer.onDate = val;
              timerChanged = true;
            }
          }
          if (request->hasParam("M", true)) {
            uint8_t val = request->getParam("M", true)->value().toInt();
            if (val != editTimer.onMonth) {
              editTimer.onMonth = val;
              timerChanged = true;
            }
          }
          if (request->hasParam("P", true)) {
            uint32_t val = convertDDD_HH_MMtoMins(request->getParam("P", true)->value());
            if (val != editTimer.duration) {
              editTimer.duration = val;
              timerChanged = true;
            }
          }
          if (request->hasParam("T", true)) {
            int16_t val = request->getParam("T", true)->value().toInt(); // neg temp?
            if (val != editTimer.temp) {
              editTimer.temp = val;
              timerChanged = true;
            }
          }
          if (request->hasParam("L", true)) {
            uint8_t val = request->getParam("L", true)->value().toInt();
            if (val != GET_TMR_LESSTHAN(editTimer)) { // value changing?
              editTimer.lessHyst = (val << 14) | GET_TMR_HYST(editTimer);
              timerChanged = true;
            }
          }
          if (request->hasParam("H", true)) {
            uint16_t val = request->getParam("H", true)->value().toInt();
            if (val != GET_TMR_HYST(editTimer)) {
              editTimer.lessHyst = val | (GET_TMR_LESSTHAN(editTimer) << 14);
              timerChanged = true;
            }
          }
          if (request->hasParam("S", true)) {
            int8_t val = request->getParam("S", true)->value().toInt(); // -1 for no stat 
            if ((val < snsrCount || val == NO_STAT) && val != editTimer.statIdx) {
              editTimer.statIdx = val;
              timerChanged = true;
            }
          }
            // save timer if the value was updated!
          if (timerChanged) { 
            saveTimer(ctrl, timer, &editTimer);
    //printTimer(timer, &editTimer);
            debugln("done tmrEdit:");
          }
        }
      }
    }
  }
}
