// Copyright Fabien Grisard 2019
// MIT License
// https://github.com/Zarfab/ESP32_ShowerThoughts
//
// For TTGO T5 v2.0 (ESP32 e-paper board)
// Connect a push button between 3.3v and GPIO33 through a 10k pull-down resistor.
//       3.3v --o
//              |
//              o |
//                |-| Push button
//              o |
//              |
//              o--- GPIO33
//              |
//             ---
//            |   |
//            |10k|
//            |   |
//             ---
//              |
//       GND  --o
// This sketch will grab a random 'Shower Thought' from Reddit,
// display it on its e-paper screen and got to sleep until it receives
// a press on the button.
//
//
// modify ssid and wifi password (psk) in WifiCredentials.h before uploading the sketch

#include <WiFi.h>
#include "WifiCredentials.h"
// to get pages from web
#include <WiFiClientSecure.h>


// TTGO_T5 drivers
#include <GxEPD.h>                  // https://github.com/ZinggJM/GxEPD
#include <GxIO/GxIO_SPI/GxIO_SPI.h>
#include <GxIO/GxIO.h>
#include <GxGDE0213B1/GxGDE0213B1.h>
#include <GxGDEH0213B72/GxGDEH0213B72.h>
#include <Fonts/FreeMono9pt7b.h> // https://github.com/adafruit/Adafruit-GFX-Library

// TTGO_T5 pin assignment
#define ELINK_SS 5
#define ELINK_BUSY 4
#define ELINK_RESET 16
#define ELINK_DC 17

// deep sleep constants, Push Button to GPIO 33 pulled down with a 10K Ohm resistor
#define BUTTON_PIN_BITMASK 0x200000000 // 2^33 in hex

#define DISPLAY_DELAY 8000 // time to display a full screen text (6 lines) in milliseconds


// Instantiate the WiFiClientSecure class to use it for creating a TLS connection
WiFiClientSecure client;
// Define web client constants
const char* host = "www.reddit.com";
const int httpsPort = 443;

// Instantiate epaper display
GxIO_Class io(SPI, ELINK_SS, ELINK_DC, ELINK_RESET);
GxEPD_Class display(io, ELINK_RESET, ELINK_BUSY);



//////////////////////////////////////////////////
/////         E-Paper display utils          /////
//////////////////////////////////////////////////
typedef enum
{
    RIGHT_ALIGNMENT = 0,
    LEFT_ALIGNMENT,
    CENTER_ALIGNMENT,
} Text_alignment;



void displayInit(void)
{
    static bool isInit = false;
    if (isInit)
    {
        return;
    }
    isInit = true;
    display.init();
    display.setRotation(1);
    display.eraseDisplay();
    display.setTextColor(GxEPD_BLACK);
    display.setFont(&FreeMono9pt7b);
    display.setTextSize(0);
}



void displayLine(const String &str, int16_t y, uint8_t alignment = LEFT_ALIGNMENT)
{
  int16_t x = 0;
  int16_t x1, y1;
  uint16_t w, h;
  display.setCursor(x, y);
  display.getTextBounds(str, x, y, &x1, &y1, &w, &h);

  switch (alignment)
  {
  case RIGHT_ALIGNMENT:
      display.setCursor(display.width() - w - x1, y);
      break;
  case LEFT_ALIGNMENT:
      display.setCursor(0, y);
      break;
  case CENTER_ALIGNMENT:
      display.setCursor(display.width() / 2 - ((w + x1) / 2), y);
      break;
  default:
      break;
  }
  display.println(str);
}



void displayText(String str) { 
  const int NB_LINES_MAX = 35;
  String lines[NB_LINES_MAX];
  int lineIndex = 0;
  while(str.length() > 22) {
    int currentPos = 22;
    while(str.charAt(currentPos) != ' ' && currentPos > 0) {
      currentPos--;
    }
    if(currentPos > 0) {
      lines[lineIndex] = str.substring(0, currentPos);
      str.remove(0, currentPos + 1);
    }
    else { // word longer than 22 chars
      lines[lineIndex] = str.substring(0, 22);
      str.remove(0, 22);
    }
    lineIndex++;
    if(lineIndex >= NB_LINES_MAX -1) {
      break;
    }
  }
  lines[lineIndex] = str;
  int nbLines = lineIndex + 1;
  /*Serial.printf("%i lines to display :\n", nbLines);
  for(int i = 0; i < nbLines; i++) {
    Serial.printf("[%i] : ", i);
    Serial.println(lines[i]);
  }*/

  displayInit();
  if(nbLines == 7) {
    display.fillScreen(GxEPD_WHITE);
    for(int i = 0; i < 7; i++) {
      int yOffset = 15;
      displayLine(lines[i], yOffset + (i * 17), CENTER_ALIGNMENT);
    }
    display.update();
  }
  else {
    for(int line = 0; line < nbLines; line += 6) {
      display.fillScreen(GxEPD_WHITE);
      for(int i = 0; i < 6; i++) {
        int yOffset = 28;
        if(nbLines <= 6) {
          yOffset = 11 + (122 - 17 * nbLines) / 2;
        }
        displayLine(lines[line + i], yOffset + (i * 17), CENTER_ALIGNMENT);
      }
      display.update();
      if(line + 6 < nbLines) {
        delay(DISPLAY_DELAY);
      }
    }
  }
}



