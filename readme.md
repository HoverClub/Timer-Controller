A comprehensive programmable controller for managing timers, sensors, and control outputs, with a web-based interface for configuration and monitoring based on the Espressif 8266.  Supports up to 127 controls with 127 timers (time periods) per control with on-demand control web page loading.  Each timer can use  an optional input sensor (thermostat, switch, etc.) .  All of this is programmed through the web based user interface,

To reduce network 

Inspired by (and includes code from) Matthew Ford,  2021/12/06 (c)2021-2022 Forward Computing and Control Pty. Ltd. https://www.forward.com.au/pfod/HomeAutomation/PowerTimer/index.html NSW, Australia  www.forward.com.au  This code may be freely used for both private and commerical use. Provide this copyright is maintained.

1.  The web interface is designed to be used on a local network.  It is not secure 
    and should not be exposed to the internet.  It is intended to be used to configure
    and monitor the controller.  The controller can be configured to connect to the 
    local network using the web interface and then the web interface can be used to 
    monitor and configure the controller.
    
2.  To transfer files to littleFS storage using PlatformIO:
    Files MUST be in /data folder under project
    Click the PIO icon at the left side bar. The project tasks should open.
    Select board env: name (depends on the board you’re using).
    Expand the Platform menu.
    Select Build Filesystem Image.
    Make sure the serial monitor isn't connected to the board then 
    click Upload Filesystem Image.

3.  If you enter the wrong WiFi password then either wipe the flash OR
    upload a new Filesystem Image in platformIO (this will delete any WiFi & 
    control/timer settings).  If no network SSID has been saved  it will try to connect 
    to 'wifiSSID' / wifiPassword' (wifiConfig.cpp) before setting up the access point to let 
    you configure the network.  Change those values to your local network to improve 
    reliability if you will only connect to this network.  Change 'wifiWebConfigAP' to
    whatever you want to call the configuration access point setup by this device.

4.  If using a static IP and Google DNS then DNS does not work (won't resolve any site 
    names).  If using local router DNS then external addresses are resolved OK BUT ntp 
    doesn't return any data (UDP blocked?).

The web interface loads each control tab html content into the home page when the user selects a control to view/modify.  If the user selects another then it is loaded onto the page at that time.  This considerably reduces the size of served html (there is limited serving capacity on ESP8266/32) and allows the number of controls to be increased up to a (theoretical) 127 without any noticeable performance hit.  

Timers are currently fixed at 4 per control max. BUT it would be possible to increase this significantly by also serving timer html on demand as per control html.  This device will usually be operated on a local network so multiple html requests per page won't normally be a significant issue.
