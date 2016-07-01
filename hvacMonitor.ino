#include <SimpleTimer.h>        // Timer library
#define BLYNK_PRINT Serial      // Turn this on to see basic Blynk activities
//#define BLYNK_DEBUG           // Turn this on to see *everything* Blynk is doing... really fills up serial monitor!
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>        // Required for OTA
#include <WiFiUdp.h>            // Required for OTA
#include <ArduinoOTA.h>         // Required for OTA
#include <BlynkSimpleEsp8266.h> // 6/22/16 Rev: Includes edit of this file to expand virtual pins
#include <TimeLib.h>            // Used by WidgetRTC.h
#include <WidgetRTC.h>          // Blynk's RTC
#include <EEPROM.h>             // EEPROM library - keep HVAC total runtime values to survive restarts

#include <OneWire.h>            // Temperature sensor library
#include <DallasTemperature.h>  // Temperature sensor library
#define ONE_WIRE_BUS 13          // WeMos pin D7
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

DeviceAddress ds18b20RA = { 0x28, 0xEF, 0x97, 0x1E, 0x00, 0x00, 0x80, 0x54 }; // Return air probe - Device address found by going to File -> Examples -> DallasTemperature -> oneWireSearch
DeviceAddress ds18b20SA = { 0x28, 0xF1, 0xAC, 0x1E, 0x00, 0x00, 0x80, 0xE8 }; // Supply air probe

char auth[] = "fromBlynkApp"; // HVAC Production
//char auth[] = "fromBlynkApp"; // HVAC Dev
char ssid[] = "ssid";
char pass[] = "pw";

char* host = "data.sparkfun.com";
char* streamId   = "publicKey";
char* privateKey = "privateKey";

SimpleTimer timer;

WidgetLED led1(V2);        // Heartbeat (status)
WidgetRTC rtc;
BLYNK_ATTACH_WIDGET(rtc, V8);
WidgetTerminal terminal(V26);

const int blowerPin = 12;  // WeMos pin D6. Todo: Change blowerPin to coolingPin (also, fanOnlyPin and heatingPin for future).
bool daySet = FALSE;       // Sets the day once after reset and RTC is set correctly.

bool resetFlag = TRUE;
bool startFlag = FALSE;
bool stopFlag = FALSE;

int offHour, offHour24,  // All used to send an HVAC run status string to Blynk's app.
    onHour, onHour24, offMinute, onMinute, offMonth, onMonth, offDay, onDay;

int tempSplit;                    // Difference between return and supply air.
const int tempSplitSetpoint = 20; // Split lower than this value will send alarm notification.
int alarmFor = 0;                 // Provides "latching" for high split alarm counting & functionality. 0 is normal. 1 is prealarm. 5 is alarmed and lockout.
const int splitAlarmTime = 180;   // If a high split persists for this duration (in seconds), notification is sent.
int alarmTime;                    // Seconds that high split alarm has been active.

int todaysAccumRuntimeSec;            // Today's accumulated runtime in seconds - displays in app.
int todaysAccumRuntimeMin;            // Today's accumulated runtime in minutes - displays in app.
int currentCycleSec, currentCycleMin; // If HVAC is running, the duration of the current cycle (non-accumulating).
int yesterdayRuntime;                 // Sum of yesterday's runtime in minutes - displays in app.
int todaysDate;                       // Sets today's date related to things that reset at EOD.
String currentTimeDate;               // Time formatted as "0:00AM on 0/0"
String startTimeDate, stopTimeDate, resetTimeDate;

float tempRA, tempSA;  // Return and supply air temperatures

const int eeIndex = 0;    // This is the EEPROM address location that keeps track of next EEPROM address available for storing a single blower cycle runtime.
int eeCurrent = 1;        // This is the next EEPROM address location that cycle runtime will be stored to.
int eeWBsum;              // This is the sum of all EEPROM addresses (total runtime stored).
int eeTodaysStartsCount;  // Records how many times the unit has started today.

