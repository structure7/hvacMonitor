# hvacMonitor

A small project to simply monitor my HVAC system. I do not currently have plans to add control (as the t-stat seems to handle this fine on its own).

Hardware is a standalone ESP8266-01 and a few DS18B20 1-Wire digital thermometers. ESP8266-01 may be replaced by a larger Pro Mini or ESP-12E as bells and whistles increase.

## Libraries and Resources

* Time (Time.h and/or TimeLib.h): https://github.com/PaulStoffregen/Time
* SimpleTimer (SimpleTimer.f): https://github.com/jfturcot/SimpleTimer
