#include <SimpleTimer.h>        // Timer library
#define BLYNK_PRINT Serial      // Turn this on to see basic Blynk activities
//#define BLYNK_DEBUG           // Turn this on to see *everything* Blynk is doing... really fills up serial monitor!
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

char* hostSF = "raspi";
char* streamId   = "publicKey";
char* privateKey = "privateKey";

SimpleTimer timer;

WidgetRTC rtc;
WidgetTerminal terminal(V26);

const int blowerPin = 13;  // WeMos pin D7.
const int fanPin = 14;     // Future fan only pin.
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
String currentStatus;                 // What displays in app. Used solely for sync and recover purposes.

long currentFilterSec;                // Filter age based on unit runtime in seconds (stored in vPin). Seconds used for finer resolution.
long currentFilterHours;              // Filter age based on unit runtime in hours.
long timeFilterChanged;               // Unix time that filter was last changed.
bool filterConfirmFlag = FALSE;       // Used to enter/exit the filter change mode.
const int filterChangeHours = 300L;   // Duration in hours between filter changes. Up for debate and experimentation!
bool changeFilterAlarm = FALSE;       // TRUE if filter needs to be changed, until reset.

long todaysAccumRuntimeSecCooling;    // Seconds that system has been cooling.
long todaysAccumRuntimeSecHeating;    // Seconds that system has been heating.
bool heatingMode;                     // FALSE = Cooling. TRUE = Heating.
bool hvacMode;                        // FALSE = Cooling. TRUE = Heating.

double tempRA, tempSA;                // Return and supply air temperatures

// HVAC control pins
int coolingRelay = 5;   // WeMos pin D1.
int heatingRelay = 4;   // WeMos pin D2.
int fanRelay = 16;      // WeMos pin D0.
int bypassRelay = 0;    // WeMos pin D3.

int controlSplit = 1;   // Predefined value of how much I want to heat or cool above trigger point.
int controllingTemp;
int setpointTemp;
String controllingSpace;
bool coolingMode;       // 1 = cooling. 0 = heating.
bool controlMode;
int tempKK, tempLK;

long controlStart;
long controlRuntime;
long heatControlTimeout = 600000; // 600000 millis = 10 minutes
long coolControlTimeout = 120000000; // 120000000 millis = 20 minutes
bool controlLatch = FALSE;

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

  pinMode(coolingRelay, OUTPUT);
  pinMode(heatingRelay, OUTPUT);
  pinMode(fanRelay, OUTPUT);
  pinMode(bypassRelay, OUTPUT);

  // Default all HVAC relays to off
  digitalWrite(coolingRelay, HIGH);
  digitalWrite(heatingRelay, HIGH);
  digitalWrite(fanRelay, HIGH);
  digitalWrite(bypassRelay, HIGH);

  // START OTA ROUTINE
  ArduinoOTA.setHostname("hvacMonitor");     // Name that is displayed in the Arduino IDE.

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
  timer.setInterval(1000L, modeTracking);     // Tracks if unit has been mostly cooling or heating during the day.
  timer.setInterval(1000L, setClockTime);     // Creates a current time with leading zeros.
  //timer.setInterval(500L, alarmTimer);      // Alarms for low SA/RA split in heating or cooling.
  timer.setInterval(15432L, phantSend);       // ~15 sec run status updates to Phant runnting on a local Raspberry Pi.
  timer.setTimeout(100, vsync1);              // Syncs back vPins to survive hardware reset.
  timer.setTimeout(2000, vsync2);             // Syncs back vPins to survive hardware reset.
  timer.setTimeout(4000, vsync3);             // Syncs back vPins to survive hardware reset.
  timer.setTimeout(8000, controlReset);
  timer.setInterval(30000L, tempUpdater);      // When bypass on, this evaluates controlling space against setpoint.
  timer.setInterval(30000L, tempControl);     // Temps (controlling space) that can control HVAC are synced.
  timer.setInterval(1000L, tempControlSafety);

}

void loop()  // To keep Blynk happy, keep most tasks out of the loop.
{
  Blynk.run();
  timer.run();
  ArduinoOTA.handle();
}

