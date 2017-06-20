#include <SimpleTimer.h>        // Timer library
#define BLYNK_PRINT Serial      // Turn this on to see basic Blynk activities
//#define BLYNK_DEBUG           // Turn this on to see *everything* Blynk is doing... really fills up serial monitor!
#include <ESP8266mDNS.h>        // Required for OTA
#include <WiFiUdp.h>            // Required for OTA
#include <ArduinoOTA.h>         // Required for OTA
#include <BlynkSimpleEsp8266.h>
#include <TimeLib.h>            // Used by WidgetRTC.h
#include <WidgetRTC.h>          // Blynk's RTC
#include <OneWire.h>            // Temperature sensor library
#include <DallasTemperature.h>  // Temperature sensor library
#define ONE_WIRE_BUS 12         // WeMos pin D6. RA/SA temp sensor array.
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

DeviceAddress ds18b20RA = { 0x28, 0xEF, 0x97, 0x1E, 0x00, 0x00, 0x80, 0x54 }; // Return air probe - Device address found by going to File -> Examples -> DallasTemperature -> oneWireSearch
DeviceAddress ds18b20SA = { 0x28, 0xF1, 0xAC, 0x1E, 0x00, 0x00, 0x80, 0xE8 }; // Supply air probe

char auth[] = "fromBlynkApp";
char ssid[] = "ssid";
char pass[] = "pw";

SimpleTimer timer;
WidgetRTC rtc;
WidgetTerminal terminal(V26);

const int fanRelay = 16;      // WeMos pin D0. Set LOW to start fan-only.
const int coolingRelay = 5;   // WeMos pin D1. Set LOW to start cooling.
const int heatingRelay = 4;   // WeMos pin D2. Set LOW to start heating.
const int bypassRelay = 0;    // WeMos pin D3. Set LOW to bypass house t-stat.
const int fanPin = 14;        // WeMos pin D5. LOW when fan-only is running.
const int blowerPin = 13;     // WeMos pin D7. LOW when cool or heat is running.

double tempRA, tempSA;        // Return air and supply air temperatures.

bool fanOnly;         // true when fan-only mode is running
bool fanCooling;      // true when cooling mode is running
bool fanHeating;      // true when heating mode is running

long todaysRuntimeSec;  // How many seconds the HVAC ran today
int todaysRuntimeMin;   // How many minutes the HVAC ran today
int currentRunSec;      // How many seconds the HVAC has ran in it's current cycle (resets after HVAC turns off)
long filterUseSec;      // How many seconds the current filter has been used. Can be reset manually after filter change.
int yesterdaysRuntimeSec, yesterdaysRuntimeMin;
int todaysRuns;         // How many times the HVAC has ran today (counted when it shuts off).
int coolingCounter, heatingCounter;   // Count approx how many minutes unit cooled/heated. Used for Tweeting.
String currentStatus;   // For the sole purpose of syncing back the status label after a hardware reset
int todaysDate;         // Today's date (15 or 16, etc)

String currentTime;
String currentTimeDate;
String changeTimeDate;
bool changeFlag;

bool ranOnce;
bool filterChangeMode;
bool startupSynced;

int fanStatusTimer, setClockTimeTimer, tempDisplayTimer, statusDisplayTimer, longStatusDisplayTimer, tempWatcherTimer;

double tempAT, tempKK, tempLK, tempMK;  // Temperatures bridged in from other devices to use for control.
int controllingRoom;
int setpointTemp;     // The HVAC target temperature
int triggerTemp = 1;  // The number of degrees above/below setpoint that the HVAC will start
int tempDiff = 1;     // Temperature differential. You can add 1 to whatever is here because of the way the code compared a double and an int.
bool coolingMode, heatingMode;  // True when cooling or heating is on
int i, maxT, highRoom;    // Used to evaluate all room temperatures in whole-house cooling mode

bool overrunFlag;

