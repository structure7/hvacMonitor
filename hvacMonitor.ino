#include <SimpleTimer.h>        // Timer library http://playground.arduino.cc/Code/SimpleTimer
#define BLYNK_PRINT Serial      // Comment this out to disable prints and save space
//#define BLYNK_DEBUG           // Turn this on to see everything Blynk is doing
#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <OneWire.h>            // Temperature sensor library
#include <DallasTemperature.h>  // Temperature sensor library
#include <TimeLib.h>            // Used by WidgetRTC.h
#include <WidgetRTC.h>          // Blynk's RTC
#define ONE_WIRE_BUS 0          // GPIO location of the temperature sensors
//#include <ArduinoJson.h>        // For parsing information from Weather Underground API
#include <EEPROM.h>

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

DeviceAddress ds18b20RA = { 0x28, 0xEF, 0x97, 0x1E, 0x00, 0x00, 0x80, 0x54 }; // Return air probe - Device address found by going to File -> Examples -> DallasTemperature -> oneWireSearch
DeviceAddress ds18b20SA = { 0x28, 0xF1, 0xAC, 0x1E, 0x00, 0x00, 0x80, 0xE8 }; // Supply air probe

char auth[] = "fromBlynkApp"; // Assigned by Blynk's app

char ssid[] = "ssid";
char pass[] = "pw";

char* host = "data.sparkfun.com";
char* streamId   = "publicKey";
char* privateKey = "privateKey";

SimpleTimer timer;

WiFiClient client; // Try running without... might not need? I see this down already in the various subroutines

WidgetLED led1(V2); // Heartbeat (status)
WidgetRTC rtc;
BLYNK_ATTACH_WIDGET(rtc, V8);
WidgetTerminal terminal(V26);

int blowerPin = 2;  // 3.3V logic source from blower
int xStop = 1;
int xStart = 1;
int alarmTrigger = 0;
int alarmFor = 0;
int xFirstRun = 0;
int resetTattle = 0;
int splitAlarmTime = 180; // 180s (3 min) timeout after a high split reading

unsigned long onNow = now();
unsigned long offNow = now(); // To prevent error on first Tweet after blower starts - NOT WORKING YET

int offHour, offHour24, onHour, onHour24, offMinute, onMinute, offSecond, onSecond, offMonth, onMonth,
    offDay, onDay, runTime, tempSplit, secondsCount, alarmTime, startHour, startMin, startMonth, startDay;

int resetLatch = 0;

int currentRuntimeSec;      // Sum of today's blower runtime in seconds
int currentOfftimeSec = 0;
int currentRuntimeMin;      // Sum of today's blower runtime in minutes
int currentRuntimeStint;    // Current (last) duration of a blower fan run "cycle/session"
int currentRuntimeStintMin; // Current (last) duration of a blower fan run "cycle/session"
int yesterdayRuntime;       // Sum of yesterday's blower runtime in minutes
int totalRuntimeLatch = 0;

int dayCountLatch = 0;
int todaysDate, yesterdaysDate, yesterdaysMonth;
int msgLatch = 0;

float tempRA, tempSA;       // Return and supply air temperatures

// For EEPROM
int eeIndex = 0;  // This is the EEPROM address location that keeps track of next EEPROM address available for storing a blower runtime.
int eeCurrent;    // This is the next EEPROM address location that runtime will be stored to.
int eeWBsum;      // This is the sum of all EEPROM addresses (total runtime stored).

void setup()
{
  Serial.begin(9600);
  Blynk.begin(auth, ssid, pass);

  EEPROM.begin(201); // Reminder: "201" means addresses 0 to 200 are available (not 1 to 201 or 0 to 201).

  sensors.begin();
  sensors.setResolution(ds18b20RA, 10);
  sensors.setResolution(ds18b20SA, 10);

  while (Blynk.connect() == false) {
    // Wait until connected
  }

  // START EEPROM WRITEBACK ROUTINE
  if (EEPROM.read(eeIndex) > 1) // If this is true, than the index has advanced beyond the first position (meaning something was stored there (and maybe higher) to be written back.
  {
    for (int i = 1 ; i < 200 ; i++) {
      eeWBsum += EEPROM.read(i);
    }
  }

  // If there's something in EEPROM, write it to Today's Runtime
  if (eeWBsum > 0) {
    currentRuntimeSec = (eeWBsum * 60);
  }

  yesterdayRuntime = (EEPROM.read(200) * 4);
  // END EEPROM WRITEBACK ROUTINE

  rtc.begin();

  timer.setInterval(2500L, sendTemps);        // Temperature sensor polling interval
  timer.setInterval(2500L, sendBlowerStatus); // Blower fan status polling interval
  timer.setInterval(1000L, countRuntime);     // Counts blower runtime for daily accumulation displays.
  timer.setInterval(1000L, totalRuntime);     // Counts blower runtime for daily EEPROM storage.
  timer.setInterval(500L, timeKeeper);
  timer.setInterval(15432L, sfUpdate);        // ~15 sec run status updates to data.sparkfun.com

  heartbeatOn();
}

