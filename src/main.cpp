#include <Arduino.h>
#include <Roomba.h>
#include <ESP8266WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ESP8266HTTPClient.h>

#define LED_OFF HIGH
#define LED_ON LOW
#define LED 2

// Credentials for the network
const String SSID = "***REMOVED***";
const String PSK  = "***REMOVED***";

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

void playImperialMarch(){
  roomba.fullMode();
  uint8_t impNotes [] = { 31, 64 };
  roomba.song(1, impNotes, sizeof(impNotes)/sizeof(impNotes[0]));
  delay(100);
  roomba.playSong(1);
  delay(4000);
  roomba.start();
}


void setup() {
  pinMode(LED, OUTPUT);
  
  // Connect to wifi
  WiFi.mode(WIFI_STA); // Client only, no ap
  WiFi.begin(SSID, PSK);

  // play a song on the roomba
  roomba.start();
  delay(500);
  playImperialMarch();
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

void loop() {
  static int numloops = 0;
  restartIfWifiIsDiconnected();
  timeClient.update();
  HTTPClient http;
  WiFiClient wifiClient;

  updateBatteryCharge();
  String host = "192.168.190.103";
  int port = 8000;
  http.begin(wifiClient, host, port, "Loop : " + 
  
  String(numloops), false);
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
  delay(10000);
  numloops++;
}