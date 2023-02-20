/**
      This test illustrates the use if yield methods and internal StatusRequest objects
      THIS TEST HAS BEEN TESTED ON NODEMCU V.2 (ESP8266)

      The WiFi initialization and NTP update is executed in parallel to blinking the onboard LED
      and an external LED connected to D2 (GPIO04)
      Try running with and without correct WiFi parameters to observe the difference in behaviour
*/

//#define _TASK_SLEEP_ON_IDLE_RUN
#define _TASK_STATUS_REQUEST
#include <TaskScheduler.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>
#include <WiFiUdp.h>
#include <Adafruit_NeoPixel.h>
#include <TimeLib.h>
#include <FS.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <string.h>
#include "pitches.h"
#include "sunset.h"

/* Cass Bay */
#define LATITUDE        -43.601131
#define LONGITUDE       172.689831
#define CST_OFFSET      12
#define DST_OFFSET      13

SunSet sun;

// Which pin on the ESP8266 is connected to the NeoPixels?
#define NEOPIXEL_PIN 3      // This is the D9 pin
#define PIEZO_PIN 5         // This is D1
#define analogInPin  A0     // ESP8266 Analog Pin ADC0 = A0

//************* Declare NeoPixel ******************************
//Using 1M WS2812B 5050 RGB Non-Waterproof 16 LED Ring
// use NEO_KHZ800 but maybe 400 makes wifi more stable???
#define NUM_LEDS 16
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_LEDS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

ESP8266WebServer server(80);       // create a web server on port 80
WebSocketsServer webSocket(81);    // create a websocket server on port 81
uint8_t websocketId_num = 0;

File fsUploadFile;                                    // a File variable to temporarily store the received file

// must be longer than longest message
char buf[200];

time_t currentTime;
time_t nextCalcTime;
time_t nextAlarmTime;
//************* Declare structures ******************************
//Create structure for LED RGB information
struct RGB {
  byte r, g, b;
};

//Create structure for time information
struct TIME {
  byte Hour, Minute;
};

//************* Editable Options ******************************

//The colour of the "12" to give visual reference to the top
RGB Twelve = {0, 0, 60}; // blue
//The colour of the "quarters" 3, 6 & 9 to give visual reference
RGB Quarters = { 0, 0, 60 }; //blue
//The colour of the "divisions" 1,2,4,5,7,8,10 & 11 to give visual reference
RGB Divisions = { 0, 0, 6 }; //blue
//All the other pixels with no information
RGB Background = { 0, 0, 0 }; //blue

//The Hour hand
RGB Hour = { 0, 255, 0 };//green
//The Minute hand
RGB Minute = { 255, 127, 0 };//orange medium
//The Second hand
RGB Second = { 0, 0, 100 }; //blue

//Night Options
//The colour of the "12" to give visual reference to the top
RGB TwelveNight = {0, 0, 0}; // off
//The colour of the "quarters" 3, 6 & 9 to give visual reference
RGB QuartersNight = { 0, 0, 0 }; //off
//The colour of the "divisions" 1,2,4,5,7,8,10 & 11 to give visual reference
RGB DivisionsNight = { 0, 0, 0 }; //off
//All the other pixels with no information
RGB BackgroundNight = { 0, 0, 0 }; //off

//The Hour hand
RGB HourNight = { 100, 0, 0 };//red
//The Minute hand
RGB MinuteNight = { 0, 0, 0 };//off
//The Second hand
RGB SecondNight = { 0, 0, 0 }; //off

//To save color set by slider
RGB SliderColor = {0, 0, 0};

// Make clock go forwards or backwards (dependant on hardware)
bool ClockGoBackwards = true;

//Set your timezone in hours difference rom GMT
int hours_Offset_From_GMT = 12;

//Set brightness by time for night and day mode
TIME WeekNight = {18, 00}; // Night time to go dim
TIME WeekMorning = {7, 15}; //Morning time to go bright
TIME WeekendNight = {18, 00}; // Night time to go dim
TIME WeekendMorning = {7, 15}; //Morning time to go bright
TIME Sunrise;
TIME Sunset;
TIME CivilSunrise;
TIME CivilSunset;
TIME NauticalSunrise;
TIME NauticalSunset;
TIME AstroSunrise;
TIME AstroSunset;