void setup()
{
  Serial.begin(9600);
  WiFi.mode(WIFI_STA);

  //WiFi.softAPdisconnect(true); // Per https://github.com/esp8266/Arduino/issues/676 this turns off AP
  Blynk.begin(auth, ssid, pass);

  sensors.begin();
  sensors.setResolution(ds18b20RA, 10);
  sensors.setResolution(ds18b20SA, 10);

  while (Blynk.connect() == false) {
    // Wait until connected
  }

  // START OTA ROUTINE
  ArduinoOTA.setHostname("esp8266-HVAC");

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("MAC address: ");
  Serial.println(WiFi.macAddress());
  // END OTA ROUTINE

  rtc.begin();

  EEPROM.begin(201); // EEPROM is zero-indexed: means addresses 0 to 200 are available (not 1 to 201 or 0 to 201).

  // START EEPROM WRITEBACK ROUTINE
  if (EEPROM.read(eeIndex) > 1)              // If this is true, than the index has advanced beyond the first position (meaning something was stored there (and maybe higher) to be written back.
  {
    for (int i = 1 ; i < 199 ; i++) {
      eeWBsum += EEPROM.read(i);             // Add up values in addresses 1 through 198 and stick into eeWBsum.

    }
    eeTodaysStartsCount = EEPROM.read(199);    // Reads how many times the unit has started today (if any runs have been recorded).
  }

  todaysAccumRuntimeSec = (eeWBsum * 60);    // Writes seconds of today's accumulated runtime back to variable.
  todaysAccumRuntimeMin = eeWBsum;           // Writes minutes of today's accumulated runtime back to variable (used for app runtime display only).
  yesterdayRuntime = (EEPROM.read(200) * 4); // Writes minutes of yesterday's accumulated runtime back to variable (use for app runtime display only).
  // END EEPROM WRITEBACK ROUTINE

  timer.setInterval(2500L, sendTemps);        // Temperature sensor polling and app display refresh interval.
  timer.setInterval(1234L, sendStatus);       // Unit run status polling interval (for app Current Status display only).
  timer.setInterval(1000L, countRuntime);     // Counts blower runtime for daily accumulation displays.
  timer.setInterval(1000L, totalRuntime);     // Counts blower runtime for daily EEPROM storage.
  timer.setInterval(500L, alarmTimer);        // Track a cycle start/end time for app display.
  timer.setInterval(15432L, sfUpdate);        // ~15 sec run status updates to data.sparkfun.com

  heartbeatOn();
}

void loop()  // To keep Blynk happy, keep Blynk tasks out of the loop.
{
  Blynk.run();
  timer.run();
  ArduinoOTA.handle();
}

void sfUpdate()
{
  if (digitalRead(blowerPin) == LOW)
  {
    int runStatus = 40;

    Serial.print("connecting to ");
    Serial.println(host);

    // Use WiFiClient class to create TCP connections
    WiFiClient client;
    const int httpPort = 80;
    if (!client.connect(host, httpPort)) {
      Serial.println("connection failed");
      return;
    }

    Serial.print("Requesting...");

    // This will send the request to the server
    client.print(String("GET ") + "/input/" + streamId + "?private_key=" + privateKey + "&returntemp=" + tempRA + "&runstatus=" + runStatus + "&supplytemp=" + tempSA + " HTTP/1.1\r\n" +
                 "Host: " + host + "\r\n" +
                 "Connection: close\r\n\r\n");
    unsigned long timeout = millis();
    while (client.available() == 0) {
      if (millis() - timeout > 10000) {
        Serial.println(">>> Client Timeout !");
        client.stop();
        return;
      }
    }

    // Read all the lines of the reply from server and print them to Serial
    while (client.available()) {
      String line = client.readStringUntil('\r');
      Serial.print(line);
    }

    Serial.println();
    Serial.println("closing connection");
  }

  else if (digitalRead(blowerPin) == HIGH)
  {
    int runStatus = NULL;

    Serial.print("connecting to ");
    Serial.println(host);

    // Use WiFiClient class to create TCP connections
    WiFiClient client;
    const int httpPort = 80;
    if (!client.connect(host, httpPort)) {
      Serial.println("connection failed");
      return;
    }

    Serial.print("Requesting...");

    // This will send the request to the server
    client.print(String("GET ") + "/input/" + streamId + "?private_key=" + privateKey + "&returntemp=" + tempRA + "&runstatus=" + runStatus + "&supplytemp=" + tempSA + " HTTP/1.1\r\n" +
                 "Host: " + host + "\r\n" +
                 "Connection: close\r\n\r\n");
    unsigned long timeout = millis();
    while (client.available() == 0) {
      if (millis() - timeout > 5000) {
        Serial.println(">>> Client Timeout !");
        client.stop();
        return;
      }
    }

    // Read all the lines of the reply from server and print them to Serial
    while (client.available()) {
      String line = client.readStringUntil('\r');
      Serial.print(line);
    }

    Serial.println();
    Serial.println("closing connection");
  }
}

BLYNK_WRITE(V27) // App button to report uptime
{
  int pinData = param.asInt();

  if (pinData == 0) // Triggers when button is released only
  {
    long minDur = millis() / 60000L;
    long hourDur = millis() / 3600000L;
    terminal.println(" "); terminal.println(" "); terminal.println(" "); terminal.println(" "); // Will CR work with terminal?
    terminal.println(" "); terminal.println(" "); terminal.println(" "); terminal.println(" ");
    terminal.println("------------ UPTIME REPORT ------------");

    if (minDur < 121)
    {
      terminal.print(String("HVAC: ") + minDur + " mins @ ");
      terminal.println(WiFi.localIP());
    }
    else if (minDur > 120)
    {
      terminal.print(String("HVAC: ") + hourDur + " hrs @ ");
      terminal.println(WiFi.localIP());
    }
    terminal.flush();
  }
}

