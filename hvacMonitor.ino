#include <SimpleTimer.h>
#define BLYNK_PRINT Serial    // Comment this out to disable prints and save space
//#define BLYNK_DEBUG
#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <TimeLib.h> // Used by WidgetRTC.h
#include <WidgetRTC.h>
#define ONE_WIRE_BUS 2
#include <ArduinoJson.h>
#include <EEPROM.h>

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
  "GET /api/myApiKey/conditions/q/pws:KAZTEMPE29.json HTTP/1.1\r\n"
  "User-Agent: ESP8266/0.1\r\n"
  "Accept: */*\r\n"
  "Host: " WUNDERGROUND "\r\n"
  "Connection: close\r\n"
  "\r\n";

float temp_f;

WidgetLED led1(V2);
WidgetLED led2(V28); // eeprom write
WidgetRTC rtc;
BLYNK_ATTACH_WIDGET(rtc, V8);

int blowerPin = 0;  // 3.3V logic source from blower
int xStop = 1;
int xStart = 1;
int alarmTrigger = 0;
int alarmFor = 0;
int xFirstRun = 0;
int resetTattle = 0;


unsigned long onNow = now();
unsigned long offNow = now(); // To prevent error on first Tweet after blower starts - NOT WORKING YET

int offHour, offHour24, onHour, onHour24, offMinute, onMinute, offSecond, onSecond, offMonth, onMonth,
    offDay, onDay, runTime, tempSplit, secondsCount, alarmTime;

int currentRuntimeSec; // Sum of today's blower runtime in seconds
int currentOfftimeSec = 0;
int currentRuntimeMin; // Sum of today's blower runtime in minutes
int currentRuntimeStint; // Current (last) duration of a blower fan run "cycle/session".
int currentRuntimeStintMin; // Current (last) duration of a blower fan run "cycle/session".
int yesterdayRuntime; // Sum of yesterday's blower runtime in minutes
int totalRuntimeLatch = 0;

int dayCountLatch = 0;
int todaysDate, yesterdaysDate, yesterdaysMonth;
int msgLatch = 0;

// For EEPROM
int eeIndex = 0; // This is the EEPROM address location that keeps track of next EEPROM address available for storing a blower runtime.
int eeCurrent;
int eeWBsum;

void setup()
{
  Serial.begin(9600);
  Blynk.begin(auth, "ssid", "pw");

  EEPROM.begin(200);

  /*
    //*************************************RUN THIS ONCE THEN COMMENT OUT TO CLEAR OUT EEPROM****************************
    for (int i = 0 ; i < 201 ; i++) {
      EEPROM.write(i, 0);
    }
    EEPROM.write(eeIndex, 1); // Defined address 1 as the starting location.
    EEPROM.commit();
    //*************************************RUN THIS ONCE THEN COMMENT OUT TO CLEAR OUT EEPROM****************************
  */

  sensors.begin();
  sensors.setResolution(ds18b20RA, 10);
  sensors.setResolution(ds18b20SA, 10);

  while (Blynk.connect() == false) {
    // Wait until connected
  }

  // EEPROM WRITEBACK AFTER RESET
  if (EEPROM.read(eeIndex) > 1) // If this is true, than the index has advanced beyond the first position (meaning something was stored there (and maybe higher) to be written back.
  {
    for (int i = 1 ; i < 201 ; i++) {
      eeWBsum += EEPROM.read(i);
    }
    //Serial.println(String(eeWBsum) + " minutes reported from EEPROM after reset.");
    //Blynk.virtualWrite(17, eeWBsum);
    //Serial.println(String(EEPROM.read(eeIndex)) + " is the next index location to write to.");
  }
  Blynk.virtualWrite(17, eeWBsum);
  Blynk.virtualWrite(18, EEPROM.read(eeIndex));
  
  rtc.begin();

  timer.setInterval(2500L, sendTemps); // Temperature sensor polling interval
  timer.setInterval(2500L, sendBlowerStatus); // Blower fan status polling interval
  timer.setInterval(5000L, sendHeartbeat); // Blinks Blynk LED to reflect online status
  timer.setInterval(300000L, sendWU); // 5 minutes between Wunderground API calls.
  timer.setInterval(330000L, sendWUtoBlynk); // 5ish minutes between API data updates to Blynk.
  timer.setInterval(1000L, countRuntime);  // Counts blower runtime for daily accumulation displays.
  timer.setInterval(1000L, totalRuntime);  // Counts blower runtime for daily EEPROM storage.

}

static char respBuf[4096];

bool showWeather(char *json);

void sendWUtoBlynk()
{
  // Intended to screen out errors from Wunderground API
  if (temp_f > 10)
  {
    Blynk.virtualWrite(12, temp_f);
  }
  else
  {
    Blynk.virtualWrite(12, "ERR");
    Blynk.tweet(String("WU API error reporting a temp value of ") + temp_f + " at " + hour() + ":" + minute() + ":" + second() + " " + month() + "/" + day() + "/" + year());
  }
}

