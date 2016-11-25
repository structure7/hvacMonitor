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

#include <OneWire.h>            // Temperature sensor library
#include <DallasTemperature.h>  // Temperature sensor library
#define ONE_WIRE_BUS 12         // WeMos pin D6
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

DeviceAddress ds18b20RA = { 0x28, 0xEF, 0x97, 0x1E, 0x00, 0x00, 0x80, 0x54 }; // Return air probe - Device address found by going to File -> Examples -> DallasTemperature -> oneWireSearch
DeviceAddress ds18b20SA = { 0x28, 0xF1, 0xAC, 0x1E, 0x00, 0x00, 0x80, 0xE8 }; // Supply air probe

char auth[] = "fromBlynkApp";
char ssid[] = "ssid";
char pass[] = "pw";

char myEmail[] = "email";

// All sparkfun updates now handled by Blynk's WebHook widget
//char* host = "data.sparkfun.com";
//char* streamId   = "publicKey";
//char* privateKey = "privateKey";

SimpleTimer timer;

WidgetRTC rtc;
BLYNK_ATTACH_WIDGET(rtc, V8);
WidgetTerminal terminal(V26);

const int blowerPin = 13;  // WeMos pin D7.
bool daySet = FALSE;       // Sets the day once after reset and RTC is set correctly.

bool runOnce = TRUE;
bool resetFlag = TRUE;        // TRUE when the hardware has been reset.
bool startFlag = FALSE;       // TRUE when the A/C has started.
bool stopFlag = FALSE;        // TRUE when the A/C has stopped.
bool sensorFailFlag = FALSE;  // TRUE when RA or SA temp sensor are returning bad values.

int offHour, offHour24,  // All used to send an HVAC run status string to Blynk's app.
    onHour, onHour24, offMinute, onMinute, offMonth, onMonth, offDay, onDay;

int tempSplit;                    // Difference between return and supply air.
const int tempSplitSetpoint = 20; // Split lower than this value will send alarm notification.
int alarmFor = 0;                 // Provides "latching" for high split alarm counting & functionality. 0 is normal. 1 is prealarm. 5 is alarmed and lockout.
const int splitAlarmTime = 180;   // If a high split persists for this duration (in seconds), notification is sent.
int alarmTime;                    // Seconds that high split alarm has been active.

long todaysAccumRuntimeSec;           // Today's accumulated runtime in seconds - displayed in app as minutes.
int currentCycleSec, currentCycleMin; // If HVAC is running, the duration of the current cycle (non-accumulating).
int yesterdayRuntime;                 // Sum of yesterday's runtime in minutes - displays in app.
int hvacTodaysStartCount;             // Records how many times the unit has started today.
int todaysDate;                       // Sets today's date related to things that reset at EOD.
String currentTimeDate;               // Time formatted as "0:00AM on 0/0"
String startTimeDate, stopTimeDate, resetTimeDate;
int runStatus;                        // Used for graphing. 40 == running. NULL == off.

long currentFilterSec;                // Filter age based on unit runtime in seconds (stored in vPin). Seconds used for finer resolution.
long currentFilterHours;              // Filter age based on unit runtime in hours.
long timeFilterChanged;               // Unix time that filter was last changed.
bool filterConfirmFlag = FALSE;       // Used to enter/exit the filter change mode.
const int filterChangeHours = 300L;   // Duration in hours between filter changes. Up for debate and experimentation!
bool changeFilterAlarm = FALSE;       // TRUE if filter needs to be changed, until reset.

double tempRA, tempSA;                // Return and supply air temperatures

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
  ArduinoOTA.setHostname("esp8266-HVAC");     // Name that is displayed in the Arduino IDE.

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
  //setSyncInterval(120);

  timer.setInterval(2500L, sendTemps);        // Temperature sensor polling and app display refresh interval.
  timer.setInterval(1234L, sendStatus);       // Unit run status polling interval (for app Current Status display only).
  timer.setInterval(1000L, countRuntime);     // Counts blower runtime for daily accumulation displays.
  timer.setInterval(1000L, totalRuntime);     // Counts blower runtime.
  timer.setInterval(1000L, setClockTime);     // Creates a current time with leading zeros.
  timer.setInterval(500L, alarmTimer);        // Track a cycle start/end time for app display.
  timer.setInterval(15432L, sfWebhook);       // ~15 sec run status updates to data.sparkfun.com.
  //timer.setInterval(30000L, debugVpins);
  timer.setTimeout(100, vsync1);              // Syncs back vPins to survive hardware reset.
  timer.setTimeout(5000, vsync2);             // Syncs back vPins to survive hardware reset.
}

