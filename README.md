# hvacMonitor
A small project to simply monitor my HVAC system with future plans to add control.<br>
<p align="center"><img src="http://i.imgur.com/th3gq05.png"/></p>
Hardware is a WeMos D1 Mini and a few DS18B20 1-Wire digital thermometers. I started out with an ESP-01 but, as always, I wanted more!

## Features
 * Arduino code running on a single [WeMos D1 Mini](http://www.wemos.cc/Products/d1_mini.html).
 * Monitoring of the air temperature coming into my HVAC unit (return air) and temperature of air after it's cooled (supply air), including a Blynk notification if the unit isn't cooling enough. All temperature sensors are <a href="https://datasheets.maximintegrated.com/en/ds/DS18B20.pdf">Maxim/Dallas DS18B20</a>... a mix of probe-style and bare TO-92 package sensors.
 * Monitoring of HVAC run status. This is done by tapping the 24VAC t-stat with an ice cube relay to give the WeMos a dry contact to monitor. The Blynk app displays if the HVAC is on or off, and how long it's been on or off. To monitor on/off and temperature activities, information is sent to data.sparkfun.com and share with analog.io to produce some pretty graphs.
 * EEPROM storage to survive ESP resets.
 * OTA Updates: Using BasicOTA. Learned [from this post](https://github.com/esp8266/Arduino/issues/1017#issuecomment-223466025) that a complete power down is required after uploading BasicOTA for the first time. Weird, but whatever. [Ivan](https://github.com/igrr), I have a man crush.
 * *Currently on hold*: 4-channel DC 5V relay switch module (<a href="http://www.ebay.com/itm/321869298037">source</a>) providing control of Fan Only and Cooling modes. Todo: Heating and Bypass-Only (allow ESP to control HVAC *in lieu of* house t-stat).

## Libraries and Resources

Title | Include | Link 
------|---------|------
Time | Timelib.h | https://github.com/PaulStoffregen/Time
SimpleTimer | SimpleTimer.h | https://github.com/jfturcot/SimpleTimer
ESP8266/Arduino | ESP8266WiFi.h | https://github.com/esp8266/Arduino
blynk-library | BlynkSimpleEsp8266.h, WidgetRTC.h, TimeLib.h | https://github.com/blynkkk/blynk-library
OneWire | OneWire.h | https://github.com/PaulStoffregen/OneWire
Arduino-Temperature-Control-Library | DallasTemperature.h | https://github.com/milesburton/Arduino-Temperature-Control-Library
ESP8266 board mgr | N/A | [json](http://arduino.esp8266.com/stable/package_esp8266com_index.json) & [instructions](https://github.com/esp8266/Arduino#installing-with-boards-manager)
EEPROM | EEPROM.h |

Many thanks to all the library authors. I know nothing. They do.

## Pin Assignments
HW Pin | GPIO† | Purpose 
------|-----|------
D6 | 12 | Cooling run state. 10K pullup.
D7 | 13 | DS18B20 array. 4.7K pullup.
D1 | 5  | *Future* cooling relay.
D2 | 4  | *Future* heating relay.
D5 | 14 | *Future* t-stat bypass relay.
D3 | 0  | Note: Be careful use does not conflict with ESP "pull" required to set mode.††
D4 | 2  | Note: Be careful use does not conflict with ESP "pull" required to set mode.††
D8 | 15 | Note: Be careful use does not conflict with ESP "pull" required to set mode.††

† ESP8266 GPIO: The pin number used in the IDE.</br>
†† [*About ESP GPIOs and boot modes.*](http://www.forward.com.au/pfod/ESP8266/GPIOpins/index.html)

## Testimonials
From `friend`:
> Cool, man.

From `kids`:
> What is that?

From `wife`:
> That's nice, honey.

From `me`:
> It's great to have historical data on outside temperatures and see how they impact the inside, as well as notifications if the return air/supply air delta temperature drops too low or is trending that way. I can at least get a jump on possible refrigerant loss, compressor overheat, or other service issue. Toss in a notification when the outside temperature equals my house thermostat target (indicating when I can cut off A/C and open doors/windows) and I might even see a small utility savings. Go team.


[How to edit this.](https://guides.github.com/features/mastering-markdown/)