void setup()
{
  Serial.begin(9600);
  WiFi.mode(WIFI_STA);

  Blynk.begin(auth, ssid, pass);

  sensors.begin();
  sensors.setResolution(ds18b20RA, 10);
  sensors.setResolution(ds18b20SA, 10);

  // Set pin modes used to control HVAC
  pinMode(coolingRelay, OUTPUT);
  pinMode(heatingRelay, OUTPUT);
  pinMode(fanRelay, OUTPUT);
  pinMode(bypassRelay, OUTPUT);

  // Default all HVAC relays to off
  digitalWrite(coolingRelay, HIGH);
  digitalWrite(heatingRelay, HIGH);
  digitalWrite(fanRelay, HIGH);
  digitalWrite(bypassRelay, HIGH);

  while (Blynk.connect() == false) {
    // Wait until connected
  }

  // START OTA ROUTINE
  ArduinoOTA.setHostname("hvacMonitor");     // Port name that is displayed in the Arduino IDE.

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

  fanStatusTimer = timer.setInterval(1000, fanStatus);
  setClockTimeTimer = timer.setInterval(1000, setClockTime);
  tempDisplayTimer = timer.setInterval(3210, tempDisplay);
  statusDisplayTimer = timer.setInterval(3543, statusDisplay);
  tempWatcherTimer = timer.setInterval(5000, tempWatcher);
  longStatusDisplayTimer = timer.setInterval(600000, longStatusDisplay);  // 10 minutes
  timer.setInterval(65432, updateVpinData1);

  timer.disable(fanStatusTimer);
  timer.disable(tempDisplayTimer);
  timer.disable(statusDisplayTimer);
  timer.disable(tempWatcherTimer);
}

void loop()  // To keep Blynk happy, keep most tasks out of the loop.
{
  Blynk.run();
  timer.run();
  ArduinoOTA.handle();

  if (!startupSynced && year() != 1970) {
    startupSynced = true;
    startupSync();
    todaysDate = day();
    timer.setTimeout(5000, startTimers);
  }
}

// BEGIN CONTROL SECTION

//int tempATreportMin, tempKKreportMin, tempLKreportMin, tempMKreportMin;
double tempATlast, tempKKlast, tempLKlast, tempMKlast;
int tempATmatch, tempKKmatch, tempLKmatch, tempMKmatch;

BLYNK_WRITE(V127) {   // Collect temperature data from sensors (sensors push data) and notify if sensor is not reporting
  int nodeData = param[0].asInt();
  double tempData = param[1].asDouble();

  if (nodeData == 1) {
    tempAT = tempData;
    //tempATreportMin = minute();

    if (tempAT == tempATlast) {     // If temps match...
      ++tempATmatch;                // count how many times they matched.
    }
    else {                      // if they didn't
      tempATmatch = 0;          // reset the match counter
      tempATlast = tempAT;      // and "reset" tempATlast.
    }
    //Serial.println(String("[") + currentTime + "] AT:" + tempAT + "F (" + tempATmatch + "/" + tempATreportMin + "m)");
    Serial.println(String("[") + currentTime + "] AT:" + tempAT + "F (" + tempATmatch + ")");
  }
  else if (nodeData == 2) {
    tempKK = tempData;
    //tempKKreportMin = minute();

    if (tempKK == tempKKlast) {
      ++tempKKmatch;
    }
    else {
      tempKKmatch = 0;
      tempKKlast = tempKK;
    }
    //Serial.println(String("[") + currentTime + "] KK:" + tempKK + "F (" + tempKKmatch + "/" + tempKKreportMin + "m)");
    Serial.println(String("[") + currentTime + "] KK:" + tempKK + "F (" + tempKKmatch + ")");
  }
  else if (nodeData == 3) {
    tempLK = tempData;
    //tempLKreportMin = minute();

    if (tempLK == tempLKlast) {
      ++tempLKmatch;
    }
    else {
      tempLKmatch = 0;
      tempLKlast = tempLK;
    }
    //Serial.println(String("[") + currentTime + "] LK:" + tempLK + "F (" + tempLKmatch + "/" + tempLKreportMin + "m)");
    Serial.println(String("[") + currentTime + "] LK:" + tempLK + "F (" + tempLKmatch + ")");
  }
  else if (nodeData == 4) {
    tempMK = tempData;
    //tempMKreportMin = minute();

    if (tempMK == tempMKlast) {
      ++tempMKmatch;
    }
    else {
      tempMKmatch = 0;
      tempMKlast = tempMK;
    }
    //Serial.println(String("[") + currentTime + "] MK:" + tempMK + "F (" + tempMKmatch + "/" + tempMKreportMin + "m)");
    Serial.println(String("[") + currentTime + "] MK:" + tempMK + "F (" + tempMKmatch + ")");
  }
}