String daysOfWeek[8] = {"dummy", "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
String monthNames[13] = {"dummy", "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};

bool sound_alarm_flag = false;
bool light_alarm_flag = false;
bool led_color_alarm_flag = false;
uint32_t led_color_alarm_rgb;
tmElements_t calcTime = {0};

struct ALARM {
  bool alarmSet = false;
  uint8_t alarmType;
  uint16_t duration;
  uint32_t repeat;
  tmElements_t alarmTime;
};

ALARM alarmInfo;

// notes in the melody for the sound alarm:
int melody[] = { // Shave and a hair cut (3x) two bits terminate with -1 = STOP
  NOTE_C5, NOTE_G4, NOTE_G4, NOTE_A4, NOTE_G4, REST,
  NOTE_C5, NOTE_G4, NOTE_G4, NOTE_A4, NOTE_G4, REST,
  NOTE_C5, NOTE_G4, NOTE_G4, NOTE_A4, NOTE_G4, REST, NOTE_B4, NOTE_C5, STOP
};
int whole_note_duration = 1000; // milliseconds
// note durations: 4 = quarter note, 8 = eighth note, etc.:
int noteDurations[] = {
  4, 8, 8, 4, 4, 4, 4, 8, 8, 4, 4, 4, 4, 8, 8, 4, 4, 4, 4, 4
};
int melodyNoteIndex;

#include "localwificonfig.h"
Scheduler ts;

// Callback methods prototypes
void connectInit();
void ledCallback();
bool ledOnEnable();
void ledOnDisable();
void ledRed();
void ledBlue();
void ntpUpdateInit();
void serverRun();
void webSocketRun();
void playMelody();
bool playMelodyOnEnable();
void playMelodyOnDisable();

// Tasks

Task  tConnect    (TASK_SECOND, TASK_FOREVER, &connectInit, &ts, true);
Task  tRunServer  (TASK_SECOND / 16, TASK_FOREVER, &serverRun, &ts, false);
Task  tRunWebSocket  (TASK_SECOND / 16, TASK_FOREVER, &webSocketRun, &ts, false);
Task  tLED        (TASK_IMMEDIATE, TASK_FOREVER, &ledCallback, &ts, false, &ledOnEnable, &ledOnDisable);
Task  tplayMelody (TASK_IMMEDIATE, TASK_FOREVER, &playMelody, &ts, false, &playMelodyOnEnable, &playMelodyOnDisable);

// Tasks running on events
Task  tNtpUpdate  (&ntpUpdateInit, &ts);


long  ledDelayRed, ledDelayBlue;

#define CONNECT_TIMEOUT   30      // Seconds
#define CONNECT_OK        0       // Status of successful connection to WiFi
#define CONNECT_FAILED    (-99)   // Status of failed connection to WiFi

// NTP Related Definitions
#define NTP_PACKET_SIZE  48       // NTP time stamp is in the first 48 bytes of the message

IPAddress     timeServerIP;       // time.nist.gov NTP server address
const char*   ntpServerName = "nz.pool.ntp.org";
byte          packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets
unsigned long epoch;

WiFiUDP udp;                      // A UDP instance to let us send and receive packets over UDP

#define LOCAL_NTP_PORT  2390      // Local UDP port for NTP update



void setup() {
  Serial.begin(115200);
  Serial.println(F("Modified TaskScheduler test #14 - Yield and internal StatusRequests"));
  Serial.println(F("=========================================================="));
  Serial.println();
  sun.setPosition(LATITUDE, LONGITUDE, DST_OFFSET);
  startWebSocket();            // Start a WebSocket server
  startServer();               // Start a HTTP server with a file read handler and an upload handler
  strip.begin(); // This initializes the NeoPixel library.
  startSPIFFS();               // Start the SPIFFS and list all contents

  tNtpUpdate.waitFor( tConnect.getInternalStatusRequest() );  // NTP Task will start only after connection is made
}

void loop() {
  ts.execute();                   // Only Scheduler should be executed in the loop
}

/**
   Initiate connection to the WiFi network
*/
void connectInit() {
  Serial.print(millis());
  Serial.println(F(": connectInit."));
  Serial.println(F("WiFi parameters: "));
  Serial.print(F("SSID: ")); Serial.println(homeSSID);
  Serial.print(F("PWD : ")); Serial.println(homePW);

  WiFi.mode(WIFI_STA);
  WiFi.hostname("esp8266");
  WiFi.begin(homeSSID, homePW);
  yield();

  ledDelayRed = TASK_SECOND / 2;
  ledDelayBlue = TASK_SECOND / 4;
  tLED.enable();

  tConnect.yield(&connectCheck);            // This will pass control back to Scheduler and then continue with connection checking
}

/**
   Periodically check if connected to WiFi
   Re-request connection every 5 seconds
   Stop trying after a timeout
*/
void connectCheck() {
  Serial.print(millis());
  Serial.println(F(": connectCheck."));

  if (WiFi.status() == WL_CONNECTED) {                // Connection established
    Serial.print(millis());
    Serial.print(F(": Connected to AP. Local ip: "));
    Serial.println(WiFi.localIP());
    tConnect.disable();
    tRunServer.enable();
    tRunWebSocket.enable();
  }
  else {

    if (tConnect.getRunCounter() % 5 == 0) {          // re-request connection every 5 seconds

      Serial.print(millis());
      Serial.println(F(": Re-requesting connection to AP..."));

      WiFi.disconnect(true);
      yield();                                        // This is an esp8266 standard yield to allow linux wifi stack run
      WiFi.hostname("esp8266");
      WiFi.mode(WIFI_STA);
      WiFi.begin(homeSSID, homePW);
      yield();                                        // This is an esp8266 standard yield to allow linux wifi stack run
    }

    if (tConnect.getRunCounter() == CONNECT_TIMEOUT) {  // Connection Timeout
      tConnect.getInternalStatusRequest()->signal(CONNECT_FAILED);  // Signal unsuccessful completion
      tConnect.disable();

      Serial.print(millis());
      Serial.println(F(": connectOnDisable."));
      Serial.print(millis());
      Serial.println(F(": Unable to connect to WiFi."));

      ledDelayRed = TASK_SECOND / 16;                  // Blink LEDs quickly due to error
      ledDelayBlue = TASK_SECOND / 16;
      tLED.enable();
    }
  }
}

/**
   Initiate NTP update if connection was established
*/
void ntpUpdateInit() {
  Serial.print(millis());
  Serial.println(F(": ntpUpdateInit."));

  if ( tConnect.getInternalStatusRequest()->getStatus() != CONNECT_OK ) {  // Check status of the Connect Task
    Serial.print(millis());
    Serial.println(F(": cannot update NTP - not connected."));
    return;
  }

  udp.begin(LOCAL_NTP_PORT);
  if ( WiFi.hostByName(ntpServerName, timeServerIP) ) { //get a random server from the pool

    Serial.print(millis());
    Serial.print(F(": timeServerIP = "));
    Serial.println(timeServerIP);

    sendNTPpacket(timeServerIP); // send an NTP packet to a time server
  }
  else {
    Serial.print(millis());
    Serial.println(F(": NTP server address lookup failed."));
    tLED.disable();
    udp.stop();
    tNtpUpdate.disable();
    return;
  }

  ledDelayRed = TASK_SECOND / 8;
  ledDelayBlue = TASK_SECOND / 8;
  tLED.enable();

  tNtpUpdate.set( TASK_SECOND, CONNECT_TIMEOUT, &ntpCheck );
  tNtpUpdate.enableDelayed();
}


void serverRun () {
  server.handleClient();                      // run the server
}

void webSocketRun () {
  webSocket.loop();                           // constantly check for websocket events
}


// Modified for Southern Hemisphere DST
bool IsDst()
{
  int previousSunday = day() - weekday();
  //Serial.print("    IsDst ");
  //Serial.print(month());
  //Serial.println(previousSunday);
  if (month() < 4 || month() > 9)  return true;
  if (month() > 4 && month() < 9)  return false;


  if (month() == 4) return previousSunday < 8;
  if (month() == 9) return previousSunday > 23;
  return false; // this line never gonna happend
}

/**
   Check if NTP packet was received
   Re-request every 5 seconds
   Stop trying after a timeout
*/
void ntpCheck() {
  Serial.print(millis());
  Serial.println(F(": ntpCheck."));

  if ( tNtpUpdate.getRunCounter() % 5 == 0) {

    Serial.print(millis());
    Serial.println(F(": Re-requesting NTP update..."));

    udp.stop();
    yield();
    udp.begin(LOCAL_NTP_PORT);
    sendNTPpacket(timeServerIP);
    return;
  }

  if ( doNtpUpdateCheck()) {
    Serial.print(millis());
    Serial.println(F(": NTP Update successful"));

    Serial.print(millis());
    Serial.print(F(": Unix time = "));
    Serial.println(epoch);
    setTime(epoch);
    adjustTime(hours_Offset_From_GMT * 3600);
    if (IsDst()) adjustTime(3600); // offset the system time by an hour for Daylight Savings

    ledDelayRed = TASK_SECOND / 2;
    ledDelayBlue = TASK_SECOND / 2;
    //tLED.disable();
    tNtpUpdate.disable();
    udp.stop();
  }
  else {
    if ( tNtpUpdate.isLastIteration() ) {
      Serial.print(millis());
      Serial.println(F(": NTP Update failed"));
      ledDelayRed = TASK_SECOND / 2;
      ledDelayBlue = TASK_SECOND / 16;
      //tLED.disable();
      udp.stop();
    }
  }
}

/**
   Send NTP packet to NTP server
*/
void sendNTPpacket(IPAddress & address)
{
  Serial.print(millis());
  Serial.println(F(": sendNTPpacket."));

  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
  yield();
}

/**
   Check if a packet was recieved.
   Process NTP information if yes
*/
bool doNtpUpdateCheck() {

  Serial.print(millis());
  Serial.println(F(": doNtpUpdateCheck."));

  yield();
  int cb = udp.parsePacket();
  if (cb) {
    // We've received a packet, read the data from it
    udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

    //the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, esxtract the two words:

    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    unsigned long secsSince1900 = highWord << 16 | lowWord;

    // now convert NTP time into everyday time:
    // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
    const unsigned long seventyYears = 2208988800UL;
    // subtract seventy years:
    epoch = secsSince1900 - seventyYears;
    return (epoch != 0);
  }
  return false;
}

void startSPIFFS() { // Start the SPIFFS and list all contents
  SPIFFS.begin();                             // Start the SPI Flash File System (SPIFFS)
  Serial.println("SPIFFS started. Contents:");
  {
    Dir dir = SPIFFS.openDir("/");
    while (dir.next()) {                      // List the file system contents
      String fileName = dir.fileName();
      size_t fileSize = dir.fileSize();
      Serial.printf("\tFS File: %s, size: %s\r\n", fileName.c_str(), formatBytes(fileSize).c_str());
    }
    Serial.printf("\n");
  }
}


void startWebSocket() { // Start a WebSocket server
  webSocket.begin();                          // start the websocket server
  webSocket.onEvent(webSocketEvent);          // if there's an incomming websocket message, go to function 'webSocketEvent'
  Serial.println("WebSocket server started.");
}

void startServer() { // Start a HTTP server with a file read handler and an upload handler

  //list directory
  server.on("/list", HTTP_GET, handleFileList);
  //load editor
  server.on("/edit", HTTP_GET, []() {
    if (!handleFileRead("/edit.htm")) {
      server.send(404, "text/plain", "FileNotFound");
    }
  });
  //create file
  server.on("/edit", HTTP_PUT, handleFileCreate);
  //delete file
  server.on("/edit", HTTP_DELETE, handleFileDelete);
  //first callback is called after the request has ended with all parsed arguments
  //second callback handles file uploads at that location
  server.on("/edit", HTTP_POST, []() {
    server.send(200, "text/plain", "");
  }, handleFileUpload);


  server.on("/restart", []() {
    server.send(200, "text/plain", "Restarting ...");
    ESP.restart();
  });

  server.on("/add124sec", []() {
    server.send(200, "text/plain", "Adjust time by 124 sec");
    adjustTime(124);
  });

  server.on("/spiff", []() {
    Dir dir = SPIFFS.openDir("/");
    String output = "SPIFF: \r\n\n";
    while (dir.next()) {                      // List the file system contents
      String fileName = dir.fileName();
      size_t fileSize = dir.fileSize();
      //Serial.println("NAME " + fileName);
      sprintf(buf, "%s\t %s\r\n", fileName.c_str(), formatBytes(fileSize).c_str());
      output += buf;
    }
    server.send(200, "text/plain", output);
  });

  server.on("/whattime", []() {
    sprintf(buf, "%d:%02d:%02d %s %d %s %d", hour(), minute(), second(), daysOfWeek[weekday()].c_str(), day(), monthNames[month()].c_str(), year());
    server.send(200, "text/plain", buf);
    //delay(1000);
  });


  server.onNotFound(handleNotFound);          // if someone requests any other file or page, go to function 'handleNotFound'
  // and check if the file exists

  server.begin();                             // start the HTTP server
  Serial.println("HTTP server started.");
}

/*__________________________________________________________SERVER_HANDLERS__________________________________________________________*/

void handleNotFound() { // if the requested file or page doesn't exist, return a 404 not found error
  if (!handleFileRead(server.uri())) {        // check if the file exists in the flash memory (SPIFFS), if so, send it
    server.send(404, "text/plain", "404: File Not Found");
  }
}

bool handleFileRead(String path) { // send the right file to the client (if it exists)
  Serial.println("handleFileRead: " + path);
  if (path.endsWith("/")) path += "index.htm";          // If a folder is requested, send the index file
  String contentType = getContentType(path);             // Get the MIME type
  String pathWithGz = path + ".gz";
  if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)) { // If the file exists, either as a compressed archive, or normal
    if (SPIFFS.exists(pathWithGz))                         // If there's a compressed version available
      path += ".gz";                                         // Use the compressed verion
    File file = SPIFFS.open(path, "r");                    // Open the file
    size_t sent = server.streamFile(file, contentType);    // Send it to the client
    file.close();                                          // Close the file again
    Serial.println(String("\tSent file: ") + path);
    return true;
  }
  Serial.println(String("\tFile Not Found: ") + path);   // If the file doesn't exist, return false
  return false;
}