void heartbeatOn()  // Blinks a virtual LED in the Blynk app to show the ESP is live and reporting.
{
  led1.on();
  timer.setTimeout(2500L, heartbeatOff);
}

void heartbeatOff()
{
  led1.off();  // The OFF portion of the LED heartbeat indicator in the Blynk app
  timer.setTimeout(2500L, heartbeatOn);
}

BLYNK_WRITE(V19) // App button to reset EEPROM 0-200
{
  int pinData = param.asInt();

  if (pinData == 0)
  {
    Serial.println("EEPROM RESET");
    for (int i = 0 ; i < 201 ; i++) {
      EEPROM.write(i, 0);
    }
    EEPROM.write(eeIndex, 1); // Define address 1 as the starting location.
    EEPROM.commit();

    todaysAccumRuntimeSec = 0;  // Also zeros out variables so display is correct.
    todaysAccumRuntimeMin = 0;
    yesterdayRuntime = 0;
    eeTodaysStartsCount = 0;
    eeWBsum = 0;
  }
}

BLYNK_WRITE(V20) // App button to reset hardware
{
  int pinData = param.asInt();

  if (pinData == 0)
  {
    ESP.restart();
  }
}

void sendTemps()
{
  sensors.requestTemperatures();                                            // Polls the sensors
  tempRA = sensors.getTempF(ds18b20RA);
  tempSA = sensors.getTempF(ds18b20SA);
  tempSplit = tempRA - tempSA;

  // RETURN AIR
  if (tempRA >= 0 && tempRA <= 120 && digitalRead(blowerPin) == LOW)        // If temp 0-120F and blower running...
  {
    Blynk.virtualWrite(0, tempRA);                                          // ...display the temp...
  }
  else if (tempRA >= 0 && tempRA <= 120 && digitalRead(blowerPin) == HIGH)  // ...unless it's not running, then...
  {
    Blynk.virtualWrite(0, "OFF");                                           // ...display OFF, unless...
  }
  else
  {
    Blynk.virtualWrite(0, "ERR");                                           // ...there's an error, then display ERR.
  }

  // SUPPLY AIR
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
  if (resetFlag == TRUE && year() != 1970) {                       // Runs once following Arduino reset/start.
    resetTimeDate = currentTimeDate;
    Blynk.virtualWrite(16, String("SYSTEM RESET at ") + resetTimeDate);
  }

  // Purpose: To keep the display showing a system reset state until the HVAC has turned on or off.
  if (digitalRead(blowerPin) == LOW && resetFlag == TRUE) {        // Runs once following Arduino reset/start if fan is ON.
    startFlag = TRUE;                                              // Tells the next set of IFs that fan changed state.
    resetFlag = FALSE;                                             // Locks out this IF and reset IF above.
  }
  else if (digitalRead(blowerPin) == HIGH && resetFlag == TRUE) {  // Runs once following Arduino reset/start if fan is OFF.
    stopFlag = TRUE;
    resetFlag = FALSE;
  }

  // Purpose: To swap between ON and OFF display once per fan state change.
  if (digitalRead(blowerPin) == HIGH && startFlag == TRUE) {          // OFF, but was running
    stopTimeDate = currentTimeDate;                                   // Set off time.
    Blynk.virtualWrite(16, String("HVAC OFF since ") + stopTimeDate); // Write to app.
    stopFlag = TRUE;                                                  // Ready flag when fan turns on.
    startFlag = FALSE;                                                // Keep everything locked out until the next cycle.
  }
  else if (digitalRead(blowerPin) == LOW && stopFlag == TRUE) {    // RUNNING, but was off
    startTimeDate = currentTimeDate;
    Blynk.virtualWrite(16, String("HVAC ON since ") + startTimeDate);
    startFlag = TRUE;
    stopFlag = FALSE;
  }
}

void totalRuntime()
{
  if (digitalRead(blowerPin) == LOW) // If fan is running...
  {
    ++currentCycleSec;                          // accumulate the running time, however...
    currentCycleMin = (currentCycleSec / 60);
  }
  else if (digitalRead(blowerPin) == HIGH && currentCycleSec > 0) // if fan is off but recorded some runtime...
  {
    EEPROM.write(199, ++eeTodaysStartsCount);   // Stores how many times the unit has started today... adds one start.
    EEPROM.commit();

    eeCurrent = EEPROM.read(eeIndex);           // Set eeCurrent as the value of the next available register. (Also, allows display of number of runs to survive reset.)
    EEPROM.write(eeCurrent, currentCycleMin);   // Write runtime to EEPROM
    EEPROM.commit();                            // Make it so  CAN WE JUST COMMIT ONCE? TESSSSSSSSSSSSTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT

    //eeCurrent++;                              // Advance to next EEPROM register for next run
    EEPROM.write(eeIndex, ++eeCurrent);         // Advance to next EEPROM register (for next run) and write that next register to EEPROM
    EEPROM.commit();                            // Make it so

    currentCycleSec = 0;                        // Reset the current cycle clock
    currentCycleMin = 0;
  }
}

