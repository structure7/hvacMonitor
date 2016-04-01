/*
        Add additional DS18B20 at house t-stat (CAT5e or extra tstat wire).
        Add logic for notification/alarm if split temp is too low after X minutes.
        Might still want to use an acutal RTC to avoid potential Blynk server issues, etc.
        Maybe clean up some of the lcd.print madness.
*/

#include <SimpleTimer.h>
#define BLYNK_PRINT Serial    // Comment this out to disable prints and save space
#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <TimeLib.h>
#include <WidgetRTC.h>
#define ONE_WIRE_BUS 2

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

DeviceAddress ds18b2001 = { 0x28, 0x77, 0x8A, 0x1E, 0x00, 0x00, 0x80, 0xD4 }; // Temp test probe
DeviceAddress ds18b20RA = { 0x28, 0xEF, 0x97, 0x1E, 0x00, 0x00, 0x80, 0x54 }; // Return air probe
DeviceAddress ds18b20SA = { 0x28, 0xF1, 0xAC, 0x1E, 0x00, 0x00, 0x80, 0xE8 }; // Supply air probe

char auth[] = "getFromBlynkApp";

SimpleTimer timer;

WidgetLCD lcd(V5);

WidgetRTC rtc;
BLYNK_ATTACH_WIDGET(rtc, V8);

int blowerPin = 0;  // 3.3V logic source from blower
int offHour, offHour24, onHour, onHour24, offMinute, onMinute, offSecond, onSecond, offMonth, onMonth, offDay, onDay;

void setup()
{
  Serial.begin(9600);
  Blynk.begin(auth, "ssid1", "pw1");
  //Blynk.begin(auth, "ssid2", "pw2");

  //WiFi.softAP("ssid", "pw");
  //WiFi.softAPdisconnect(true);

  sensors.begin();
  // Add more of below as more sensors are added. 9, 10, 11, or 12 bits, corresponding to increments of 0.5°C, 0.25°C, 0.125°C, and 0.0625°C, respectively.
  // Defaults to 12-bit if not set. ~94ms conversion time for 9-bit, to 750ms for 12-bit!
  sensors.setResolution(ds18b20RA, 10);
  sensors.setResolution(ds18b20SA, 10);
  sensors.setResolution(ds18b2001, 10);

  timer.setInterval(1000L, sendTemps); // Temperature sensor polling interval
  timer.setInterval(10000L, sendStatus); // Blower fan status polling interval


  while (Blynk.connect() == false) {
    // Wait until connected
  }
  rtc.begin();
  timer.setInterval(10000L, clockDisplay);
}

void clockDisplay()
{
  BLYNK_LOG("Current time: %02d:%02d:%02d %02d %02d %d",
            hour(), minute(), second(),
            day(), month(), year());
}

void sendTemps()
{
  sensors.requestTemperatures(); // Polls the sensors
  float tempRA = sensors.getTempF(ds18b20RA);
  float tempSA = sensors.getTempF(ds18b20SA);
  float tempTEST = sensors.getTempF(ds18b2001);

  Blynk.virtualWrite(0, tempRA);
  Blynk.virtualWrite(1, tempSA);
  Blynk.virtualWrite(2, tempTEST);
}

void sendStatus()
{
  if (digitalRead(blowerPin) == HIGH) // Runs when blower is OFF
  {
    lcd.clear();
    lcd.print(0, 0, " HVAC OFF since");
    if (offHour < 10)
    {
      lcd.print(1, 1, offHour);
    }
    else
    {
      lcd.print(0, 1, offHour);
    }

    lcd.print(2, 1, ":");

    if (offMinute < 10)
    {
      lcd.print(3, 1, "0");
      lcd.print(4, 1, offMinute);
    }
    else
      lcd.print(3, 1, offMinute);

    if (offHour24 < 12)
    {
      lcd.print(5, 1, "AM on");
    }
    else
    {
      lcd.print(5, 1, "PM on");
    }
    if (offMonth < 10)
    {
      lcd.print(11, 1, offMonth);
      lcd.print(12, 1, "/");
      lcd.print(13, 1, offDay);
    }
    else
    {
      lcd.print(11, 1, offMonth);
      lcd.print(13, 1, "/");
      lcd.print(14, 1, offDay);
    }

    // The following records the time the blower started
    onHour24 = hour();
    onHour = hourFormat12();
    onMinute = minute();
    onSecond = second();
    onMonth = month();
    onDay = day();
  }
  else
  {
    lcd.clear(); // Runs when blower is ON
    lcd.print(0, 0, " HVAC ON since");
    if (onHour < 10)
    {
      lcd.print(1, 1, onHour);
    }
    else
    {
      lcd.print(0, 1, onHour);
    }

    lcd.print(2, 1, ":");

    if (offMinute < 10)
    {
      lcd.print(3, 1, "0");
      lcd.print(4, 1, onMinute);
    }
    else
      lcd.print(3, 1, onMinute);

    if (onHour24 < 12)
    {
      lcd.print(5, 1, "AM on");
    }
    else
    {
      lcd.print(5, 1, "PM on");
    }

    if (onMonth < 10)
    {
      lcd.print(11, 1, onMonth);
      lcd.print(12, 1, "/");
      lcd.print(13, 1, onDay);
    }
    else
    {
      lcd.print(11, 1, onMonth);
      lcd.print(13, 1, "/");
      lcd.print(14, 1, onDay);
    }

    // The following records the time the blower stops
    offHour24 = hour();
    offHour = hourFormat12();
    offMinute = minute();
    offSecond = second();
    offMonth = month();
    offDay = day();
  }
}

void loop()
{
  Blynk.run();
  timer.run();
}