void handleFileUpload() { // upload a new file to the SPIFFS
  if (server.uri() != "/edit") {
    return;
  }
  HTTPUpload& upload = server.upload();
  String path;
  if (upload.status == UPLOAD_FILE_START) {
    path = upload.filename;
    if (!path.startsWith("/")) path = "/" + path;
    if (!path.endsWith(".gz")) {                         // The file server always prefers a compressed version of a file
      String pathWithGz = path + ".gz";                  // So if an uploaded file is not compressed, the existing compressed
      if (SPIFFS.exists(pathWithGz))                     // version of that file must be deleted (if it exists)
        SPIFFS.remove(pathWithGz);
    }
    Serial.print("handleFileUpload Name: "); Serial.println(path);
    fsUploadFile = SPIFFS.open(path, "w");            // Open the file for writing in SPIFFS (create if it doesn't exist)
    path = String();
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize); // Write the received bytes to the file
  } else if (upload.status == UPLOAD_FILE_END) {
    if (fsUploadFile) {                                   // If the file was successfully created
      fsUploadFile.close();                               // Close the file again
      Serial.print("handleFileUpload Size: "); Serial.println(upload.totalSize);
    } else {
      server.send(500, "text/plain", "500: couldn't create file");
    }
  }
}

void handleFileDelete() {
  if (server.args() == 0) {
    return server.send(500, "text/plain", "BAD ARGS");
  }
  String path = server.arg(0);
  Serial.println("Serial: " + path);
  if (path == "/") {
    return server.send(500, "text/plain", "BAD PATH");
  }
  if (!SPIFFS.exists(path)) {
    return server.send(404, "text/plain", "FileNotFound");
  }
  SPIFFS.remove(path);
  server.send(200, "text/plain", "");
  path = String();
}