// ******************************************************** HVAC CTRL START **********************************************
BLYNK_WRITE(V40) {
  switch (param.asInt())
  {
    // **************************************** IN ALL CASES BELOW: LOW == ON/CLOSED, HIGH == OFF/OPEN
    case 1: {                         // House t-stat controlling (bypass off)
        controlMode = 0;
        controllingSpace = "";
        Blynk.setProperty(V39, "label", "Setpoint");
        digitalWrite(fanRelay, HIGH);
        digitalWrite(coolingRelay, HIGH);
        digitalWrite(heatingRelay, HIGH);
        digitalWrite(bypassRelay, HIGH);
        break;
      }
    case 2: {                         // House t-stat bypass (Blynk-controlling, but nothing running)
        controlMode = 0;
        controllingSpace = "";
        Blynk.setProperty(V39, "label", "Setpoint");
        digitalWrite(fanRelay, HIGH);
        digitalWrite(coolingRelay, HIGH);
        digitalWrite(heatingRelay, HIGH);
        digitalWrite(bypassRelay, LOW);
        break;
      }
    case 3: {                         // Manual fan (blower only)
        controlMode = 0;
        controllingSpace = "";
        Blynk.setProperty(V39, "label", "Setpoint");
        digitalWrite(fanRelay, LOW);
        digitalWrite(coolingRelay, HIGH);
        digitalWrite(heatingRelay, HIGH);
        digitalWrite(bypassRelay, LOW);
        break;
      }
    case 4: {                         // Manual cooling
        /*
          controlMode = 0;
          controllingSpace = "";
          Blynk.setProperty(V39, "label", "Setpoint");
          digitalWrite(fanRelay, HIGH);
          digitalWrite(coolingRelay, LOW);
          digitalWrite(heatingRelay, HIGH);
          digitalWrite(bypassRelay, LOW);
        */
        controlReset();

        break;
      }
    case 5: {                         // Manual heating
        /*
          controlMode = 0;
          controllingSpace = "";
          Blynk.setProperty(V39, "label", "Setpoint");
          digitalWrite(fanRelay, HIGH);
          digitalWrite(coolingRelay, HIGH);
          digitalWrite(heatingRelay, LOW);
          digitalWrite(bypassRelay, LOW);
        */
        controlReset();

        break;
      }
    case 6: {                                         // Keaton-controlled cooling
        controlMode = 1;
        controllingSpace = "KK";                      // Here's the room that we're watching
        coolingMode = 1;                              // This sets the HVAC in correct mode and decides tstat logic
        controllingTemp = tempKK;
        Blynk.setProperty(V39, "label", String("Setpoint: ") + setpointTemp + ". Controlled by KK (currently " + tempKK + "F).");
        digitalWrite(fanRelay, HIGH);
        digitalWrite(coolingRelay, HIGH);
        digitalWrite(heatingRelay, HIGH);
        digitalWrite(bypassRelay, LOW);
        break;
      }
    case 7: {                             // Keaton-controlled heating
        Blynk.syncVirtual(V39);
        controlMode = 1;
        controllingSpace = "KK";
        coolingMode = 0;
        controllingTemp = tempKK;
        Blynk.setProperty(V39, "label", String("Setpoint: ") + setpointTemp + "F. Controlled by KK (currently " + tempKK + "F).");
        digitalWrite(fanRelay, HIGH);
        digitalWrite(coolingRelay, HIGH);
        digitalWrite(heatingRelay, HIGH);
        digitalWrite(bypassRelay, LOW);
        break;
      }
    case 8: {                             // Liv-controlled cooling
        controlMode = 1;
        controllingSpace = "LK";
        coolingMode = 1;
        controllingTemp = tempLK;
        Blynk.setProperty(V39, "label", String("Setpoint: ") + setpointTemp + ". Controlled by LK (currently " + tempLK + "F).");
        digitalWrite(fanRelay, HIGH);
        digitalWrite(coolingRelay, HIGH);
        digitalWrite(heatingRelay, HIGH);
        digitalWrite(bypassRelay, LOW);
        break;
      }
    case 9: {                             // Liv-controlled heating
        controlMode = 1;
        controllingSpace = "LK";
        coolingMode = 0;
        controllingTemp = tempLK;
        Blynk.setProperty(V39, "label", String("Setpoint: ") + setpointTemp + ". Controlled by LK (currently " + tempLK + "F).");
        digitalWrite(fanRelay, HIGH);
        digitalWrite(coolingRelay, HIGH);
        digitalWrite(heatingRelay, HIGH);
        digitalWrite(bypassRelay, LOW);
        break;
      }
    default: {
        break;
      }
  }
}