//////////////////////////////////////////////////
/////              Reddit utils              /////
//////////////////////////////////////////////////
String getShowerthought()
{
  // Connect to reddit.com and fetch the showerthought data using the web client
  if(!client.connect(host, httpsPort)) {
    Serial.println("connection 1 failed!");
    return "Impossible to connect to Reddit";
  }
  String url = "/r/Showerthoughts/random.json?sort=hot&limit=1&t=week";
  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "User-Agent: ESP32/0.1\r\n" + 
               "Connection: close\r\n\r\n");
  unsigned long timeout = millis();
  while (client.available() == 0) {
      if (millis() - timeout > 5000) {
          Serial.println(">>> Client Timeout !");
          client.stop();
          return "Reddit did not respond in time";
      }
  }

  // get the random post url (redirection)
  String headerLine = "";
  String locationUrl;
  while(headerLine != "\r") {
    headerLine = client.readStringUntil('\n');
    if(headerLine.indexOf("location: ") ==0) {
      locationUrl = headerLine.substring(10, headerLine.indexOf("?"));
      break;
    }
  }
  client.stop();
  // get the content of the url we just got
  if(!client.connect(host, httpsPort)) {
    Serial.println("connection 2 failed!");
    return "Impossible to reconnect to Reddit";
  }
  client.print(String("GET ") + locationUrl + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "User-Agent: ESP32/0.1\r\n" + 
               "Connection: close\r\n\r\n");
  timeout = millis();
  while (client.available() == 0) {
      if (millis() - timeout > 5000) {
          Serial.println(">>> Client Timeout !");
          client.stop();
          return "Shower thought did not arrived in time";
      }
  }
  // get rid of header
  while(client.connected()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") {
      //Serial.println("headers received");
      break;
    }
  }
  // Get the response from the reddit.com server
  String quote = client.readStringUntil('\n');
  client.stop();
  // As the JSON-formatted response text can be too long, we'll parse it manually instead of using ArduinoJson
  int quote_start = quote.indexOf("\"title\"");
  // The showerthought quote ends right before a ', "' substring
  int quote_end = quote.indexOf(", \"", quote_start+1); // we start the search from the position where "title" is
  String showerthought = quote.substring(quote_start+9, quote_end);
  // Sanitize the string a bit
  showerthought.replace("\\\"","'"); // get rid of escaped quotes in the text ('\"')
  int junkIndex = showerthought.indexOf("\\u");
  while(junkIndex >= 0) {
    showerthought.remove(junkIndex, 5);
    showerthought.setCharAt(junkIndex, '\'');
    junkIndex = showerthought.indexOf("\\u");
  }
  return showerthought;
}



//////////////////////////////////////////////////
/////            Main functions              /////
//////////////////////////////////////////////////
void setup()
{
  Serial.begin(115200);
  delay(10);
  // configure wake up source for deep sleep
  esp_sleep_enable_ext1_wakeup(BUTTON_PIN_BITMASK, ESP_EXT1_WAKEUP_ANY_HIGH);
  

  WiFi.begin(_SSID, _PSK); // use values from WifiCredentials.h
  int nbTries = 0;
  while(WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if(nbTries % 4 == 3) {
      WiFi.begin(_SSID, _PSK);
    }
    if(nbTries >= 20) {
      displayText("Impossible to connect to " + WiFi.SSID() + ", going to sleep now");
      esp_deep_sleep_start();
    }
    nbTries++;
  }

  Serial.println();
  Serial.println("WiFi connected");
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  String showerthought = getShowerthought();
  Serial.println(showerthought);
  displayText(showerthought);
  
  //Go to sleep now
  Serial.println("Going to sleep now");
  esp_deep_sleep_start();
}



void loop()
{
  // never called
}