void handleFileCreate() {
  if (server.args() == 0) {
    return server.send(500, "text/plain", "BAD ARGS");
  }
  String path = server.arg(0);
  Serial.println("handleFileCreate: " + path);
  if (path == "/") {
    return server.send(500, "text/plain", "BAD PATH");
  }
  if (SPIFFS.exists(path)) {
    return server.send(500, "text/plain", "FILE EXISTS");
  }
  File file = SPIFFS.open(path, "w");
  if (file) {
    file.close();
  } else {
    return server.send(500, "text/plain", "CREATE FAILED");
  }
  server.send(200, "text/plain", "");
  path = String();
}

void handleFileList() {
  if (!server.hasArg("dir")) {
    server.send(500, "text/plain", "BAD ARGS");
    return;
  }

  String path = server.arg("dir");
  Serial.println("handleFileList: " + path);
  Dir dir = SPIFFS.openDir(path);
  path = String();

  String output = "[";
  while (dir.next()) {
    File entry = dir.openFile("r");
    if (output != "[") {
      output += ',';
    }
    bool isDir = false;
    output += "{\"type\":\"";
    output += (isDir) ? "dir" : "file";
    output += "\",\"name\":\"";
    if (entry.name()[0] == '/') {
      output += &(entry.name()[1]);
    } else {
      output += entry.name();
    }
    output += "\"}";
    entry.close();
  }

  output += "]";
  server.send(200, "application/json", output);
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) { // When a WebSocket message is received
  //NOTE messages get queued up
  websocketId_num = num; // save so can send to websock from other places
  switch (type) {
    case WStype_DISCONNECTED:             // if the websocket is disconnected
      Serial.printf("[%u] Disconnected!\n", num);
      break;
    case WStype_CONNECTED: {              // if a new websocket connection is established
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
      }
      break;
    case WStype_TEXT:                     // if new text data is received
      Serial.printf("[%u] payload: %s length: %d\n", num, payload, length);
      if (payload[0] == '#') {            // we get RGB data
        uint32_t rgb = (uint32_t) strtol((const char *) &payload[1], NULL, 16);   // decode rgb data
        int r = ((rgb >> 20) & 0x3FF);                     // 10 bits per color, so R: bits 20-29
        int g = ((rgb >> 10) & 0x3FF);                     // G: bits 10-19
        int b =          rgb & 0x3FF;                      // B: bits  0-9
        sprintf(buf, "led color r = %d, g = %d, b = %d", r, g, b);
        SliderColor  = {r >> 2, g >> 2, b >> 2};
        //webSocket.sendTXT(num, buf);
        led_color_alarm_flag = true;
        led_color_alarm_rgb = strip.Color(r >> 2, g >> 2, b >> 2); // colors 0 to 255
        //analogWrite(ESP_BUILTIN_LED, b); INTERFER with LED strip
        //Serial.printf("%d\n", b);
      } else if (payload[0] == 'V') {                      // browser sent V to save config file
        if (!saveConfig()) {
          Serial.println("Failed to save config");
          sprintf(buf, "Failed to save config");
          webSocket.sendTXT(num, buf);
        } else {
          Serial.println("Config saved");
          sprintf(buf, "Config saved");
          webSocket.sendTXT(num, buf);
        }
      } else if (payload[0] == 'B') {                      // browser sent B to set Background color from SliderColor
        Background = SliderColor;
      } else if (payload[0] == 't') {                      // browser sent t to set Twelve color from SliderColor
        Twelve = SliderColor;
      } else if (payload[0] == 'q') {                      // browser sent q to set Quarters color from SliderColor
        Quarters = SliderColor;
      } else if (payload[0] == 'd') {                      // browser sent d to set Divisions color from SliderColor
        Divisions = SliderColor;
      } else if (payload[0] == 'H') {                      // browser sent H to set Hour hand color from SliderColor
        Hour = SliderColor;
      } else if (payload[0] == 'M') {                      // browser sent M to set Minute hand color from SliderColor
        Minute = SliderColor;
      } else if (payload[0] == 's') {                      // browser sent s to set Second hand color from SliderColor
        Second = SliderColor;
      } else if (payload[0] == 'R') {                      // the browser sends an R when the rainbow effect is enabled
        light_alarm_flag = true;
      } else if (payload[0] == 'L') {                      // the browser sends an L when the meLody effect is enabled
        sound_alarm_flag = true;
        tplayMelody.enable();
        //digitalWrite(ESP_BUILTIN_LED, 1);  // turn off the LED
      } else if (payload[0] == 'W') {                      // the browser sends an W for What time?
        sprintf(buf, "%d:%02d:%02d %s %d %s %d", hour(), minute(), second(), daysOfWeek[weekday()].c_str(), day(), monthNames[month()].c_str(), year());
        webSocket.sendTXT(num, buf);
        //digitalWrite(ESP_BUILTIN_LED, 0);  // turn on the LED
      } else if (payload[0] == 'A') {                      // the browser sends an A to set alarm
          // TODO Now only sets one alarm, but will allow many
          char Aday[4]; //3 char
          char Amonth[4]; //3 char
          int  AmonthNum;
          int Adate;
          int Ayear;
          int Ahour;
          int Aminute;

          //sprintf(buf, "Set Alarm for %s length: %d", payload, length);
          //webSocket.sendTXT(num, buf);
          sscanf((char *) payload, "A%d %s %s %2d %4d %2d:%2d", &AmonthNum, Aday, Amonth, &Adate, &Ayear, &Ahour, &Aminute);
          Serial.printf("Set alarm for %s %s %2d %2d %4d %2d:%2d\n", Aday, Amonth, Adate, AmonthNum + 1, Ayear, Ahour, Aminute);
          sprintf(buf, "Set alarm for %s %s %2d %2d %4d %2d:%2d", Aday, Amonth, Adate, AmonthNum + 1, Ayear, Ahour, Aminute);
          webSocket.sendTXT(num, buf);

          setalarmtime(10000, SECS_PER_DAY, 0, Aminute, Ahour, Adate, AmonthNum + 1, Ayear);
          alarmInfo.alarmSet = true;

          sprintf(buf, "Alarm Set to %d:%02d:%02d %s %d %s %d, duration %d ms repeat %d sec",
                hour(makeTime(alarmInfo.alarmTime)), minute(makeTime(alarmInfo.alarmTime)), second(makeTime(alarmInfo.alarmTime)),
                daysOfWeek[weekday(makeTime(alarmInfo.alarmTime))].c_str(), day(makeTime(alarmInfo.alarmTime)),
                monthNames[month(makeTime(alarmInfo.alarmTime))].c_str(), year(makeTime(alarmInfo.alarmTime)),
                alarmInfo.duration, alarmInfo.repeat);
          webSocket.sendTXT(num, buf);
      } else if (payload[0] == 'S') {                      // the browser sends an S to compute sunsets
        Serial.printf("Compute Sunsets\n");
        calcSun();
      }
      break;
  }
}