void debugVpins()
{
  Serial.println(String("V110 local variable (hvacTodaysStartCount) is: ") + hvacTodaysStartCount);
  Serial.println(String("V111 local variable (todaysAccumRuntimeSec) is: ") + todaysAccumRuntimeSec);
  Serial.println(String("V112 local variable (yesterdayRuntime) is: ") + yesterdayRuntime);
  Serial.println(String("V11 local variable (currentFilterSec) is: ") + currentFilterSec);
}

void loop()  // To keep Blynk happy, keep most tasks out of the loop.
{
  Blynk.run();
  timer.run();
  ArduinoOTA.handle();
}

void vsync1()
{
  Blynk.syncVirtual(V110);    // hvacTodaysStartCount
  Blynk.syncVirtual(V111);    // todaysAccumRuntimeSec
}

void vsync2()
{
  Blynk.syncVirtual(V112);    // yesterdayRuntime
  Blynk.syncVirtual(V11);     // currentFilterSec
}

BLYNK_WRITE(V110)
{
  hvacTodaysStartCount = param.asInt();
}

BLYNK_WRITE(V111)
{
  todaysAccumRuntimeSec = param.asLong();
}

BLYNK_WRITE(V112)
{
  yesterdayRuntime = param.asInt();
}

BLYNK_WRITE(V11)
{
  currentFilterSec = param.asLong();
}

void sfWebhook() {
  if (digitalRead(blowerPin) == LOW)        // If HVAC blower motor is running...
  {
    runStatus = 40;
  }
  else if (digitalRead(blowerPin) == HIGH)  // If HVAC blower motor if off...
  {
    runStatus = NULL;
  }

  Blynk.virtualWrite(67, String("returntemp=") + tempRA + "&runstatus=" + runStatus + "&supplytemp=" + tempSA);

  // The following changes the color of Time Until Filter Change in app... placed here because the timing.
  if (filterChangeHours - (currentFilterSec / 3600) > 24 )
  {
    Blynk.setProperty(V10, "color", "#23C48E");   // Green
  }
  else if (filterChangeHours - (currentFilterSec / 3600) < 24 &&  filterChangeHours - (currentFilterSec / 3600) > 8)
  {
    Blynk.setProperty(V10, "color", "#ED9D00");   // Yellow
  }
  else if (filterChangeHours - (currentFilterSec / 3600) < 8)
  {
    Blynk.setProperty(V10, "color", "#D3435C");   // Red
  }
}

BLYNK_WRITE(V19)              // App button to display all terminal commands available
{
  int pinData = param.asInt();

  if (pinData == 0)
  {
    Blynk.setProperty(V16, "label", "Current Status:                    Hi/Lo temps above are last 24h");
  }

  if (pinData == 1)
  {
    Blynk.setProperty(V16, "label", "Current Status:                    Hi/Lo temps above are since midnight");
  }
}

BLYNK_WRITE(V18)              // App button to display all terminal commands available
{
  int pinData = param.asInt();

  if (pinData == 0) // Triggers when button is released only
  {
    terminal.println(" "); terminal.println(" "); terminal.println(" "); terminal.println(" ");
    terminal.println(" "); terminal.println(" "); terminal.println(" "); terminal.println(" ");
    terminal.println("  --------- AVAILABLE COMMANDS ----------");
    terminal.println(" ");
    terminal.println(" filter : HVAC filter menu");
    terminal.println(" WU : Change WU API station");
    terminal.println(" ");
    terminal.flush();
  }
}