BLYNK_WRITE(V39)  // Input from slider in app to set setpoint
{
  setpointTemp = param.asInt();
  /*DEBUG*/Serial.println(String("[") + millis() + "] setpointTemp = " + setpointTemp);
}

void tempWatcher() {
  double tempArray[5] = {0, tempAT, tempKK, tempLK, tempMK};  // This starts with 0, since AT is Node01, KK is Node02, etc.
  // Evaluates which room is the hottest
  for (i = 0; i < 5; i++) {         // If 0 skip, then look at tempAT and other rooms.
    if (tempArray[i] > maxT) {      // Compare each room to maxT and if higher,
      maxT = tempArray[i];          // store that room's temp as maxT.
      if (!coolingMode) {           // Now if cooling is off,
        highRoom = i;               // set the index number (i) as the high room. This will setup this particular room as the target to cool (control watches this room to determine when to turn off).
      }
    }
    if (i == 0) {                   // When we're back to the top of the order, reset maxT, otherwise it would just always hold the highest temperature since the WeMos was reset!
      maxT = 0;
    }
  }

  String highRoomName;          // Gives a "proper" room name in lieu of node for use in app labels, etc.

  if (highRoom == 1) {
    highRoomName = "House";
  }
  else if (highRoom == 2) {
    highRoomName = "KK";
  }
  else if (highRoom == 3) {
    highRoomName = "LK";
  }
  else if (highRoom == 4) {
    highRoomName = "MK";
  }

  if (controllingRoom == 1 && !coolingMode && setpointTemp != 0 && ( tempArray[highRoom] >= setpointTemp + triggerTemp) ) {
    /*DEBUG*/Serial.println(String("[") + currentTime + "] Cooling started by " + highRoomName + " when it reached " + tempArray[highRoom] + "F. Shuts off at " + (setpointTemp - tempDiff) + "F.");
    Blynk.setProperty(V16, "label", String("Status: Started by ") + highRoomName + " when it reached " + tempArray[highRoom] + "F. Shuts off at " + (setpointTemp - tempDiff) + "F.") ;
    digitalWrite(coolingRelay, LOW);
    coolingMode = true;
  }
  else if (controllingRoom == 1 && coolingMode && setpointTemp != 0 && ( tempArray[highRoom] <= setpointTemp - tempDiff) ) {
    /*DEBUG*/Serial.println(String("[") + currentTime + "] Cooling stopped by " + highRoomName + " b/c " + tempArray[highRoom] + " <= " + (setpointTemp - tempDiff));
    Blynk.setProperty(V16, "label", "Status:");
    digitalWrite(coolingRelay, HIGH);
    coolingMode = false;
    overrunFlag = false;
  }

  // SAFETY: Turn off HVAC it it runs over 2 hours
  if (currentRunSec > 7200 && coolingMode) {
    shutdownHVACoverrun();
  }
}

void shutdownHVACoverrun() {
  //Blynk.setProperty(V16, "label", String("Status: ") + currentTime + " HVAC SHUTDOWN: Ran longer than 3 hours.") ;
  //coolingMode = false;
  //Blynk.virtualWrite(V40, "pick", 2);
  //Blynk.virtualWrite(V40, 2);
  //Blynk.syncVirtual(V40);
  if (!overrunFlag) {
    Blynk.notify(String(currentTime) + " WARNING: HVAC has run for 2 hours.");
    overrunFlag = true;
  }
  
}

