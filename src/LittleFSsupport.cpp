/*
   LittleFSsupport.cpp
   by Matthew Ford,  2021/12/06
   (c)2021 Forward Computing and Control Pty. Ltd.
   NSW, Australia  www.forward.com.au
   This code may be freely used for both private and commerical use.
   Provide this copyright is maintained.

*/

#include "main.h"

static bool FS_initialized = false;

/* ===================
r   Open a file for reading. If a file is in reading mode, then no data is deleted if a file is already present on a system.
r+  open for reading and writing from beginning

w   Open a file for writing. If a file is in writing mode, then a new file is created if a file doesn’t exist at all. 
    If a file is already present on a system, then all the data inside the file is truncated, and it is opened for writing purposes.
w+  open for reading and writing, overwriting a file

a   Open a file in append mode. If a file is in append mode, then the file is opened. The content within the file doesn’t change.
a+  open for reading and writing, appending to file
============== */

bool initializeFS() {
  if (FS_initialized) {
    return FS_initialized;
  }

  debugln("Mount LittleFS");
  if (!LittleFS.begin()) {
    debugln("LittleFS mount failed");
    return FS_initialized;
  }
  // else
  FS_initialized = true;
  listDir("/");
  return FS_initialized;
}

void listDir(const char * dirname) {
  if (!FS_initialized) {
    debugln("FS not initialized yet");
    return;
  }

  debugf("Listing directory: %s\n", dirname);
#ifdef ESP32
  File root = LittleFS.open(dirname);
  if (root.isDirectory()) {
    File file;
    while (file = root.openNextFile()) {
      if (file.isDirectory()) {
        debugf("  DIR : %s\n", file.name());
      } else 
        debugf("  FILE: %s SIZE: %i\n", file.name(), file.size());
    }
  }
#else
  Dir root = LittleFS.openDir(dirname);
  while (root.next()) {
    File file = root.openFile("r");
    debug("  FILE: ");
    debug(root.fileName());
    debug("  SIZE: ");
    debugln(file.size());
    file.close();
  }
#endif
  debugln();
}

bool renameFile(const char * path1, const char * path2) {
  if (!FS_initialized) {
    debugln("FS not initialized yet");
    return false;
  }

  debugf("Renaming file %s to %s\n", path1, path2);
  if (LittleFS.rename(path1, path2)) {
    debugln("File renamed");
    return true;
  } //else
  debugln("Rename failed");
  return false;
}

bool deleteFile(const char * path) {
  if (!FS_initialized) {
    debugln("FS not initialized yet");
    return false;
  }
  debugf("Deleting file: %s\n", path);
  if (LittleFS.remove(path)) {
    debugln("File deleted");
    return true;
  }
  //else
  debugln("Delete failed");
  return false;
}