void setalarmtime(uint16_t t, uint32_t r, uint8_t s, uint8_t m, uint8_t h, uint8_t d, uint8_t mth, uint16_t y) {
  Serial.printf("setalarmtime: %d %d %d %d %d %d %d %d\n", t, r, s, m, h, d, mth, y);

  alarmInfo.duration = t;
  alarmInfo.repeat = r;
  alarmInfo.alarmTime.Second = s;
  alarmInfo.alarmTime.Minute = m;
  alarmInfo.alarmTime.Hour = h;
  alarmInfo.alarmTime.Day = d;
  alarmInfo.alarmTime.Month = mth;
  //NOTE year is excess from 1970
  alarmInfo.alarmTime.Year = y - 1970;
}

void calcSun()
{
  double sunrise;
  double sunset;
  double civilsunrise;
  double civilsunset;
  double astrosunrise;
  double astrosunset;
  double nauticalsunrise;
  double nauticalsunset;

  /* Get the current time, and set the Sunrise code to use the current date */
  currentTime = now();

  sprintf(buf, "calcSun at %d:%02d:%02d %s %d %s %d", hour(currentTime), minute(currentTime),
          second(currentTime), daysOfWeek[weekday(currentTime)].c_str(), day(currentTime),
          monthNames[month(currentTime)].c_str(), year(currentTime));
  webSocket.sendTXT(websocketId_num, buf);

  sun.setCurrentDate(year(currentTime), month(currentTime), day(currentTime));
  /* Check to see if we need to update our timezone value */
  if (IsDst())
    sun.setTZOffset(DST_OFFSET);
  else
    sun.setTZOffset(CST_OFFSET);

  // These are all minutes after midnight
  sunrise = sun.calcSunrise();
  sunset = sun.calcSunset();
  civilsunrise = sun.calcCivilSunrise();
  civilsunset = sun.calcCivilSunset();
  nauticalsunrise = sun.calcNauticalSunrise();
  nauticalsunset = sun.calcNauticalSunset();
  astrosunrise = sun.calcAstronomicalSunrise();
  astrosunset = sun.calcAstronomicalSunset();

  Sunset.Hour = sunset / 60;
  Sunset.Minute = sunset - 60 * Sunset.Hour + 0.5;
  CivilSunset.Hour = civilsunset / 60;
  CivilSunset.Minute = civilsunset - 60 * CivilSunset.Hour + 0.5;
  NauticalSunset.Hour = nauticalsunset / 60;
  NauticalSunset.Minute = nauticalsunset - 60 * NauticalSunset.Hour + 0.5;
  AstroSunset.Hour = astrosunset / 60;
  AstroSunset.Minute = astrosunset - 60 * AstroSunset.Hour + 0.5;

  Sunrise.Hour = sunrise / 60;
  Sunrise.Minute = sunrise - 60 * Sunrise.Hour + 0.5;
  CivilSunrise.Hour = civilsunrise / 60;
  CivilSunrise.Minute = civilsunrise - 60 * CivilSunrise.Hour + 0.5;
  NauticalSunrise.Hour = nauticalsunrise / 60;
  NauticalSunrise.Minute = nauticalsunrise - 60 * NauticalSunrise.Hour + 0.5;
  AstroSunrise.Hour = astrosunrise / 60;
  AstroSunrise.Minute = astrosunrise - 60 * AstroSunrise.Hour + 0.5;

  WeekMorning = Sunrise;
  WeekNight = CivilSunset;

  WeekendNight = WeekNight;
  WeekendMorning = WeekMorning;

  nextCalcTime = currentTime;
  nextCalcTime += 24 * 3600;
  breakTime(nextCalcTime, calcTime);
  calcTime.Hour = 0;
  calcTime.Minute = 0;
  calcTime.Second = 1;


  Serial.print("Sunrise is ");
  Serial.print(sunrise);
  Serial.println(" minutes past midnight.");
  Serial.print("Sunset is ");
  Serial.print(sunset);
  Serial.print(" minutes past midnight.");


  sprintf(buf, "Sunset at %d:%02d, Sunrise at %d:%02d", Sunset.Hour, Sunset.Minute, Sunrise.Hour, Sunrise.Minute);
  webSocket.sendTXT(websocketId_num, buf);

  sprintf(buf, "CivilSunset at %d:%02d, CivilSunrise at %d:%02d", CivilSunset.Hour, CivilSunset.Minute, CivilSunrise.Hour, CivilSunrise.Minute);
  webSocket.sendTXT(websocketId_num, buf);

  sprintf(buf, "NauticalSunset at %d:%02d, NauticalSunrise at %d:%02d", NauticalSunset.Hour, NauticalSunset.Minute, NauticalSunrise.Hour, NauticalSunrise.Minute);
  webSocket.sendTXT(websocketId_num, buf);

  sprintf(buf, "AstroSunset at %d:%02d, AstroSunrise at %d:%02d", AstroSunset.Hour, AstroSunset.Minute, AstroSunrise.Hour, AstroSunrise.Minute);
  webSocket.sendTXT(websocketId_num, buf);

  sprintf(buf, "Next calcSun %d:%02d:%02d %s %d %s %d", hour(makeTime(calcTime)), minute(makeTime(calcTime)),
          second(makeTime(calcTime)), daysOfWeek[weekday(makeTime(calcTime))].c_str(), day(makeTime(calcTime)),
          monthNames[month(makeTime(calcTime))].c_str(), year(makeTime(calcTime)));
  webSocket.sendTXT(websocketId_num, buf);

  sprintf(buf, "dim at %d:%02d, brighten at %d:%02d", WeekNight.Hour, WeekNight.Minute, WeekMorning.Hour, WeekMorning.Minute);
  webSocket.sendTXT(websocketId_num, buf);
}

