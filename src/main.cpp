#include <Arduino.h>
#include <Roomba.h>
#include <ESP8266WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoOTA.h>
#include <ESP8266mDNS.h>
// #include <PubSubClient.h>

#define LED_OFF HIGH
#define LED_ON LOW
#define LED 2

// contains wifi and mqtt credentials
#include "secrets.h"

const int MAX_WIFI_TIMEOUT = 60 * 1000; // 60 secs

// Roomba declaration and sensor variables
Roomba roomba(&Serial, Roomba::Baud115200);
uint16_t battCharge = 0;
uint16_t battCappacity = 0;
float battPercentage = 0;

// NTP for time of the day
const auto utcOffsetInSeconds = -4 * 60 * 60; // UTC -4
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "north-america.pool.ntp.org", utcOffsetInSeconds);

void toggle(uint8_t pin){
  digitalWrite(pin, !digitalRead(pin));
}

void setupOTA(){
  ArduinoOTA.setHostname("esp8266-roomba");
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([]() {
    // TODO : Play update sound
  });

  ArduinoOTA.onEnd([]() {
    // TODO : play update ended sound
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    // Flash the LED 
    toggle(LED);
  });

  ArduinoOTA.onError([](ota_error_t error) {
    // TODO : play error sound

    /* Code is not used because the serial interface is connected directly to the roomba
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTaH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
    */
  });

  ArduinoOTA.begin();
}

void restartIfWifiIsDiconnected(){
  // Restart the MCU if the wifi is disconnnected for too long
  static int lastWifiConnected = 0;
  if(!WiFi.isConnected()){
    while(!WiFi.isConnected()){
      if(millis() - lastWifiConnected > MAX_WIFI_TIMEOUT) {
        ESP.restart();
      }
      toggle(LED);
      delay(50);
    }
  }
  else {
    lastWifiConnected = millis();
  }
}

void setup() {
  pinMode(LED, OUTPUT);
  
  // Connect to wifi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  restartIfWifiIsDiconnected();

  roomba.start();
  delay(500);

  // Setup OTA
  setupOTA();
}

void updateBatteryCharge(){
  bool updated = roomba.getSensors(roomba.SensorBatteryCapacity, (uint8_t*) &battCappacity, 2);
  updated = roomba.getSensors(roomba.SensorBatteryCharge, (uint8_t*) &battCharge, 2);
  if(updated) {
    battPercentage = (float) battCharge / (float) battCappacity * 100;
    digitalWrite(LED, LED_ON);
  } else {
    digitalWrite(LED, LED_OFF);
  }
}

void playImperialMarch(){
  // Notes taken from https://github.com/t0ph/ArduinoRoombaControl/blob/master/RoombaImperialMarch.ino
  uint8_t a[] = { 55, 32, 55, 32, 55, 32, 51, 24, 58, 8, 55, 32, 51, 24, 58, 8, 55, 64 };
  uint8_t b[] = { 62, 32, 62, 32, 62, 32, 63, 24, 58, 8, 54, 32, 51, 24, 58, 8, 55, 64 };
  uint8_t c[] = { 3, 12, 67, 32, 55, 24, 55, 8, 67, 32, 66, 24, 65, 8, 64, 8, 63, 8, 64, 16, 30, 16, 56, 16, 61, 32 };
  uint8_t d[] = { 4, 14, 60, 24, 59, 8, 58, 8, 57, 8, 58, 16, 10, 16, 52, 16, 54, 32, 51, 24, 58, 8, 55, 32, 51, 24, 58, 8, 55, 64 };
  
  roomba.start();
  delay(50);  
  // Load the song parts
  roomba.song(1, a, sizeof(a)/2);
  delay(50);
  roomba.song(2, b, sizeof(b)/2);
  delay(50);
  roomba.song(3, c, sizeof(c)/2);
  delay(50);
  roomba.song(4, d, sizeof(d)/2);
  delay(50);

  roomba.safeMode();
  delay(50);
  // play the song
  roomba.playSong(1);
  delay(4000);
  roomba.playSong(2);
  delay(4000);
  roomba.playSong(3);
  delay(3500);
  roomba.playSong(4);
  delay(4000);
}

void startCleaning(){
  roomba.start();
  delay(50);
  // roomba.safeMode(); // Probably only needed for series 600
  // delay(50);
  roomba.cover(); // Sends clean command
}

void stopCleaning(){
  roomba.start();
  delay(50);
  // roomba.safeMode();
  // delay(50);
  roomba.coverAndDock(); // Send command to seek dock
}

void loop() {
  static int numloops = 0;

  restartIfWifiIsDiconnected();

  ArduinoOTA.handle();

  timeClient.update();

  HTTPClient http;
  WiFiClient wifiClient;

  updateBatteryCharge();
  static auto lastWifiUpdate = millis();
  if(millis() - lastWifiUpdate > 10000) {
    String host = "192.168.190.138";
    int port = 8000;
    http.begin(wifiClient, host, port, "Loop : " + String(numloops), false);
    http.GET();
    http.end();

    http.begin(wifiClient, host, port, String(battPercentage) + "%", false);
    http.GET();
    http.end();

    http.begin(wifiClient, host, port, String(battCharge) + "mAh", false);
    http.GET();
    http.end();

    http.begin(wifiClient, host, port, String(battCappacity) + "mAh", false);
    http.GET();
    http.end();

    lastWifiUpdate = millis();
    numloops++;
  }
  
}