void loop()
{
  Blynk.run();
  timer.run();
}

void sfUpdate()  //  data.sparkfun update when unit is on
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
    terminal.println(" ");
    terminal.println(" ");
    terminal.println(" ");
    terminal.println(" ");
    terminal.println(" ");
    terminal.println(" ");
    terminal.println(" ");
    terminal.println(" ");
    terminal.println(" ");
    terminal.println("------------ UPTIME REPORT ------------");

    if (minDur < 121)
    {
      terminal.println(String("HVAC: ") + minDur + " mins ");
    }
    else if (minDur > 120)
    {
      terminal.println(String("HVAC: ") + hourDur + " hours ");
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

BLYNK_WRITE(V19) // App button to reset EEPROM stored total and address
{
  int pinData = param.asInt();

  if (pinData == 0)
  {
    Serial.println("EEPROM RESET");
    for (int i = 0 ; i < 201 ; i++) {
      EEPROM.write(i, 0);
    }
    EEPROM.write(eeIndex, 1); // Defined address 1 as the starting location.
    EEPROM.commit();
  }
}

BLYNK_WRITE(V20) // App button to reset ESP
{
  int pinData = param.asInt();

  if (pinData == 0)
  {
    ESP.restart();
  }
}

void sendTemps()
{
  sensors.requestTemperatures(); // Polls the sensors
  tempRA = sensors.getTempF(ds18b20RA);
  tempSA = sensors.getTempF(ds18b20SA);
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
}

void sendBlowerStatus()
{
  if (resetLatch == 0) // Sets ESP start/reset time and date to display in app until blower activity.
  {
    startHour = hour();
    startMin = minute();
    startMonth = month();
    startDay = day();
    resetLatch++;
  }

  if (digitalRead(blowerPin) == HIGH && resetTattle == 0 && startMin > 9)
  {
    Blynk.virtualWrite(16, String("SYSTEM RESET at ") + startHour + ":" + startMin + " " + startMonth + "/" + startDay);
  }
  else if (digitalRead(blowerPin) == HIGH && resetTattle == 0 && startMin < 10)
  {
    Blynk.virtualWrite(16, String("SYSTEM RESET at ") + startHour + ":0" + startMin + " " + startMonth + "/" + startDay);
  }
  else if (digitalRead(blowerPin) == LOW && resetTattle == 0 && startMin > 9)
  {
    Blynk.virtualWrite(16, String("SYSTEM RESET at ") + startHour + ":" + startMin + " " + startMonth + "/" + startDay);
  }
  else if (digitalRead(blowerPin) == LOW && resetTattle == 0 && startMin < 10)
  {
    Blynk.virtualWrite(16, String("SYSTEM RESET at ") + startHour + ":0" + startMin + " " + startMonth + "/" + startDay);
  }
  else if (digitalRead(blowerPin) == HIGH && resetTattle == 1) // Runs when blower is OFF.
  {
    if (offHour24 < 12 && offMinute > 9)
    {
      Blynk.virtualWrite(16, String("HVAC OFF since ") + offHour + ":" + offMinute + "AM on " + offMonth + "/" + offDay);
    }
    else if (offHour24 < 12 && offMinute < 10)
    {
      Blynk.virtualWrite(16, String("HVAC OFF since ") + offHour + ":0" + offMinute + "AM on " + offMonth + "/" + offDay);
    }
    else if (offHour24 > 11 && offMinute > 9)
    {
      Blynk.virtualWrite(16, String("HVAC OFF since ") + offHour + ":" + offMinute + "PM on " + offMonth + "/" + offDay);
    }
    else if (offHour24 > 11 && offMinute < 10)
    {
      Blynk.virtualWrite(16, String("HVAC OFF since ") + offHour + ":0" + offMinute + "PM on " + offMonth + "/" + offDay);
    }
  }
  else if (digitalRead(blowerPin) == LOW && resetTattle == 1)
  {
    if (onHour24 < 12 && onMinute > 9)
    {
      Blynk.virtualWrite(16, String("HVAC ON since ") + onHour + ":" + onMinute + "AM on " + onMonth + "/" + onDay);
    }
    else if (onHour24 < 12 && onMinute < 10)
    {
      Blynk.virtualWrite(16, String("HVAC ON since ") + onHour + ":0" + onMinute + "AM on " + onMonth + "/" + onDay);
    }
    else if (onHour24 > 11 && onMinute > 9)
    {
      Blynk.virtualWrite(16, String("HVAC ON since ") + onHour + ":" + onMinute + "PM on " + onMonth + "/" + onDay);
    }
    else if (onHour24 > 11 && onMinute < 10)
    {
      Blynk.virtualWrite(16, String("HVAC ON since ") + onHour + ":0" + onMinute + "PM on " + onMonth + "/" + onDay);
    }
  }
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
    // INTENT IS TO ONLY READ FROM THIS IN THE EVENT OF A DEVICE RESET ehhh.... then.. why not put this in setup?!

    currentRuntimeStintMin = (currentRuntimeStint / 60);
    eeCurrent = EEPROM.read(eeIndex);
    EEPROM.write(eeCurrent, currentRuntimeStintMin);
    EEPROM.commit();
    eeCurrent++;
    EEPROM.write(eeIndex, eeCurrent);
    EEPROM.commit();
    currentRuntimeStint = 0;
  }
}