void tempControl() {

  if (controlMode == 1 && setpointTemp != 0 && controllingTemp != 0)     // If we're in bypass mode, and established setpoint and control temps, then proceed:
  {

    if (coolingMode == 0 && controllingTemp < setpointTemp)       // If mode is heat and room falls equal or below the target temp...
    {
      digitalWrite(heatingRelay, LOW);                            // turn on the heat. But when
      controlRuntime = controlRuntime + 30;
    }
    else if (coolingMode == 0 && controllingTemp >= (setpointTemp + controlSplit))   // If mode is heat and room is warmer then target temp...
    {
      digitalWrite(heatingRelay, HIGH);                           // everything is turned off.
      controlRuntime = 0;
      controlLatch = FALSE;
    }
    else if (coolingMode == 1 && controllingTemp > setpointTemp)  // If mode is cool and room rises equal or above the target temp...
    {
      digitalWrite(coolingRelay, LOW);                            // turn on cooling. Then,
      controlRuntime = controlRuntime + 30;
    }
    else if (coolingMode == 1 && controllingTemp <= (setpointTemp - controlSplit))   // when mode is cool and room is cooler then target temp...
    {
      digitalWrite(coolingRelay, HIGH);                           // turn everything off.
      controlRuntime = 0;
      controlLatch = FALSE;
    }

  }
}

void tempControlSafety() {      // Safeties running every second
  if (controlMode == 1 && digitalRead(blowerPin) == LOW && (setpointTemp <= 0 || controllingTemp  <= 0) )  // If HVAC is running and the setpoint or controlling temp are 0, then open relays. Control will be locked out until those temps are != 0.
  {
    safetyShutoff();

    Blynk.notify(hour() + String(":") + minute() + ":" + second() + " " + month() + "/" + day() + " ERROR: Ctrl attempted with no setpointTemp or controllingTemp");
  }

  if (controlMode == 1 && digitalRead(blowerPin) == LOW && controlLatch == FALSE) {  // If Blynk is controlling, and fan starts:
    controlStart = millis();            // Records start time of unit.
    controlLatch = TRUE;
  }

  if (controlMode == 1 && digitalRead(blowerPin) == LOW && coolingMode == 0 && millis() >= controlStart + heatControlTimeout) {
    safetyShutoff();

    Blynk.notify(hour() + String(":") + minute() + ":" + second() + " " + month() + "/" + day() + " ERROR: Heater ran for more than " + (heatControlTimeout / 60000) + " minutes.");
  }
  else if (controlMode == 1 && digitalRead(blowerPin) == LOW && coolingMode == 1 && millis() >= controlStart + coolControlTimeout) {
    safetyShutoff();

    Blynk.notify(hour() + String(":") + minute() + ":" + second() + " " + month() + "/" + day() + " ERROR: Cooler ran for more than " + (coolControlTimeout / 60000) + " minutes.");
  }

}

void safetyShutoff() {
  controlMode = 0;
  controllingSpace = "";
  controlLatch = FALSE;
  Blynk.setProperty(V39, "label", "Setpoint");
  digitalWrite(fanRelay, HIGH);
  digitalWrite(coolingRelay, HIGH);
  digitalWrite(heatingRelay, HIGH);
  digitalWrite(bypassRelay, HIGH);
}

void tempUpdater() {
  Blynk.syncVirtual(V4);
  Blynk.syncVirtual(V6);

  if (controllingSpace == "KK")
  {
    controllingTemp = tempKK;
    Blynk.setProperty(V39, "label", String("Setpoint: ") + setpointTemp + "F. Controlled by KK (currently " + tempKK + "F).");
  }
  else if (controllingSpace == "LK")
  {
    controllingTemp = tempLK;
    Blynk.setProperty(V39, "label", String("Setpoint: ") + setpointTemp + "F. Controlled by LK (currently " + tempLK + "F).");
  }
}