// Set which room is controlling HVAC via the app's menu
BLYNK_WRITE(V40) {
  switch (param.asInt())
  {
    case 1: {                             // House t-stat in control
        /*DEBUG*/Serial.println(String("[") + currentTime + "] House tstat in control model selected");
        controllingRoom = 0;              // This number is node number
        /*DEBUG*/Serial.println(String("[") + currentTime + "] controllingRoom = 0");
        timer.disable(tempWatcherTimer);
        /*DEBUG*/Serial.println(String("[") + currentTime + "] tempWatcherTimer disabled");
        digitalWrite(fanRelay, HIGH);
        digitalWrite(coolingRelay, HIGH);
        digitalWrite(heatingRelay, HIGH);
        digitalWrite(bypassRelay, HIGH);
        break;
      }
    case 2: {                             // Manual fan-only
        /*DEBUG*/Serial.println(String("[") + currentTime + "] Manual fan only mode selected");
        controllingRoom = 0;
        /*DEBUG*/Serial.println(String("[") + currentTime + "] controllingRoom = 0");
        timer.disable(tempWatcherTimer);
        /*DEBUG*/Serial.println(String("[") + currentTime + "] tempWatcherTimer disabled");
        digitalWrite(fanRelay, LOW);
        digitalWrite(coolingRelay, HIGH);
        digitalWrite(heatingRelay, HIGH);
        digitalWrite(bypassRelay, LOW);
        break;
      }
    case 3: {                             // Whole-house cooling
        /*DEBUG*/Serial.println(String("[") + currentTime + "] Whole house cooling mode selected");
        controllingRoom = 1;
        /*DEBUG*/Serial.println(String("[") + currentTime + "] controllingRoom = 1 (whole-house)");
        timer.enable(tempWatcherTimer);
        /*DEBUG*/Serial.println(String("[") + currentTime + "] tempWatcherTimer enabled");
        coolingMode = false;              // This sets up the unit to correctly watch room temps (seems reversed but it isn't)
        digitalWrite(fanRelay, HIGH);
        digitalWrite(coolingRelay, HIGH);
        digitalWrite(heatingRelay, HIGH);
        digitalWrite(bypassRelay, LOW);
        break;
      }
    case 4: {                             // KK cooling
        controllingRoom = 2;
        timer.enable(tempWatcherTimer);
        digitalWrite(fanRelay, HIGH);
        digitalWrite(coolingRelay, HIGH);
        digitalWrite(heatingRelay, HIGH);
        digitalWrite(bypassRelay, LOW);
        break;
      }
    case 5: {                             // LK cooling
        controllingRoom = 3;
        timer.enable(tempWatcherTimer);
        digitalWrite(fanRelay, HIGH);
        digitalWrite(coolingRelay, HIGH);
        digitalWrite(heatingRelay, HIGH);
        digitalWrite(bypassRelay, LOW);
        break;
      }
  }
}

// END CONTROL SECTION

// BEGIN STARTUP SEQUENCE
void startupSync() {
  Blynk.virtualWrite(V40, "pick", 1);
  Blynk.syncVirtual(V11, V39, V40, V110, V111, V112);
}

BLYNK_WRITE(V11)
{
  int filterUseMin = param.asInt();   // Doing this because there is no param.asLong
  filterUseSec = filterUseMin * 60;
}
BLYNK_WRITE(V110)
{
  todaysRuns = param.asInt();
}
BLYNK_WRITE(V111)
{
  todaysRuntimeMin = param.asInt();   // Doing this because there is no param.asLong
  todaysRuntimeSec = todaysRuntimeMin * 60L;
}
BLYNK_WRITE(V112)
{
  yesterdaysRuntimeMin = param.asInt();
  yesterdaysRuntimeSec = yesterdaysRuntimeMin * 60L;

  // Updates the "Yesterday's runtime" display (at startup)
  if (yesterdaysRuntimeSec == 0) {
    Blynk.virtualWrite(V14, "None");
  }
  else {
    yesterdaysRuntimeMin = yesterdaysRuntimeSec / 60;
    Blynk.virtualWrite(V14, String(yesterdaysRuntimeMin) + " minutes");
  }
}