void alarmTimer() // Does the timing for the RA/SA split temperature alarm.
{
  if (digitalRead(blowerPin) == LOW) // Blower is running.
  {
    int secondsCount = (millis() / 1000); // Running count that the alarm will reference after the first high split is noticed.

    if (tempSplit > tempSplitSetpoint) // Condition normal. Reset any previous alarm if there was one.
    {
      alarmFor = 0;
      alarmTime = 0;
    }
    else // Condition abnormal.
    {
      if (alarmFor == 0)
      {
        alarmTime = secondsCount + splitAlarmTime; // This sets the alarm time (from split out of spec to notification being sent)
        ++alarmFor; // Locks out alarm clock reset until alarmFor == 0.
      }
    }
    if (tempSplit <= tempSplitSetpoint && secondsCount > alarmTime && alarmFor == 1)
    {
      Blynk.tweet(String("Low split (") + tempSplit + "°F) " + "recorded at " + hour() + ":" + minute() + ":" + second() + " " + month() + "/" + day() + "/" + year());
      Blynk.notify(String("Low HVAC split: ") + tempSplit + "°F. Call Wolfgang's"); // ALT + 248 = °
      alarmFor = 5; // Arbitrary value indicating notification sent and locking out repetitive notifications.
    }
  }
  else
  {
    alarmFor = 0; // Resets alarm counting "latch."
    alarmTime = 0; // Resets splitAlarmTime alarm.
  }
}

void countRuntime()
{
  if (year() != 1970) // Doesn't start until RTC is set correctly.
  {

    // Below gives me leading zeros on minutes and AM/PM.
    if (minute() > 9 && hour() > 11) {
      currentTimeDate = String(hourFormat12()) + ":" + minute() + "PM on " + month() + "/" + day();
    }
    else if (minute() < 10 && hour() > 11) {
      currentTimeDate = String(hourFormat12()) + ":0" + minute() + "PM on " + month() + "/" + day();
    }
    else if (minute() > 9 && hour() < 12) {
      currentTimeDate = String(hourFormat12()) + ":" + minute() + "AM on " + month() + "/" + day();
    }
    else if (minute() < 10 && hour() < 12) {
      currentTimeDate = String(hourFormat12()) + ":0" + minute() + "AM on " + month() + "/" + day();
    }

    if (daySet == FALSE) { // Sets the date (once per hardware restart) now that RTC is correct.
      todaysDate = day();
      daySet = TRUE;
    }

    if (digitalRead(blowerPin) == LOW && todaysDate == day()) // Accumulates seconds unit is running today.
    {
      ++todaysAccumRuntimeSec;
      todaysAccumRuntimeMin = (todaysAccumRuntimeSec / 60);
    }
    else if (todaysDate != day())
    {
      yesterdayRuntime = todaysAccumRuntimeMin; // Moves today's runtime to yesterday for the app display.
      todaysAccumRuntimeSec = 0; // Reset today's sec timer.
      todaysAccumRuntimeMin = 0; // Reset today's min timer.
      eeTodaysStartsCount = 0;   // Reset how many times unit has started today.
      todaysDate = day();

      // Resets EEPROM at the end of the day (except for yesterday's runtime)
      for (int i = 0 ; i < 200 ; i++) {
        EEPROM.write(i, 0);
      }
      EEPROM.write(eeIndex, 1);                  // Define address 1 as the starting location.
      EEPROM.write(200, (yesterdayRuntime / 4)); // Write yesterday's runtime to EEPROM
      EEPROM.write(199, 0);                      // Resets how many times the unit has started today.
      EEPROM.commit();
    }

    if (yesterdayRuntime < 1)
    {
      Blynk.virtualWrite(14, "None");
    }
    else
    {
      Blynk.virtualWrite(14, String(yesterdayRuntime) + " minutes");
    }

    if (todaysAccumRuntimeSec < 1)
    {
      Blynk.virtualWrite(15, "None");
    }
    else if (todaysAccumRuntimeMin > 0 && eeCurrent < 1)
    {
      Blynk.virtualWrite(15, String(todaysAccumRuntimeMin) + " minutes");
    }
    else if (todaysAccumRuntimeMin > 0 && eeCurrent > 0)
    {
      Blynk.virtualWrite(15, String(todaysAccumRuntimeMin) + " mins (" + (eeTodaysStartsCount) + " runs)");
    }
  }
}