void sendWU()
{
  // Open socket to WU server port 80
  Serial.print(F("Connecting to "));
  Serial.println(WUNDERGROUND);

  // Use WiFiClient class to create TCP connections
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

  // Kept this because I haven't quite figured out how to keep ESP from crashing without it.
  if (showWeather(respBuf)) {
    delay(DELAY_NORMAL);
  }
  else {
    delay(DELAY_ERROR);
  }

}

bool showWeather(char *json)
{
  StaticJsonBuffer<3 * 1024> jsonBuffer;

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
  //Serial.print(temp_f, 1); Serial.print(F(" F "));

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

void sendBlowerStatus()
{
  if (digitalRead(blowerPin) == HIGH && resetTattle == 0)
  {
    Blynk.virtualWrite(16, String("SYSTEM RESET at ") + hour() + ":" + minute());
  }
  else if (digitalRead(blowerPin) == LOW && resetTattle == 0)
  {
    Blynk.virtualWrite(16, String("SYSTEM RESET at ") + hour() + ":" + minute());
  }
  else if (digitalRead(blowerPin) == HIGH && resetTattle == 1) // Runs when blower is OFF.
  {
    if (hour() < 12 && minute() > 9)
    {
      Blynk.virtualWrite(16, String("HVAC OFF since ") + offHour + ":" + offMinute + "AM on " + offMonth + "/" + offDay);
    }
    else if (hour() < 12 && minute() < 10)
    {
      Blynk.virtualWrite(16, String("HVAC OFF since ") + offHour + ":" + "0" + offMinute + "AM on " + offMonth + "/" + offDay);
    }
    else if (hour() > 11 && minute() > 9)
    {
      Blynk.virtualWrite(16, String("HVAC OFF since ") + offHour + ":" + offMinute + "PM on " + offMonth + "/" + offDay);
    }
    else if (hour() > 11 && minute() < 10)
    {
      Blynk.virtualWrite(16, String("HVAC OFF since ") + offHour + ":" + "0" + offMinute + "PM on " + offMonth + "/" + offDay);
    }
  }
  else if (digitalRead(blowerPin) == LOW && resetTattle == 1)
  {
    if (hour() < 12 && minute() > 9)
    {
      Blynk.virtualWrite(16, String("HVAC ON since ") + onHour + ":" + onMinute + "AM on " + onMonth + "/" + onDay);
    }
    else if (hour() < 12 && minute() < 10)
    {
      Blynk.virtualWrite(16, String("HVAC ON since ") + onHour + ":" + "0" + onMinute + "AM on " + onMonth + "/" + onDay);
    }
    else if (hour() > 11 && minute() > 9)
    {
      Blynk.virtualWrite(16, String("HVAC ON since ") + onHour + ":" + onMinute + "PM on " + onMonth + "/" + onDay);
    }
    else if (hour() > 11 && minute() < 10)
    {
      Blynk.virtualWrite(16, String("HVAC ON since ") + onHour + ":" + "0" + onMinute + "PM on " + onMonth + "/" + onDay);
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

void sendHeartbeat()  // Blinks a virtual LED in the Blynk app to show the ESP is live and reporting.
{
  led1.on();
}

void totalRuntime() // Counts the current blower run session.
{
  if (digitalRead(blowerPin) == HIGH && currentRuntimeStint == 0) // Runs ONCE after reset if fan is OFF, or returns to OFF after being ON.
  {
    totalRuntimeLatch = 0;
  }
  else if (digitalRead(blowerPin) == LOW && totalRuntimeLatch == 0) // Runs while blower is ON (after reset or normal operation).
  {
    currentRuntimeStint++;
  }
  else if (digitalRead(blowerPin) == HIGH && currentRuntimeStint > 0) // Runs once after blower turns OFF (only after running and logging some runtime).
  {
    // INTENT IS TO ONLY READ FROM THIS IN THE EVENT OF A CHIP RESET

    currentRuntimeStintMin = (currentRuntimeStint / 60);
    eeCurrent = EEPROM.read(eeIndex);
    EEPROM.write(eeCurrent, currentRuntimeStintMin);
    EEPROM.commit();
    eeCurrent++;
    EEPROM.write(eeIndex, eeCurrent);
    EEPROM.commit();
    Blynk.virtualWrite(18, eeCurrent);
    currentRuntimeStint = 0;
  }

}

void loop() // Typically Blynk tasks should not be placed in void loop(), however none of these (Blynk) tasks run continuously for any reason.
{
  Blynk.run();
  timer.run();

  if (digitalRead(blowerPin) == HIGH) // Runs when blower is OFF.
  {
    // The following records the time the blower started.
    onHour24 = hour();
    onHour = hourFormat12();
    onMinute = minute();
    onSecond = second();
    onMonth = month();
    onDay = day();

    if (xStop == 0) // This variable isn't set to zero until the blower runs for the first time after ESP reset.
    {
      runTime = ( (offNow - onNow) / 60 );

      //Blynk.tweet(String("A/C OFF after running ") + runTime + " minutes. " + hour() + ":" + minute() + ":" + second() + " " + month() + "/" + day() + "/" + year());
      xStop++;
      resetTattle = 1;
    }
    xStart = 0;
    onNow = now();
  }
  else
  {
    // The following records the time the blower stopped.
    offHour24 = hour();
    offHour = hourFormat12();
    offMinute = minute();
    offSecond = second();
    offMonth = month();
    offDay = day();

    if (xStart == 0)
    {
      runTime = ( (onNow - offNow) / 60 ); // Still need to make this report 'right' time on first run!

      // After ESP startup (first run) this displays a message so RTC error doesn't mess with clock.
      if (xFirstRun == 0)
      {
        xFirstRun++;
        xStart++;
        resetTattle = 1;
      }
      else
      {
        //Blynk.tweet(String("A/C ON after ") + runTime + " minutes of inactivity. " + hour() + ":" + minute() + ":" + second() + " " + month() + "/" + day() + "/" + year());
        xStart++;
      }
    }
    xStop = 0;
    offNow = now();
  }

  // Does the timing for the RA/SA split temperature alarm/notification.
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
      //terminal.println(String("Low HVAC split: ") + tempSplit + "°F");
      alarmFor = 5; // Arbitrary value indicating notification sent and locking out repetitive notifications.
    }
  }
  else
  {
    alarmFor = 0; // Resets alarm counting "latch."
    alarmTime = 0; // Resets 120s alarm.
  }
}

void countRuntime()
{
  // This makes sure the right numerical date for yesterday is shown for the first of any month.
  if (day() == 1)
  {
    if (month() == 3) // The date before 3/1 is 29 (2/29), and so on below.
    {
      yesterdaysDate = 29;
      yesterdaysMonth = month() - 1;
    }
    else if (month() == 5 || month() == 7 || month() == 10 || month() == 12)
    {
      yesterdaysDate = 30;
      yesterdaysMonth = month() - 1;
    }
    else if (month() == 2 || month() == 4 || month() == 6 || month() == 8 || month() == 9 || month() == 11)
    {
      yesterdaysDate = 31;
      yesterdaysMonth = month() - 1;
    }
    else if (month() == 1)
    {
      yesterdaysDate = 31;
      yesterdaysMonth = 12;
    }
  }
  else
  {
    yesterdaysDate = day() - 1; // Subtracts 1 day to get to yesterday unless it's the first of the month (see above)
    yesterdaysMonth = month();
  }

  // Sets the date once after reset/boot up, or when the date actually changes.
  if (dayCountLatch == 0)
  {
    todaysDate = day();
    dayCountLatch++;
  }

  // Runs today's blower timer when the blower starts.
  if (digitalRead(blowerPin) == LOW && todaysDate == day())
  {
    currentRuntimeSec++;
  }
  // Resets the timer on the next day if the blower is running.
  else if (digitalRead(blowerPin) == LOW && todaysDate != day())
  {
    Blynk.tweet(String("On ") + yesterdaysMonth + "/" + yesterdaysDate + "/" + year() + " the A/C ran for " + currentRuntimeMin + " minutes total."); // Tweet total runtime.
    yesterdayRuntime = currentRuntimeMin; // Moves today's runtime to yesterday for the app display.
    dayCountLatch = 0; // Allows today's date to be reset.
    currentRuntimeSec = 0; // Reset today's sec timer.
    currentRuntimeMin = 0; // Reset today's min timer.

    // Resets EEPROM at the end of the day
    for (int i = 0 ; i < 201 ; i++) {
      EEPROM.write(i, 0);
    }
    EEPROM.write(eeIndex, 1); // Defined address 1 as the starting location.
    EEPROM.commit();
  }
  // Resets the timer on the next day if the blower isn't running.
  else if (digitalRead(blowerPin) == HIGH && todaysDate != day())
  {
    Blynk.tweet(String("On ") + yesterdaysMonth + "/" + yesterdaysDate + "/" + year() + " the A/C ran for " + currentRuntimeMin + " minutes total."); // Tweet total runtime.
    yesterdayRuntime = currentRuntimeMin;
    dayCountLatch = 0;
    currentRuntimeSec = 0;
    currentRuntimeMin = 0;

    // Resets EEPROM at the end of the day
    for (int i = 0 ; i < 201 ; i++) {
      EEPROM.write(i, 0);
    }
    EEPROM.write(eeIndex, 1); // Defined address 1 as the starting location.
    EEPROM.commit();
  }

  currentRuntimeMin = (currentRuntimeSec / 60);
  Blynk.virtualWrite(14, String(yesterdayRuntime) + " minutes");
  Blynk.virtualWrite(15, String(currentRuntimeMin) + " minutes");
}
