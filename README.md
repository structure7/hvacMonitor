# hvacMonitor

A small project to simply monitor my HVAC system. I do not currently have plans to add control (as the t-stat seems to handle this fine on its own).

Hardware is a standalone ESP8266-01 and a few DS18B20 1-Wire digital thermometers. ESP8266-01 may be replaced by a larger Pro Mini or ESP-12E as bells and whistles increase.

## Libraries and Resources

Title | Include | Link
Time | Timelib.h | https://github.com/PaulStoffregen/Time
* Time (TimeLib.h): https://github.com/PaulStoffregen/Time
* SimpleTimer (SimpleTimer.f): https://github.com/jfturcot/SimpleTimer
* ESP8266 (ESP8266WiFi.h): https://github.com/esp8266/Arduino
* blynk-library (BlynkSimpleEsp8266.h and WidgetRTC.h): https://github.com/blynkkk/blynk-library
* OneWire (OneWire.h): https://github.com/PaulStoffregen/OneWire
* Arduino-Temperature-Control-Library (DallasTemperature.h): https://github.com/milesburton/Arduino-Temperature-Control-Library

Many thanks to all of the people above.
