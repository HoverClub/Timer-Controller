# Multi-Channel Programmable Timer Controller


A comprehensive programmable controller for managing timers, sensors, and outputs, with a web-based user interface for configuration and monitoring based on the Espressif ESP8266 or ESP32.  It supports up to 126 control outputs, each with up to 126 timer periods, each with optional input sensors (thermostats, switches, etc.).  Time is supplied and updated using Internet NNTP servers (user-defined time zone) - no realtime clock hardware is required.  A configuration page is available to allow connection the device to their local WiFi network. An optional low cost OLED display can be added to display QR codes and IP addresses for intial network setup and (once connected to the local WiFi network) the devices IP address on the local network.

**CONTROLS**
Each control is 'connected' to an output that can be defined as active low or high (compile time) and be named by the user.  Output pins can be located on an I2C-connected IO expander (64 max.) or on a GPIO pins.  Each control can have up to a (theoretical!) 126 timers attached to it (compile option).

**TIMERS**
Each timer, in addition to a programmable time period, can use any available input sensor (thermostat, switch, etc.).  If the time is within the set timer period AND the sensors input meets user-set conditions (setpoint, hysteresis and less/more than) the control output will turn on.  Hysteresis creates a 'dead zone' within which the output won't change state (eg if the set point is 25 and hysteres is +/-2 then the timer won't change state when the sensor is between 23 and 27 inc.).  This can be used to avoid output chatterring.


**INPUT SENSORS**
Sensors can be either simple on/off digital switch or variable voltage (analog).  They can be simultaneously used by multiple timers across all controls.  Max. # of input sensors is 63 and they can be located on an I2C IO/ADC expander or ESP GPIO pins. In addition to a setpoint trigger value, any analog sensor can have hysteresis value added creating a 'dead zone' around the trigger value (ie. the output won't change state inside this range).


This is all programmed through the web based user interface. Users can download (and upload) the complete current configuration of the device controls & timers.


### Control Operating Modes

**BOOST/ADVANCE** modes use a 'hidden' timer.  Its ON time is set to the current time.  *ADVANCE* uses the OFF time from timer1 and *BOOST* sets the OFF time to the current time plus 1 hour.  Advance essentially 'brings forward' the ON time for timer1.   If the current time is already past the timer1 ON time OR timer1 has no on/off time set then advance will have no effect.  If timer1 has an active input sensor then it can also used by boost/advance to control the output (complie time option).  When the advance/boost time period ends the control will revert to its previous mode (ON/OFF/AUTO). 

**AUTO** If the current time is between any of the controls timers ON and OFF times and, either there is no timer input sensor for that timer OR the sensor input meets the ON conditions, then the control will turn on.

**OFF** Disables (set the control output inactive) the control regardless of timers or sensors.

**ON** Sets the control active.  The behaviour of this option can be changed using the `USE_ANY_TIMER_STAT` and `USE_TIMER1_STAT` compile time options in `main.h` (see file for documentation).


## Compiling

There are debug versions of the code in the `platformio.ini` project file.  They will output extensive debug info to the Serial monitor during operation.

### Filesystem
Configuration and HTML files are stored by LittleFS in the device.  They can be found in the project/data directory.  If you modify any of them (or aare using a newly programmed device) you will need to upload the filesystem image to the device using PlatformIO. `PlatforionIcon->expand 'Platform' item->Build Filesystem` then `Upload Filesystem`

### Compile-time Configuration

#### Controls
A control is created for each pin in the `pins[]` array in the `main.h` file.  To set a control output as active low, logical OR the pin number with `PIN_ACTIVELO` (eg. `CTRLPIN26 | PIN_ACTIVELO`).  Output pins can be located on a I2C connected MCP23017 IO expander (four are supported) or an ESP GPIO pins.  For expander pins, b6 of the pin number is set (OR with `PIN_EXPANDER`), b0-3 is the expander channel, b4-5 select one of four expander MCP23017 devices.