/*__________________________________________________________SETUP_FUNCTIONS__________________________________________________________*/
// Taken from ConfigFile example
bool loadConfig() {
  File configFile = SPIFFS.open("/config.json", "r");
  if (!configFile) {
    Serial.println("Failed to open config file");
    return false;
  }

  size_t size = configFile.size();
  if (size > 1024) {
    Serial.println("Config file size is too large");
    return false;
  }

  // Allocate a buffer to store contents of the file.
  std::unique_ptr<char[]> buf(new char[size]);

  // We don't use String here because ArduinoJson library requires the input
  // buffer to be mutable. If you don't use ArduinoJson, you may as well
  // use configFile.readString instead.
  configFile.readBytes(buf.get(), size);

  StaticJsonDocument<384> doc;
  auto error = deserializeJson(doc, buf.get());
  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return false;
  }
  alarmInfo.alarmSet = doc["alarmSet"]; // false
  alarmInfo.alarmType = doc["alarmType"]; // 99
  alarmInfo.duration = doc["duration"]; // 1000000
  alarmInfo.repeat = doc["repeat"]; // 864000

  JsonObject alarmTime = doc["alarmTime"];
  alarmInfo.alarmTime.Second = alarmTime["sec"];
  alarmInfo.alarmTime.Minute = alarmTime["min"];
  alarmInfo.alarmTime.Hour = alarmTime["hour"];
  alarmInfo.alarmTime.Day = alarmTime["date"];
  alarmInfo.alarmTime.Month = alarmTime["month"];
  alarmInfo.alarmTime.Year = alarmTime["year"];
  return true;
}

