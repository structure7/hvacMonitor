# hvacMonitor
A small project to simply monitor my HVAC system. I do not currently have plans to add control (as the t-stat seems to handle this fine on its own).<br>
<p align="center"><img src="http://i.imgur.com/FeLw6KQ.png"/></p>
Hardware is a standalone ESP8266-01 and a few DS18B20 1-Wire digital thermometers. ESP8266-01 may be replaced by a larger Pro Mini or ESP-12E as bells and whistles increase.

## Features
 * Arduino code running on a single [WeMos D1 Mini](http://www.wemos.cc/Products/d1_mini.html).
 * Monitoring of the air temperature coming into my HVAC unit (return air) and temperature of air after it's cooled (supply air), including a Blynk notification if the unit isn't cooling enough. All temperature sensors are <a href="https://datasheets.maximintegrated.com/en/ds/DS18B20.pdf">Maxim/Dallas DS18B20</a>... a mix of probe-style and bare TO-92 package sensors.
 * Monitoring of HVAC run status. This is done by tapping the 120V supply to a blower motor with a standard phone charger (120VAC to 5VDC), then using a Fairchild 2N3904 NPN Small Signal TO-92 Transistor to reverse the logic from 5V to GND (required as the ESP8266 GPIO I'm using seems to pull itself high on boot and stays highs. If I pull it low the ESP8266 will not boot). A Blynk virtual LCD displays if the HVAC is on or off, and how long it's been on or off. When the unit starts or stops, a timestamped notification is sent to Twitter (using Blynk app).
 * EEPROM storage to survive ESP resets.
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

Many thanks to all the library authors. I know nothing. They do

## Pin Assignments
HW Pin | SW Pin | Purpose 
------|------|------
D6 | 12 | Cooling run state. 10K pullup.
D7 | 13 | DS18B20 array. 4.7K pullup.
D1 | 5  | *Future* cooling relay.
D2 | 4  | *Future* heating relay.
D5 | 14  | *Future* t-stat bypass relay.

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
