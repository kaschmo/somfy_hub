# somfy_hub
ESP8266 based hub to control somfy shading/awning.
Ported Nickduino's software to ESP8266. 
https://github.com/Nickduino/Somfy_Remote
- changed direct port register access. digitalWrite is fast enough on ESP.
- changed EEPROM commands for ESP8266.
- Added MQTT support to trigger 433Mhz Command send via MQTT commands.