bool saveConfig() {
  StaticJsonDocument<192> doc;
  doc["alarmSet"] = alarmInfo.alarmSet;
  doc["alarmType"] = alarmInfo.alarmType;
  doc["duration"] = alarmInfo.duration;
  doc["repeat"] = alarmInfo.repeat;
  JsonObject alarmTime = doc.createNestedObject("alarmTime");
  alarmTime["sec"] = alarmInfo.alarmTime.Second;
  alarmTime["min"] = alarmInfo.alarmTime.Minute;
  alarmTime["hour"] = alarmInfo.alarmTime.Hour;
  alarmTime["date"] = alarmInfo.alarmTime.Day;
  alarmTime["month"] = alarmInfo.alarmTime.Month;
  alarmTime["year"] = alarmInfo.alarmTime.Year;
  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    Serial.println("Failed to open config file for writing");
    return false;
  }
  serializeJson(doc, configFile);
  return true;
}

/*_____________For Play Melody Task______________*/

bool playMelodyOnEnable() {
  melodyNoteIndex = 0;
  return true;
}

void playMelodyOnDisable() {
  noTone(PIEZO_PIN);
}
void playMelody() {
  if (melody[melodyNoteIndex] == STOP) {  //STOP is defined to be -1
    tplayMelody.disable();
    return;
  }
  // to calculate the note duration, take whole note durations divided by the note type.
  //e.g. quarter note = whole_note_duration / 4, eighth note = whole_note_duration/8, etc.
  int noteDuration = whole_note_duration / noteDurations[melodyNoteIndex];
  if (melody[melodyNoteIndex] > 30) {
    tone(PIEZO_PIN, melody[melodyNoteIndex], noteDuration);
  }
  melodyNoteIndex++;
  // to distinguish the notes, set a minimum time between them.
  // the note's duration + 30% seems to work well:
  int pauseBetweenNotes = noteDuration * 1.30;
  tplayMelody.delay( pauseBetweenNotes );
}