void timeKeeper()
{
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
      alarmTime = 0; // Resets splitAlarmTime alarm.
    }
    else
    {
      if (alarmFor == 0)
      {
        alarmTime = secondsCount + splitAlarmTime; // This sets the alarm time (from split out of spec to notification being sent)
        alarmFor++; // Locks out alarm clock reset until alarmFor == 0.
      }
    }
    if (tempSplit <= 15 && secondsCount > alarmTime && alarmFor == 1)
    {
      Blynk.tweet(String("Low split (") + tempSplit + "°F) " + "recorded at " + hour() + ":" + minute() + ":" + second() + " " + month() + "/" + day() + "/" + year());
      Blynk.notify(String("Low HVAC split: ") + tempSplit + "°F. Call Wolfgang's");
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
    yesterdayRuntime = currentRuntimeMin; // Moves today's runtime to yesterday for the app display.
    dayCountLatch = 0; // Allows today's date to be reset.
    currentRuntimeSec = 0; // Reset today's sec timer.
    currentRuntimeMin = 0; // Reset today's min timer.

    // Resets EEPROM at the end of the day (except for yesterday's runtime)
    for (int i = 0 ; i < 200 ; i++) {
      EEPROM.write(i, 0);
    }
    EEPROM.write(eeIndex, 1); // Defined address 1 as the starting location.
    EEPROM.write(200, (yesterdayRuntime / 4)); // Write yesterday's runtime to EEPROM
    EEPROM.commit();
  }
  // Resets the timer on the next day if the blower isn't running.
  else if (digitalRead(blowerPin) == HIGH && todaysDate != day())
  {
    yesterdayRuntime = currentRuntimeMin;
    dayCountLatch = 0;
    currentRuntimeSec = 0;
    currentRuntimeMin = 0;

    // Resets EEPROM at the end of the day (except for yesterday's runtime)
    for (int i = 0 ; i < 200 ; i++) {
      EEPROM.write(i, 0);
    }
    EEPROM.write(eeIndex, 1); // Defined address 1 as the starting location.
    EEPROM.write(200, (yesterdayRuntime / 4)); // Write yesterday's runtime to EEPROM
    EEPROM.commit();
  }

  currentRuntimeMin = (currentRuntimeSec / 60);
  Blynk.virtualWrite(14, String(yesterdayRuntime) + " minutes");
  if (currentRuntimeMin < 1)
  {
    Blynk.virtualWrite(15, "None");
  }
  else if (currentRuntimeMin > 0 && eeCurrent < 1)
  {
    Blynk.virtualWrite(15, String(currentRuntimeMin) + " minutes");
  }
  else if (currentRuntimeMin > 0 && eeCurrent > 0)
  {
    Blynk.virtualWrite(15, String(currentRuntimeMin) + " mins (" + (eeCurrent - 1) + " runs)");
  }
}
