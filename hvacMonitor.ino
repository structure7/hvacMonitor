#include <SimpleTimer.h>
//#define BLYNK_PRINT Serial    // Comment this out to disable prints and save space
//#define BLYNK_DEBUG
#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <TimeLib.h> // Used by WidgetRTC.h
#include <WidgetRTC.h>
#define ONE_WIRE_BUS 2
#include <ArduinoJson.h>


OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

DeviceAddress ds18b20RA = { 0x28, 0xEF, 0x97, 0x1E, 0x00, 0x00, 0x80, 0x54 }; // Return air probe
DeviceAddress ds18b20SA = { 0x28, 0xF1, 0xAC, 0x1E, 0x00, 0x00, 0x80, 0xE8 }; // Supply air probe

char auth[] = "fromBlynkApp";

SimpleTimer timer;

// The original time entries that didn't like being removed.
#define DELAY_NORMAL    (10)
#define DELAY_ERROR     (10)

//#define WUNDERGROUND "www.mk.com"  // Keep for test/debug
#define WUNDERGROUND "api.wunderground.com"

// HTTP request
const char WUNDERGROUND_REQ[] =
  //"GET /test.json HTTP/1.1\r\n"  // Keep for test/debug
  "GET /api/[myAPIkey]/conditions/q/pws:KAZTEMPE29.json HTTP/1.1\r\n"
  "User-Agent: ESP8266/0.1\r\n"
  "Accept: */*\r\n"
  "Host: " WUNDERGROUND "\r\n"
  "Connection: close\r\n"
  "\r\n";

float temp_f;

WidgetLED led1(V2);
WidgetLCD lcd(V5);
WidgetRTC rtc;
BLYNK_ATTACH_WIDGET(rtc, V8);

int blowerPin = 0;  // 3.3V logic source from blower
int xStop = 1;
int xStart = 1;
int alarmTrigger = 0;
int alarmFor = 0;

unsigned long onNow = now();
unsigned long offNow = now(); // To prevent error on first Tweet after blower starts - NOT WORKING YET

int offHour, offHour24, onHour, onHour24, offMinute, onMinute, offSecond, onSecond, offMonth, onMonth,
    offDay, onDay, runTime, tempSplit, secondsCount, alarmTime;

void setup()
{
  Serial.begin(9600);
  Blynk.begin(auth, "ssid", "pw");

  sensors.begin();
  sensors.setResolution(ds18b20RA, 10);
  sensors.setResolution(ds18b20SA, 10);

  while (Blynk.connect() == false) {
    // Wait until connected
  }
  rtc.begin();

  timer.setInterval(2500L, sendTemps); // Temperature sensor polling interval
  timer.setInterval(2500L, sendLCDstatus); // Blower fan status polling interval
  timer.setInterval(5000L, sendHeartbeat); // Blinks Blynk LED to reflect online status
  timer.setInterval(300000L, sendWU); // 5 minutes between Wunderground API calls.
  timer.setInterval(90000L, sendWUtoBlynk); // 1.5 minutes between API data updates to Blynk.

   static char respBuf[4096];
}

static char respBuf[4096];

bool showWeather(char *json);

void sendWUtoBlynk()
{
    Blynk.virtualWrite(12, temp_f);
}

void sendWU()
{
  // Open socket to WU server port 80
  Serial.print(F("Connecting to "));
  Serial.println(WUNDERGROUND);

  // Use WiFiClient class to create TCP connections (A LITTLE WORRIED HOW THIS WILL WORK WITH BLYNK)
  WiFiClient httpclient;
  const int httpPort = 80;
  if (!httpclient.connect(WUNDERGROUND, httpPort)) {
    Serial.println(F("connection failed"));
    delay(DELAY_ERROR);
    return;
  }

  // This will send the http request to the server
  Serial.print(WUNDERGROUND_REQ);
  httpclient.print(WUNDERGROUND_REQ);
  httpclient.flush();

  // Collect http response headers and content from Weather Underground
  // HTTP headers are discarded.
  // The content is formatted in JSON and is left in respBuf.
  int respLen = 0;
  bool skip_headers = true;
  while (httpclient.connected() || httpclient.available()) {
    if (skip_headers) {
      String aLine = httpclient.readStringUntil('\n');
      //Serial.println(aLine);
      // Blank line denotes end of headers
      if (aLine.length() <= 1) {
        skip_headers = false;
      }
    }
    else {
      int bytesIn;
      bytesIn = httpclient.read((uint8_t *)&respBuf[respLen], sizeof(respBuf) - respLen);
      Serial.print(F("bytesIn ")); Serial.println(bytesIn);
      if (bytesIn > 0) {
        respLen += bytesIn;
        if (respLen > sizeof(respBuf)) respLen = sizeof(respBuf);
      }
      else if (bytesIn < 0) {
        Serial.print(F("read error "));
        Serial.println(bytesIn);
      }
    }
    delay(1);
  }
  httpclient.stop();

  if (respLen >= sizeof(respBuf)) {
    Serial.print(F("respBuf overflow "));
    Serial.println(respLen);
    delay(DELAY_ERROR);
    return;
  }
  // Terminate the C string
  respBuf[respLen++] = '\0';
  Serial.print(F("respLen "));
  Serial.println(respLen);
  //Serial.println(respBuf);


// This part will be removed in favor of the timer feature. Probably remove delay_error and just report ERR.
  if (showWeather(respBuf)) {
    delay(DELAY_NORMAL);
  }
  else {
    delay(DELAY_ERROR);
  }

}