BLYNK_WRITE(V4) {
  tempKK = param.asInt();
}

BLYNK_WRITE(V6) {
  tempLK = param.asInt();
}

BLYNK_WRITE(V39)  // Input from slider in app to set setpoint
{
  setpointTemp = param.asInt();
}

void controlReset() {
  controlMode = 0;
  controllingSpace = "";
  Blynk.setProperty(V39, "label", "Setpoint");
  digitalWrite(fanRelay, HIGH);
  digitalWrite(coolingRelay, HIGH);
  digitalWrite(heatingRelay, HIGH);
  digitalWrite(bypassRelay, HIGH);
  Blynk.virtualWrite(V40, 1);

}
// ******************************************************** HVAC CTRL END **********************************************

void vsync1()
{
  Blynk.syncVirtual(V16);    // currentStatus
  Blynk.syncVirtual(V39);    // setpointTemp
}

void vsync2()
{
  Blynk.syncVirtual(V110);    // hvacTodaysStartCount
  Blynk.syncVirtual(V111);    // todaysAccumRuntimeSec
}

void vsync3()
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

BLYNK_WRITE(V16)
{
  currentStatus = param.asStr();
}

void phantSend() {
  if (digitalRead(blowerPin) == LOW)        // If HVAC blower motor is running...
  {
    runStatus = 40;
  }
  else if (digitalRead(blowerPin) == HIGH)  // If HVAC blower motor if off...
  {
    runStatus = NULL;
  }

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

  //Blynk.virtualWrite(67, String("returntemp=") + tempRA + "&runstatus=" + runStatus + "&supplytemp=" + tempSA);

  Serial.print("connecting to ");
  Serial.println(hostSF);

  // Use WiFiClient class to create TCP connections
  WiFiClient client;
  const int httpPort = 8080;
  if (!client.connect(hostSF, httpPort)) {
    Serial.println("connection failed");
    return;
  }

  Serial.print("Requesting...");

  // This will send the request to the server
  client.print(String("GET ") + "/input/" + streamId + "?private_key=" + privateKey + "&returntemp=" + tempRA + "&runstatus=" + runStatus + "&supplytemp=" + tempSA + " HTTP/1.1\r\n" +
               "Host: " + hostSF + "\r\n" +
               "Connection: close\r\n\r\n");
  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 15000) {
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

BLYNK_WRITE(V19)
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
  sensors.requestTemperatures();                                          // Polls the sensors
  tempRA = sensors.getTempF(ds18b20RA);
  tempSA = sensors.getTempF(ds18b20SA);
  tempSplit = tempRA - tempSA;

  // RETURN AIR
  if (digitalRead(blowerPin) == LOW)    // If HVAC blower running...
  {
    Blynk.virtualWrite(0, tempRA);      // ...display the temp...
  }
  else if (digitalRead(fanPin) == LOW)  // ...if fan-only running, display FAN, unless...
  {
    Blynk.virtualWrite(0, "FAN");
  }
  else if (tempRA <= 0 || tempRA > 150)
  {
    Blynk.virtualWrite(0, "ERR");       // ...there's an error, then display ERR, otherwise...
  }
  else{
    Blynk.virtualWrite(0, "OFF");       // ...display OFF.
  }

  // SUPPLY AIR
  if (digitalRead(blowerPin) == LOW)
  {
    Blynk.virtualWrite(1, tempSA);
  }
  else if (digitalRead(fanPin) == LOW)
  {
    Blynk.virtualWrite(1, "FAN");
  }
  else if (tempSA <= 0 || tempSA > 150)
  {
    Blynk.virtualWrite(1, "ERR");
  }
  else {
    Blynk.virtualWrite(1, "OFF");
  }

  if (digitalRead(blowerPin) == LOW) {    // If HVAC is running and...
    if (tempSA > tempRA && hvacMode == FALSE)       // supply air is warmer than return air...
    {
      Blynk.setProperty(V1, "color", "#ff0000");    // Supply is RED
      Blynk.setProperty(V0, "color", "#04C0F8");    // Return is BLUE
      hvacMode = TRUE;                              // FALSE = cooling, TRUE =  heating.
    }
    else if (tempSA <= tempRA && hvacMode == TRUE)
    {
      Blynk.setProperty(V1, "color", "#04C0F8");    // Supply is BLUE
      Blynk.setProperty(V0, "color", "#ff0000");    // Return is RED
      hvacMode = FALSE;                             // FALSE = cooling, TRUE =  heating.
    }
  }
}