void startTimers() {
  timer.enable(fanStatusTimer);
  timer.enable(setClockTimeTimer);
  timer.enable(tempDisplayTimer);
  timer.enable(statusDisplayTimer);
}
// END STARTUP SEQUENCE

void fanStatus() {
  if (digitalRead(fanPin) == LOW) {  // If fan-only mode is running
    fanOnly = true;                  // set that mode and
    ++filterUseSec;                  // count seconds of filter use.
  }
  else {
    fanOnly = false;
  }

  if (digitalRead(blowerPin) == LOW) {  // If heating or cooling is running
    fanCooling = true;                  // set those modes,
    fanHeating = true;
    ++todaysRuntimeSec;                 // accumulated seconds it's ran today,
    ++filterUseSec;                     // count seconds of filter use,
    ++currentRunSec;                    // count seconds of current run (SAFETY),
  }
  else {                                // otherwise "turn off" those modes,
    fanCooling = false;
    fanHeating = false;
    currentRunSec = 0;                  // and reset current run counter.
  }

  // Sets the start/end time and date when the blower starts or stops
  if (digitalRead(blowerPin) == LOW && ( changeFlag || !ranOnce) ) {      // If heating or cooling is running, and was previously off (or the WeMos just started up)
    changeTimeDate = currentTimeDate;                                     // set start time,
    changeFlag = false;                                                   // use a flag to keep this process from happening again ("locking" the time/date for display), and
    ranOnce = true;                                                       // indicate that ranOnce... ran... once!
  }
  else if (digitalRead(blowerPin) == HIGH && !changeFlag) {               // If heating/cooling is off, and was previously off (or the WeMos just started up (no need for ranOnce as changeFlag is false at startup))
    changeTimeDate = currentTimeDate;                                     // set end time and
    changeFlag = true;                                                    // lock out this process with flag. Now,

    if (ranOnce) {                                                        // since I'm counting a run during an end event, I don't want a WeMos startup to register as a run, so I'm locking that out here, but
      ++todaysRuns;
    }
    else {
      ranOnce = true;                                                                 // unlocking here.
    }
  }

  if (todaysDate != day()) {                    // At the end of the day
    yesterdaysRuntimeSec = todaysRuntimeSec;    // move today's runcount to yesterday,
    todaysRuntimeSec = 0;                       // reset today's runtime, and
    todaysRuns = 0;                             // reset today's number of runs.
    coolingCounter = 0;
    heatingCounter = 0;
    todaysDate = day();
  }
}

void tempDisplay() {
  sensors.requestTemperatures();            // Polls the temperature sensors
  tempRA = sensors.getTempF(ds18b20RA);
  tempSA = sensors.getTempF(ds18b20SA);

  if (fanCooling || fanHeating) {
    Blynk.virtualWrite(V0, tempRA);          // Display temperatures in app
    Blynk.virtualWrite(V1, tempSA);
  }
  else if (fanOnly) {
    Blynk.virtualWrite(V0, "FAN");
    Blynk.virtualWrite(V1, "FAN");
  }
  else {
    Blynk.virtualWrite(V0, "OFF");
    Blynk.virtualWrite(V1, "OFF");
  }
}

void statusDisplay() {
  // Updates the primary "Status" display
  if (fanCooling || fanHeating ) {
    currentStatus = String("HVAC ON since ") + changeTimeDate;  // I'm using this string because I need to sync this back after startup. I need to sync it because status is not static, it's refreshed at interval.
    Blynk.virtualWrite(V16, currentStatus);
  }
  else if (!fanCooling || !fanHeating) {
    currentStatus = String("HVAC OFF since ") + changeTimeDate;
    Blynk.virtualWrite(V16, currentStatus);
  }

  // Updates the "Today's runtime" display
  if (todaysRuntimeSec == 0) {
    Blynk.virtualWrite(V15, "None");                     // Yesterday runtime label
  }
  else {
    todaysRuntimeMin = todaysRuntimeSec / 60;
    Blynk.virtualWrite(V15, String(todaysRuntimeMin) + " mins (" + todaysRuns + " runs)");
  }
}