BLYNK_WRITE(V27) // App button to report uptime
{
  int pinData = param.asInt();

  if (pinData == 0) // Triggers when button is released only
  {
    long minDur = millis() / 60000L;
    long hourDur = millis() / 3600000L;
    terminal.println(" "); terminal.println(" "); terminal.println(" "); terminal.println(" ");
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

BLYNK_WRITE(V26)
{
  if (String("FILTER") == param.asStr() || String("filter") == param.asStr()) {
    terminal.println(""); terminal.println("");
    terminal.println("       ~~ A/C Filter Status Mode ~~"); terminal.println(" ");

    if ( (filterChangeHours - currentFilterHours ) > 0) {
      terminal.print(filterChangeHours - currentFilterHours);
      terminal.println(" hours until next filter change (last");
      terminal.print("filter change was ");
      terminal.println(String("") + month(timeFilterChanged) + "/" + day(timeFilterChanged) + "/" + year(timeFilterChanged) + ")."); terminal.println("");    // More on this at http://www.pjrc.com/teensy/td_libs_Time.html
      terminal.println("Type 'f' if filter was just changed with");
      terminal.print("a 20x25x1 1085 microparticle/MERV 11.");
    }
    else {
      terminal.print("Filter change is ");
      terminal.print( -1 * (filterChangeHours - currentFilterHours) );
      terminal.println(" hrs overdue.");
      terminal.print("Last filter change was ");
      terminal.println(String("") + month(timeFilterChanged) + "/" + day(timeFilterChanged) + "/" + year(timeFilterChanged) + ")."); terminal.println("");    // More on this at http://www.pjrc.com/teensy/td_libs_Time.html
      terminal.println("Type 'f' if filter was just changed with");
      terminal.print("a 20x25x1 1085 microparticle/MERV 11.");
    }
  }

  if ( (String("F") == param.asStr() || String("f") == param.asStr()) && filterConfirmFlag == FALSE) {
    filterConfirmFlag = TRUE;
    terminal.println(" "); terminal.println("Enter 'y' to confirm filter was changed.");
    terminal.println("Enter 'n' to cancel.");
    terminal.println(" ");
  }
  else if ( (String("y") == param.asStr() || String("Y") == param.asStr()) && filterConfirmFlag == TRUE) {
    currentFilterHours = 0;
    currentFilterSec = 0;
    Blynk.virtualWrite(11, currentFilterSec);
    timeFilterChanged = now();
    Blynk.virtualWrite(17, timeFilterChanged);
    filterConfirmFlag = FALSE;
    Blynk.virtualWrite(10, String( filterChangeHours - currentFilterHours ) + " hours");
    terminal.println(" "); terminal.println("Filter change acknowledged!");
    changeFilterAlarm = FALSE;
  }
  else if ( (String("n") == param.asStr() || String("N") == param.asStr()) && filterConfirmFlag == TRUE) {
    filterConfirmFlag = FALSE;
    terminal.println(" "); terminal.println("Filter change cancelled.");
  }

  terminal.flush();
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
    Blynk.setProperty(V16, "color", "#D3435C");
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
    Blynk.setProperty(V0, "color", "#808080");
    Blynk.setProperty(V1, "color", "#808080");
    Blynk.setProperty(V16, "color", "#808080");

    if ( (filterChangeHours - currentFilterHours ) <= 0 && changeFilterAlarm == FALSE) {
      Blynk.email(myEmail, "CHANGE A/C FILTER", "Change the A/C filter (20x25x1 1085 microparticle/MERV 11) and reset the timer.");
      changeFilterAlarm = TRUE;
    }

    stopFlag = TRUE;                                                  // Ready flag when fan turns on.
    startFlag = FALSE;                                                // Keep everything locked out until the next cycle.
  }
  else if (digitalRead(blowerPin) == LOW && stopFlag == TRUE) {    // RUNNING, but was off
    startTimeDate = currentTimeDate;
    Blynk.virtualWrite(16, String("HVAC ON since ") + startTimeDate);
    Blynk.setProperty(V0, "color", "#D3435C");  // return air text turns red, and...
    Blynk.setProperty(V1, "color", "#5F7CD8");
    Blynk.setProperty(V16, "color", "#23C48E");

    startFlag = TRUE;
    stopFlag = FALSE;
  }
}

void totalRuntime()
{
  if (digitalRead(blowerPin) == LOW) // If fan is running...
  {
    ++currentCycleSec;               // accumulate the running time, however...
    currentCycleMin = (currentCycleSec / 60);
  }
  else if (digitalRead(blowerPin) == HIGH && currentCycleSec > 0) // if fan is off but recorded some runtime...
  {
    Blynk.virtualWrite(110, ++hvacTodaysStartCount);              // Stores how many times the unit has started today... adds one start.

    currentCycleSec = 0;                                          // Reset the current cycle clock
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
    if (tempSplit <= tempSplitSetpoint && secondsCount > alarmTime && alarmFor == 1 && (tempRA > 0 && tempSA > 0))
    {
      Blynk.tweet(String("Low split (") + tempSplit + "°F) " + "recorded at " + hour() + ":" + minute() + ":" + second() + " " + month() + "/" + day() + "/" + year());
      Blynk.notify(String("Low HVAC split: ") + tempSplit + "°F. Call Wolfgang's"); // ALT + 248 = °
      alarmFor = 5; // Arbitrary value indicating notification sent and locking out repetitive notifications.
    }
    else if (tempSplit <= tempSplitSetpoint && secondsCount > alarmTime && alarmFor == 1 && (tempRA <= 0 && tempSA <= 0) && sensorFailFlag == FALSE)
    {
      Blynk.notify("Multiple temp sensor errors recorded. Please check.");
      sensorFailFlag = TRUE;
    }
  }
  else
  {
    alarmFor = 0;     // Resets alarm counting "latch."
    alarmTime = 0;    // Resets splitAlarmTime alarm.
  }
}

void setClockTime()
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
  }
}