void modeTracking()
{
  if (digitalRead(blowerPin) == LOW)  // If the blower is running...
  {
    if (tempSA > tempRA)              // and it looks like we're heating, then...
    {
      hvacMode = TRUE;                // TRUE = heating, and...
      ++todaysAccumRuntimeSecHeating; // count each heating second, but...
    }
    else if (tempSA < tempRA)         // if it looks like we're coolng, then...
    {
      hvacMode = FALSE;               // FALSE = cooling, and...
      ++todaysAccumRuntimeSecCooling; // count each cooling second.
    }
  }

  if (todaysAccumRuntimeSecHeating == 0 && todaysAccumRuntimeSecCooling == 0)
  {
    Blynk.virtualWrite(38, "NONE");
  }
  else if (todaysAccumRuntimeSecHeating < todaysAccumRuntimeSecCooling)
  {
    Blynk.virtualWrite(38, "COOL");
    heatingMode = FALSE;
  }
  else if (todaysAccumRuntimeSecHeating > todaysAccumRuntimeSecCooling)
  {
    Blynk.virtualWrite(38, "HEAT");
    heatingMode = TRUE;
  }
}

void sendStatus()
{
  if (resetFlag == TRUE && year() != 1970) {                       // Runs once following Arduino reset/start.
    resetTimeDate = currentTimeDate;
    Blynk.virtualWrite(16, currentStatus);
    Blynk.setProperty(V16, "label", String("Current Status:                    SYSTEM RESET at ") + resetTimeDate);
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
    currentStatus = String("HVAC OFF since ") + stopTimeDate;
    Blynk.setProperty(V0, "color", "#808080");
    Blynk.setProperty(V1, "color", "#808080");

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
    currentStatus = String("HVAC ON since ") + startTimeDate;


    if (tempSA > tempRA)  // Assumed HEATING mode
    {
      Blynk.setProperty(V1, "color", "#ff0000");    // Supply is RED
      Blynk.setProperty(V0, "color", "#04C0F8");    // Return is BLUE
      hvacMode = FALSE;                             // FALSE = cooling, TRUE =  heating. Logic swapped on purpose.
    }
    else                  // Assumed COOLING mode
    {
      Blynk.setProperty(V1, "color", "#04C0F8");    // Supply is BLUE
      Blynk.setProperty(V0, "color", "#ff0000");    // Return is RED
      hvacMode = TRUE;                              // FALSE = cooling, TRUE =  heating. Logic swapped on purpose.
    }

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

    }
    else if (todaysDate != day())
    {
      yesterdayRuntime = (todaysAccumRuntimeSec / 60);  // Moves today's runtime to yesterday for the app display.
      Blynk.virtualWrite(112, yesterdayRuntime);        // Record yesterday's runtime to vPin.
      todaysAccumRuntimeSec = 0;                        // Reset today's sec timer.
      todaysAccumRuntimeSecHeating = 0;                 // Reset today's heating and cooling timers.
      todaysAccumRuntimeSecCooling = 0;
      Blynk.virtualWrite(111, todaysAccumRuntimeSec);
      hvacTodaysStartCount = 0;                         // Reset how many times unit has started today.
      todaysDate = day();
    }

    if (digitalRead(blowerPin) == LOW || digitalRead(fanPin) == LOW) {
      ++currentFilterSec;                               // Counts how many seconds filter is used for HVAC or fan-only.
      Blynk.virtualWrite(11, currentFilterSec);
      currentFilterHours = (currentFilterSec / 3600);   // Converts those seconds to hours for display and other uses.
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

/*
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
*/