void longStatusDisplay() {
  int filterUseHr = filterUseSec / 3600;
  Blynk.virtualWrite(V10, String(300 - filterUseHr) + " hours");

  // Updates the "Yesterday's runtime" display
  if (yesterdaysRuntimeSec == 0) {
    Blynk.virtualWrite(V14, "None");                     // Yesterday runtime label
  }
  else {
    yesterdaysRuntimeMin = yesterdaysRuntimeSec / 60;
    Blynk.virtualWrite(V14, String(yesterdaysRuntimeMin) + " minutes");
  }
}

void setClockTime() {
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

    if (minute() > 9 && hour() > 11) {
      currentTime = String(hourFormat12()) + ":" + minute() + "PM";
    }
    else if (minute() < 10 && hour() > 11) {
      currentTime = String(hourFormat12()) + ":0" + minute() + "PM";
    }
    else if (minute() > 9 && hour() < 12) {
      currentTime = String(hourFormat12()) + ":" + minute() + "AM";
    }
    else if (minute() < 10 && hour() < 12) {
      currentTime = String(hourFormat12()) + ":0" + minute() + "AM";
    }
  }
}

void updateVpinData1() {
  Blynk.virtualWrite(V110, todaysRuns);            // Stores how many times the unit has started today
  Blynk.virtualWrite(V111, todaysRuntimeMin);      // Stores many minutes the HVAC ran today
  timer.setTimeout(5002, updateVpinData2);
}

void updateVpinData2() {
  Blynk.virtualWrite(V112, yesterdaysRuntimeMin);  // Yesterday's runtime in minutes
  Blynk.virtualWrite(V11, filterUseSec / 60);      // How many minutes the installed air filter has been used


  // Counts minutes of cooling or heating to support the language used in the nightly tweet
  if (fanCooling || fanHeating) {
    if (tempRA > tempSA) {
      ++coolingCounter;
    }
    else {
      ++heatingCounter;
    }
  }

  if (coolingCounter == 0 && heatingCounter == 0) {
    Blynk.virtualWrite(V38, "NONE");
  }
  else if (coolingCounter > heatingCounter) {
    Blynk.virtualWrite(V38, "COOL");
  }
  else if (coolingCounter < heatingCounter) {
    Blynk.virtualWrite(V38, "HEAT");
  }
}

BLYNK_WRITE(V18) // Filter change mode
{
  int pinData = param.asInt();

  if (pinData == 0) // Triggers when button is released only
  {
    filterChangeMode = true;

    terminal.println(""); terminal.println("");
    terminal.println("       ~~ A/C Filter Status Mode ~~"); terminal.println(" ");
    terminal.println("Was the filter just changed (y/n)?"); terminal.println(" ");
    terminal.flush();
  }
}

BLYNK_WRITE(V26)  // Resets filter timer
{
  if ( (String("y") == param.asStr() || String("Y") == param.asStr()) && filterChangeMode) {
    filterUseSec = 0;
    terminal.println(" "); terminal.println("Filter timer reset!"); terminal.println(" ");
    filterChangeMode = false;
    int filterUseHr = filterUseSec / 3600;
    Blynk.virtualWrite(V10, String(300 - filterUseHr) + " hours");
  }
  else if ( (String("n") == param.asStr() || String("N") == param.asStr()) && filterChangeMode) {
    terminal.println(" "); terminal.println("Filter change cancelled."); terminal.println(" ");
    filterChangeMode = false;
  }
  terminal.flush();
}

BLYNK_WRITE(V27)  // App button to report uptime
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
      terminal.print(String("HVAC: ") + minDur + " min/");
      terminal.print(WiFi.RSSI());
      terminal.print("/");
      terminal.println(WiFi.localIP());
    }
    else if (minDur > 120)
    {
      terminal.print(String("HVAC: ") + hourDur + " hrs/");
      terminal.print(WiFi.RSSI());
      terminal.print("/");
      terminal.println(WiFi.localIP());
    }
    terminal.flush();
  }
}