### Input Sensors
Input sensor pins are defined in the `statPins[]` array in the `pinDefs.cpp` file.  They can be defined as digital by setting b7 of the pin# (`OR` with `STAT_DIGITAL`), b7 clear is analog.  If the `STAT_EXPANDER` bit is set then they are located on an I2C expander module (ADC1115 or MCP23017) instead of a GPIO pin.  The ADC1115 has four ADC channels per device, the MCP23017 has 16 digitial IO - you can add up to 4 of each expander making a total of 16 ADC inputs and 64 digital IO in addition to any available ESP GPIO pins.  If a sensor is on an expander then the expander ID (0-3) is in bits 4-5 and the channel# (0-15) in bits0-3.  Each expander must have its I2C address set differently (starting at 0x48 for ADC and 0x20 for digital - see datasheets). 

Analog thermistor/variable voltage type inputs can have the Steinhart-Hart equation applied (using compile-time beta, resistance and pull up resistance parameters).  Non-expander analog sensors are read using the ESPs ADC - the ADC can be multiplexed between sensors by defining a grounding pin in the sensor definition (a sensor is grounded to read it - the ADC is connected in common to all the other pins of all multiplexed sensors) - useful for the ESP8266 which only has one ADC.

Sensors can be named in the `statPins[]` array.  When shown on the web page the sensor name starts with 'x' for expander sensors and 'p' for ESP pins followed by the pin number (or exander ID/Channel#) then the name.


### Timers
The number of timers avaliable in each control can be set at compile time by changing the `MAX_TIMERS` value.  The base code and web page HTML can handle up to 126 timers per control **BUT** there is a limit to the number of timers that can practically be displayed on a single web page.  

In addition to a setpoint value, any analog timers input sensor can have hysteresis added - this creates a 'dead zone' around the trigger value (ie. the output won't change state inside this range).

The operation of `LESSTHAN MORETHAN` on each timer is obvious for analog/thermistor type inputs.  For digital/switch inputs `LESSTHAN` will invert the the sensor active state (i.e. if lessthan the sensor will be active when the input is read as **inactive** - inverting the condition read).  This allows the use of the same sensor on different timers/controls with one turning on when the sensor is active and the other when inactive.

### General
The device automatically restarts every 24 hours to avoid potential long-term heap fragmentation issues (although none have been detected they are common in long runtime embedded systems).  A restart will only occur when no controls are active.

### Web Interface
Each controls data is loaded on-demand into the web page when the user selects that control to view.  This considerably reduces the size of served html (there is limited serving capacity on ESP8266/32) and allows the number of controls to be increased up to 126 without any noticeable performance hit (other than a small increase in RAM usage). 

Any downloaded config filename will have the name `CONTROLLER_NAME + '_cfg.bin'` (the `CONTROLLER_NAME` define is in `main.h`). When uploading, only a file with the same name will be accepted. `CONTROLLER_NAME` should be unique to the specific controller build configuration (and pins used) to avoid uploading the wrong config file.  If the file has the same name and uploads OK but isn't the same size as the current configuration the system will reset to the default settings (the WiFi network settings will be retained) .

>   The web interface is designed to be used on a local network.  It is not secure and should not be exposed to the internet.  It is intended to be used to configure and monitor the controller.



## Credits
Includes code from Matthew Ford,  2021/12/06 (c)2021-2022 Forward Computing and Control Pty. Ltd. https://www.forward.com.au/pfod/HomeAutomation/PowerTimer/index.html NSW, Australia  www.forward.com.au  This code may be freely used for both private and commerical use. Provide this copyright is maintained.
  

3.  If you enter the wrong WiFi password then the device will be unable to connect to your network.  The board LED will flash rapidly as it tries to connect.  If it fails to connect the LED will slowly flash - at that point it has setup an access point (SSID = 'Controller', no password) with a WiFi web config page at IP 10.1.1.1.


    If no network SSID has been saved  it will try to connect to 'wifiSSID' / wifiPassword' (wifiConfig.cpp) before setting up the access point to let you configure the network.  Change those values to your local network to improve 
    reliability if you will only connect to this network.  Change 'wifiWebConfigAP' to
    whatever you want to call the configuration access point setup by this device.