bool showWeather(char *json)
{
  StaticJsonBuffer<3*1024> jsonBuffer;

  // Skip characters until first '{' found
  // Ignore chunked length, if present
  char *jsonstart = strchr(json, '{');
  //Serial.print(F("jsonstart ")); Serial.println(jsonstart);
  if (jsonstart == NULL) {
    Serial.println(F("JSON data missing"));
    return false;
  }
  json = jsonstart;

  // Parse JSON
  JsonObject& root = jsonBuffer.parseObject(json);
  if (!root.success()) {
    Serial.println(F("jsonBuffer.parseObject() failed"));
    return false;
  }

  // Extract weather info from parsed JSON
  JsonObject& current = root["current_observation"];
  temp_f = current["temp_f"];  // Was `const float temp_f = current["temp_f"];`
  Serial.print(temp_f, 1); Serial.print(F(" F "));

  return true;
}

void sendTemps()
{
  sensors.requestTemperatures(); // Polls the sensors
  float tempRA = sensors.getTempF(ds18b20RA);
  float tempSA = sensors.getTempF(ds18b20SA);
  tempSplit = tempRA - tempSA;

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
  led1.off();  // The OFF portion of the LED heartbeat indicator in the Blynk app
}

void sendLCDstatus()
{
  if (digitalRead(blowerPin) == HIGH) // Runs when blower is OFF.
  {
    //lcd.clear();
    lcd.print(0, 0, " HVAC OFF since  ");
    if (offHour < 10)
    {
      lcd.print(0, 1, String(" ") + offHour); // Since lcd.clear() is not used, this space overwrites old data.
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
      lcd.print(13, 1, offDay + String("  ")); // Since lcd.clear() is not used, this space overwrites old data.
    }
    else
    {
      lcd.print(11, 1, offMonth);
      lcd.print(13, 1, "/");
      lcd.print(14, 1, offDay + String("  ")); // Since lcd.clear() is not used, this space overwrites old data.
    }
  }
  else
  {
    //lcd.clear(); // Runs when blower is ON.
    lcd.print(0, 0, "  HVAC ON since  ");

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
    /*
    {
      // Create runTimeMin and runTimeHour variables (look at hours floating point)
      runTimeMin = ( (onNow - offNow) / 60 ); // Still need to make this report 'right' time on first run!
      runTimeHour = ( runTimeMin / 60 );
      if (runTimeMin > 120)
      {
      Blynk.tweet(String("A/C ON after ") + runTimeHour + " hours of inactivity. " + hour() + ":" + minute() + ":" + second() + " " + month() + "/" + day() + "/" + year());
      xStart++;
      }
      else
      {
      Blynk.tweet(String("A/C ON after ") + runTimeMin + " minutes of inactivity. " + hour() + ":" + minute() + ":" + second() + " " + month() + "/" + day() + "/" + year());
      xStart++;
      }
    }
    */
  }
}

void sendHeartbeat()
{
  led1.on(); // The ON portion of the LED heartbeat indicator in the Blynk app
}

void loop() // Typically Blynk tasks should not be placed in void loop(), however these don't run continuously for any reason.
{
  Blynk.run();
  timer.run();

  if (digitalRead(blowerPin) == HIGH) // Runs when blower is OFF.
  {
    // The following records the time the blower started
    onHour24 = hour();
    onHour = hourFormat12();
    onMinute = minute();
    onSecond = second();
    onMonth = month();
    onDay = day();

    if (xStop == 0) // This variable isn't set to zero until the blower runs for the first time after ESP reset.
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

  if (digitalRead(blowerPin) == LOW) // Blower is running.
  {
    secondsCount = (millis() / 1000); // Start the clock that the alarm will reference.
    if (tempSplit > 15) // > 15F is an acceptable split for alarm purposes (usually 20F in real life).
    {
      alarmFor = 0; // Resets alarm counting "latch."
      alarmTime = 0; // Resets 120s alarm.
    }
    else
    {
      if (alarmFor == 0)
      {
        alarmTime = secondsCount + 120; // This sets the 120s alarm (from split out of spec to notification being sent)
        alarmFor++; // Locks out alarm clock reset until alarmFor == 0.
      }
    }
    if (tempSplit <= 15 && secondsCount > alarmTime && alarmFor == 1)
    {
      Blynk.tweet(String("Low split (") + tempSplit + "°F) " + "recorded at " + hour() + ":" + minute() + ":" + second() + " " + month() + "/" + day() + "/" + year());
      Blynk.notify(String("Low HVAC split: ") + tempSplit + "°F");
      alarmFor = 5; // Arbitrary value indicating notification sent and locking out repetitive notifications.
    }
  }
  else
  {
    alarmFor = 0; // Resets alarm counting "latch."
    alarmTime = 0; // Resets 120s alarm.
  }
}
