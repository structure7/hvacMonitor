Mid-2017 note: Did I disappear? No! Am I moving? Yes! Once I land again development will return.

# hvacMonitor
A small project to monitor and control my HVAC system. Designing a PCB now!<br>
<p align="center"><img src="http://i.imgur.com/kOhaFHD.png"/></p>
Hardware is a WeMos D1 Mini, a few DS18B20 digital thermometers, and 4 relays. I started out with an ESP-01 but, as always, I wanted more!

## Features
 * Arduino code running on a single [WeMos D1 Mini](http://www.wemos.cc).
 * Monitoring of the air temperature coming into my HVAC unit (return air) and temperature of air after it's cooled/heated (supply air), including a Blynk notification if the unit isn't performing. All temperature sensors are <a href="https://datasheets.maximintegrated.com/en/ds/DS18B20.pdf">Maxim/Dallas DS18B20</a>... a mix of probe-style and bare TO-92 package sensors.
 * Monitoring of HVAC run status. This is done by tapping the 24VAC t-stat with relays (NTE R14-11A10-24(11)) to give the WeMos dry contacts to monitor. The Blynk app displays if the HVAC is on or off, and how long it's been on or off. 
 * Sending RA/SA temps and run status to a [Phant](http://phant.io/) server on a Raspberry Pi in lieu of data.sparkfun.com (which had uptime issues):
   * Install Raspbian and Node.js using [Dave Johnson's fantastic guide](http://thisdavej.com/beginners-guide-to-installing-node-js-on-a-raspberry-pi/).
   * Install Phant with `sudo npm install -g phant`
   * Because of [this issue](https://github.com/sparkfun/phant/issues/200):
     * Run `sudo git clone https://github.com/stoto/phant.git` to download fixed files (thanks [stoto](https://github.com/stoto)!)
     * Run `gksudo pcmanfm` to open file manager as root, allowing for drag and drop.
     * Copy/overwrite the content of stoto's `/phant/lib` into `/usr/lib/node_modules/phant/lib`. If this folder doesn't exist, check `/usr/local/lib/node_modules/phant/lib`
     * Run `phant`.
     * Edit home.handlebars at '/usr/lib/node_modules/phant/node_modules/phant-manager-http/views' if you'd like to keep your streams from prying eyes.
 * All data that needs to survive a hardware reset is stored to Blynk virtual pins, then synced back after a reset.
 * OTA Updates: Using BasicOTA. Learned [from this post](https://github.com/esp8266/Arduino/issues/1017#issuecomment-223466025) that a complete power down is required after uploading BasicOTA for the first time. Weird, but whatever. [Thank you Ivan!](https://github.com/igrr)
 * 4-channel DC 5V relay switch module (<a href="http://www.ebay.com/itm/321869298037">source</a>) providing control of cooling, heating and fan-only modes.
 * *Future:* Replace 24VAC dry contact relays with current monitoring via a SCT-013-030 current transformer (for HVAC run status).
 * *Future:* Air quality monitoring with a true laser particle counter (ideally 2 size ranges (>0.5 & >2.5 microns)). Either in-unit to monitor filter efficiency, and/or exterior (outdoor) and indoor AQMs that prompt for fan-only HVAC operation to utilize MERV 13 filter for air cleaning.

## Libraries and Resources

Title | Include | Link 
------|---------|------
Time | Timelib.h | https://github.com/PaulStoffregen/Time
SimpleTimer | SimpleTimer.h | https://github.com/jfturcot/SimpleTimer
blynk-library | BlynkSimpleEsp8266.h, WidgetRTC.h, TimeLib.h | https://github.com/blynkkk/blynk-library
OneWire | OneWire.h | https://github.com/PaulStoffregen/OneWire
Arduino-Temperature-Control-Library | DallasTemperature.h | https://github.com/milesburton/Arduino-Temperature-Control-Library
ESP8266 board mgr | N/A | [json](http://arduino.esp8266.com/stable/package_esp8266com_index.json) & [instructions](https://github.com/esp8266/Arduino#installing-with-boards-manager)

Many thanks to all the library authors. I know nothing. They do.

## Pin Assignments
HW Pin | GPIO† | Purpose 
------|-----|------
A0 | A0 | Available (future current transformer).
D0 | 16 | Fan control relay (green wire††).
D1 | 5  | Cooling control relay (yellow wire††).
D2 | 4  | Heating control relay (white wire††).
D3 | 0  | T-stat control bypass relay.†††
D4 | 2  | Available.†††
D5 | 14 | HVAC fan-only run state.
D6 | 12 | DS18B20 array. 4.7KΩ pullup.
D7 | 13 | HVAC cooling/heating motor run state. 10KΩ pullup. *Future:* Split these two states.
D8 | 15 | Available.†††

† ESP8266 GPIO: The pin number used in the IDE.</br>
†† Wire colors correspond to standard HVAC thermostat [4-wire system](https://en.wikipedia.org/wiki/Thermostat#Combination_heating.2Fcooling_regulation).</br>
††† [*About ESP GPIOs and boot modes.*](http://www.forward.com.au/pfod/ESP8266/GPIOpins/index.html)

## Testimonials
From `friend`:
> Cool, man.

From `kids`:
> What is that?

From `wife`:
> That's nice, honey.

From `me`:
> It's great to have historical data on outside temperatures and see how they impact the inside, as well as notifications if the return air/supply air delta temperature drops too low or is trending that way. I can at least get a jump on possible refrigerant loss, compressor overheat, or other service issue. I can also set any room's temperature sensor to be a virtual thermostat... from my phone. ZONES! Go team.


[How to edit this.](https://guides.github.com/features/mastering-markdown/)
