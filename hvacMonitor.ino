#include <SimpleTimer.h>
//#define BLYNK_PRINT Serial    // Comment this out to disable prints and save space
//#define BLYNK_DEBUG
#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <TimeLib.h>
#include <WidgetRTC.h>
#define ONE_WIRE_BUS 2

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

DeviceAddress ds18b20RA = { 0x28, 0xEF, 0x97, 0x1E, 0x00, 0x00, 0x80, 0x54 }; // Return air probe
DeviceAddress ds18b20SA = { 0x28, 0xF1, 0xAC, 0x1E, 0x00, 0x00, 0x80, 0xE8 }; // Supply air probe

char auth[] = "fromBlynkApp";

SimpleTimer timer;

WidgetLCD lcd(V5);

WidgetRTC rtc;
BLYNK_ATTACH_WIDGET(rtc, V8);

int blowerPin = 0;  // 3.3V logic source from blower
int offHour, offHour24, onHour, onHour24, offMinute, onMinute, offSecond, onSecond, offMonth, onMonth, offDay, onDay;

int xStop = 1;
int xStart = 1;

unsigned long onNow = now();
unsigned long offNow = now(); // To prevent error on first Tweet after blower starts - NOT WORKING YET

int runTime;

void setup()
{
  Serial.begin(9600);
  Blynk.begin(auth, "ssid", "pw");

  sensors.begin();
  sensors.setResolution(ds18b20RA, 10);
  sensors.setResolution(ds18b20SA, 10);

  timer.setInterval(4000L, sendTemps); // Temperature sensor polling interval
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

  // RETURN AIR - Blower pin logic voltage reversed due to high pull required on ESP8266-01 GPIOs at startup. Done with a BJT.
  if (tempRA >= 0 && tempRA <= 120 && digitalRead(blowerPin) == LOW) // If temp 0-120F and blower running...
  {
    Blynk.virtualWrite(0, tempRA); // ...display the temp...
  }
  else if (tempRA >= 0 && tempRA <= 120 && digitalRead(blowerPin) == HIGH) // ...unless it's not running, then...
  {
    Blynk.virtualWrite(0, "OFF"); // ...display OFF, unless...
  }
  else
  {
    Blynk.virtualWrite(0, "ERR"); // ...there's an error, then display ERR.
  }

  //SUPPLY AIR
  if (tempSA >= 0 && tempSA <= 120 && digitalRead(blowerPin) == LOW)
  {
    Blynk.virtualWrite(1, tempSA);
  }
  else if (tempSA >= 0 && tempSA <= 120 && digitalRead(blowerPin) == HIGH)
  {
    Blynk.virtualWrite(1, "OFF");
  }
  else
  {
    Blynk.virtualWrite(1, "ERR");
  }
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

    if (xStop == 0) // This variable isn't set to zero until the blower runs for the first time after ESP reset. LOOK INTO MOVING EVERYTHING ABOVE INTO THIS if TO STOP BLINKING DISPLAY (what happens when app is off?)
    {
      runTime = ( (offNow - onNow) / 60 );
      Blynk.tweet(String("A/C OFF after running ") + runTime + " minutes. " + hour() + ":" + minute() + ":" + second() + " " + month() + "/" + day() + "/" + year());
      xStop++;
    }
    xStart = 0;
    onNow = now();
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

    if (onMinute < 10)
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

    // The following records the time the blower stopped
    offHour24 = hour();
    offHour = hourFormat12();
    offMinute = minute();
    offSecond = second();
    offMonth = month();
    offDay = day();

    if (xStart == 0)
    {
      runTime = ( (onNow - offNow) / 60 ); // Still need to make this report 'right' time on first run!
      Blynk.tweet(String("A/C ON after ") + runTime + " minutes of inactivity. " + hour() + ":" + minute() + ":" + second() + " " + month() + "/" + day() + "/" + year());
      xStart++;
    }
    xStop = 0;
    offNow = now();
  }
}

void loop()
{
  Blynk.run();
  timer.run();
}
