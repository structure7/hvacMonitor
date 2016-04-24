# hvacMonitor
A small project to simply monitor my HVAC system. I do not currently have plans to add control (as the t-stat seems to handle this fine on its own).<br>
<p align="center"><img src="http://i.imgur.com/U9lY9Yu.png"/></p>
Hardware is a standalone ESP8266-01 and a few DS18B20 1-Wire digital thermometers. ESP8266-01 may be replaced by a larger Pro Mini or ESP-12E as bells and whistles increase.

## Libraries and Resources

Title | Include | Link | w/ IDE?
------|---------|------|----------
Time | Timelib.h | https://github.com/PaulStoffregen/Time
SimpleTimer | SimpleTimer.h | https://github.com/jfturcot/SimpleTimer | No
ESP8266/Arduino | ESP8266WiFi.h | https://github.com/esp8266/Arduino | No
blynk-library | BlynkSimpleEsp8266.h, WidgetRTC.h | https://github.com/blynkkk/blynk-library | No
OneWire | OneWire.h | https://github.com/PaulStoffregen/OneWire
Arduino-Temperature-Control-Library | DallasTemperature.h | https://github.com/milesburton/Arduino-Temperature-Control-Library
ESP8266 board mgr | N/A | [json](http://arduino.esp8266.com/stable/package_esp8266com_index.json) & [instructions](https://github.com/esp8266/Arduino#installing-with-boards-manager) | No

Many thanks to all of the people above. [How to edit this.](https://guides.github.com/features/mastering-markdown/)

## Testimonials
From `friend`:
> Huh. Kinda cool.

From `kids`:
> What is that?

From `wife`:
> That's nice, honey.

From `me`:
> It's great to have historical data on outside temperatures and see how they impact the inside, as well as notifications if the return air/supply air delta temperature drops too low or is trending that way. I can at least get a jump on possible refrigerant loss, compressor overheat, or other service issue. Toss in a notification when the outside temperature equals my house thermostat target (indicating when I can cut off A/C and open doors/windows) and I might even see a small utility savings. Aces.