/*
   Flip the LED state based on the current state
*/
bool ledState;
void ledCallback() {
  if ( ledState ) ledBlue();
  else ledRed();
}

/**
   Make sure the LED starts red
*/
bool ledOnEnable() {
  ledRed();
  return true;
}

/**
   Make sure LED ends dimmed
*/
void ledOnDisable() {
  ledBlue();
}

/**
   Turn ring red
*/
void ledRed() {
  ledState = true;
  strip.fill(0x100000);
  strip.show();
  tLED.delay( ledDelayRed );
}

/**
   Turn ring blue.
*/
void ledBlue() {
  ledState = false;
  strip.fill(0x000010);
  strip.show();
  tLED.delay( ledDelayBlue );
}

/*__________________________________________________________HELPER_FUNCTIONS__________________________________________________________*/

String formatBytes(size_t bytes) { // convert sizes in bytes to KB and MB
  if (bytes < 1024) {
    return String(bytes) + "B";
  } else if (bytes < (1024 * 1024)) {
    return String(bytes / 1024.0) + "KB";
  } else if (bytes < (1024 * 1024 * 1024)) {
    return String(bytes / 1024.0 / 1024.0) + "MB";
  } else {
    return String(bytes / 1024.0 / 1024.0 / 1024.0) + "GB";
  }
}

String getContentType(String filename) {
  if (server.hasArg("download")) {
    return "application/octet-stream";
  } else if (filename.endsWith(".htm")) {
    return "text/html";
  } else if (filename.endsWith(".html")) {
    return "text/html";
  } else if (filename.endsWith(".css")) {
    return "text/css";
  } else if (filename.endsWith(".js")) {
    return "application/javascript";
  } else if (filename.endsWith(".png")) {
    return "image/png";
  } else if (filename.endsWith(".gif")) {
    return "image/gif";
  } else if (filename.endsWith(".jpg")) {
    return "image/jpeg";
  } else if (filename.endsWith(".ico")) {
    return "image/x-icon";
  } else if (filename.endsWith(".xml")) {
    return "text/xml";
  } else if (filename.endsWith(".pdf")) {
    return "application/x-pdf";
  } else if (filename.endsWith(".zip")) {
    return "application/x-zip";
  } else if (filename.endsWith(".gz")) {
    return "application/x-gzip";
  }
  return "text/plain";
}