void countRuntime()
{
  if (year() != 1970) // Doesn't start until RTC is set correctly.
  {

    if (daySet == FALSE) {              // Sets the date (once per hardware restart) now that RTC is correct.
      todaysDate = day();
      daySet = TRUE;
    }

    if (digitalRead(blowerPin) == LOW && todaysDate == day()) // Accumulates seconds unit is running today.
    {
      ++todaysAccumRuntimeSec;                          // Counts today's AC runtime in seconds.
      Blynk.virtualWrite(111, todaysAccumRuntimeSec);
      ++currentFilterSec;                               // Counts how many seconds filter is used based on AC runtime.
      Blynk.virtualWrite(11, currentFilterSec);
      currentFilterHours = (currentFilterSec / 3600);   // Converts those seconds to hours for display and other uses.
    }
    else if (todaysDate != day())
    {
      yesterdayRuntime = (todaysAccumRuntimeSec / 60);  // Moves today's runtime to yesterday for the app display.
      Blynk.virtualWrite(112, yesterdayRuntime);        // Record yesterday's runtime to vPin.
      todaysAccumRuntimeSec = 0;                        // Reset today's sec timer.
      Blynk.virtualWrite(111, todaysAccumRuntimeSec);
      hvacTodaysStartCount = 0;                         // Reset how many times unit has started today.
      todaysDate = day();
    }


    if (yesterdayRuntime < 1)               // Displays yesterday's runtime in app, or 'None' is there's none.
    {
      Blynk.virtualWrite(14, "None");
    }
    else
    {
      Blynk.virtualWrite(14, String(yesterdayRuntime) + " minutes");
    }

    if (todaysAccumRuntimeSec == 0)          // Displays today's runtime in app, or 'None' is there's none.
    {
      Blynk.virtualWrite(15, "None");
    }
    else if (todaysAccumRuntimeSec > 0 && hvacTodaysStartCount == 0)
    {
      Blynk.virtualWrite(15, String(todaysAccumRuntimeSec / 60) + " minutes");
    }
    else if (todaysAccumRuntimeSec > 0 && hvacTodaysStartCount > 0)
    {
      Blynk.virtualWrite(15, String(todaysAccumRuntimeSec / 60) + " mins (" + (hvacTodaysStartCount) + " runs)");
    }
  }

  Blynk.virtualWrite(10, String( filterChangeHours - (currentFilterSec / 3600) ) + " hours"); // Displays how many hours are left on the filter.

  if (second() >= 0 && second() <= 5)     // Used to monitor uptime. All devices report minute. Another device confirms all reported minutes match.
  {
    Blynk.virtualWrite(100, minute());
  }
}

int todayTrends[96];  // 0 represents the first 15 minutes of the day, and so on. Will have 96 elements.
int trackTrendIndex;
int yesterdayTrends[96];
bool trackTrendResetFlag;
bool trackTrendMinuteFlag;

void trackTrends()
{
  if (hour() == 0 && trackTrendResetFlag  == 0)   // At midnight...
  {
    int i;
    for (i = 0; i < 97; i++) {                    // ...transfer todayTrends to yesterdayTrends.
      yesterdayTrends[i] = todayTrends[i];
    }

    trackTrendIndex = 0;                          // Reset array index to 0.
    trackTrendResetFlag = 1;
  }
  else if (hour() == 1)                           // Resets flag for the next midnight "reset"
  {
    trackTrendResetFlag = 0;
  }

  if (minute() == 0 && hour() == 0)
  {
    // Do nothing... this would record the previous day runtime (23:45 - 00:00) and we don't want that here.
  }
  else if ( (minute() == 0 || minute() == 15 || minute() == 30 || minute() == 45) && trackTrendMinuteFlag == 0)
  {
    todayTrends[trackTrendIndex] = (todaysAccumRuntimeSec / 60);

    if (todayTrends[trackTrendIndex] = yesterdayTrends[trackTrendIndex])
    {
      // "Trending same runtime as yesterday"
    }
    else if (todayTrends[trackTrendIndex] > yesterdayTrends[trackTrendIndex])
    {
      // "Trending higher runtime today"
    }
    else if (todayTrends[trackTrendIndex] < yesterdayTrends[trackTrendIndex])
    {
      // "Trending lower runtime today"
    }

    ++trackTrendIndex;
    trackTrendMinuteFlag = 1;
  }


  if (minute() == 1 || minute() == 16 || minute() == 31 || minute() == 46)
  {
    trackTrendMinuteFlag = 0;
  }

}
