#include <Arduino.h>
#include <Roomba.h>
#include <ESP8266WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ESP8266mDNS.h>
#include <PubSubClient.h>

// contains wifi and mqtt credentials
#include "secrets.h"

#define LED_OFF HIGH
#define LED_ON LOW
#define LED 2

// Time in ms
const unsigned long MAX_WIFI_TIMEOUT = 15 * 1000;
const unsigned long MAX_CLIENT_TIMEOUT = 120 * 1000;
const unsigned long TIME_BETWEEN_MQTT_UPDATE = 10 * 1000;

// Put to false when connected to roomba to not send bogus data
const bool PRINT_DEBUG = false;

template<typename T>
void printDebug(const T& val){
  if(PRINT_DEBUG){
    Serial.begin(115200);
    Serial.print(val);
  }
}

template<typename T>
void printlnDebug(const T& val){
  if(PRINT_DEBUG){
    Serial.begin(115200);
    Serial.println(val);
  }
}

// Roomba declaration and sensor variables
Roomba roomba(&Serial, Roomba::Baud115200);

uint8_t buffer2Bytes[2];
uint16_t battCharge = 0;
uint16_t battCappacity = 0;
float battPercentage = 0;
float battVoltage = 0;
uint16_t battVoltageMV = 0;
int16_t battCurrent = 0;
uint8_t chargingState = 0;

// NTP for time of the day - not really usefull now
const auto utcOffsetInSeconds = -4 * 60 * 60; // UTC -4
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "north-america.pool.ntp.org", utcOffsetInSeconds);

WiFiClient wifiClient;
PubSubClient client(wifiClient);


void toggle(uint8_t pin){
  digitalWrite(pin, !digitalRead(pin));
}

template<typename T>
void publishDebug(const T& message){
  client.publish("roomba/debug", String(message).c_str());
}

void setupOTA(){
  ArduinoOTA.setHostname("esp8266-roomba");
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([]() {
    printlnDebug("Starting OTA");
  });

  ArduinoOTA.onEnd([]() {
    printlnDebug("OTA finished");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    int percentage = (float) progress / (float) total * 100;

    if(!(percentage % 5)){
      printlnDebug(percentage);
    }
  });

  ArduinoOTA.onError([](ota_error_t error) {
    if(PRINT_DEBUG){
      Serial.begin(115200);
      Serial.printf("Error[%u]: ", error);
    }
    if (error == OTA_AUTH_ERROR) {
      printlnDebug("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      printlnDebug("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      printlnDebug("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      printlnDebug("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      printlnDebug("End Failed");
    }
  });

  ArduinoOTA.begin();
}

void restartIfWifiIsDiconnected(){
  // Restart the MCU if the wifi is disconnnected for too long
  static int lastWifiConnected = 0;
  if(!WiFi.isConnected()){
    printlnDebug("Wifi disconnected");
    while(!WiFi.isConnected()){
      if(millis() - lastWifiConnected > MAX_WIFI_TIMEOUT) {
        printlnDebug("Could not connect to wifi, restarting");
        ESP.restart();
      }
      delay(100);
    }
  }
  else {
    lastWifiConnected = millis();
  }
}

void restartIfClientDisconnected() {
  static int lastClientConnected = 0;
  if(!client.connected()){
    printlnDebug("Client disconnected");
    while(!client.connected()){
      if(millis() - lastClientConnected > MAX_CLIENT_TIMEOUT) {
        printlnDebug("MQTT could not connect to server, restarting");
        ESP.restart();
      }
      String clientId = "esp8266Roomba-";
      clientId += String(random(0xFFFF), HEX);
      client.connect(clientId.c_str(), MQTT_USER, MQTT_PASSWORD, "roomba/status", 0, 0, "disconnected");
      client.subscribe("roomba/commands");

      ArduinoOTA.handle();
      delay(100);
    }
  }
  else {
    lastClientConnected = millis();
  }
}

uint16_t buffToInt(const uint8_t* buff) {
  return (buff[0] << 8) | buff[1];
}

void updateBatteryCharge(){
  const uint16_t THRESHOLD_ERROR = 5000; // The biggest battery is about 4000 mAh
  roomba.start();
  delay(100);
  if(roomba.getSensors(roomba.SensorBatteryCapacity, buffer2Bytes, 2)){
    uint16_t oldVal = battCappacity;  // Fix for bug where the roomba return a super big value
    battCappacity = buffToInt(buffer2Bytes);
    if(battCappacity > THRESHOLD_ERROR) {
      String debugMessage = "Capacity : " + String(battCappacity); 
      publishDebug(debugMessage);
      battCappacity = oldVal;
    }
  }
  delay(100);
  if(roomba.getSensors(roomba.SensorBatteryCharge, buffer2Bytes, 2)){
    uint16_t oldVal = battCappacity; // Same fix
    battCharge = buffToInt(buffer2Bytes);
    if(battCharge > THRESHOLD_ERROR) {
      String debugMessage = "Charge : " + String(battCharge); 
      publishDebug(debugMessage);
      battCharge = oldVal;
    }
  }
  battPercentage = (float) battCharge / (float) battCappacity * 100;
  delay(100);
}

void updateChargingState() {
  const uint8_t MAX_CHARGE_STATE = 5;
  roomba.start();
  delay(100);
  if(roomba.getSensors(roomba.SensorChargingState, buffer2Bytes, 1)){
    uint8_t oldVal = chargingState;
    chargingState = buffer2Bytes[0];
    if(chargingState > MAX_CHARGE_STATE) {
      String debugMessage = "Charging state : " + String(chargingState); 
      publishDebug(debugMessage);
      chargingState = oldVal;
    }
  }
  delay(100);
}

void updateVoltageCurrent(){
  const float MAX_VOLTAGE = 25.0; // should never be greater than about 17V fully charged
  const float MAX_CURRENT = 6000; // Uses about 2A in regular use
  roomba.start();
  delay(100);
  if(roomba.getSensors(Roomba::SensorVoltage, buffer2Bytes, 2)){
    battVoltageMV = buffToInt(buffer2Bytes);
    float oldVal = battVoltage;
    battVoltage = (float) battVoltageMV / 1000.0f;
    if(battVoltage > MAX_VOLTAGE){
      String debugMessage = "Voltage : " + String(battVoltage); 
      publishDebug(debugMessage);
      battVoltage = oldVal;
    }
  }
  delay(100);
  if(roomba.getSensors(Roomba::SensorCurrent, buffer2Bytes, 2)){
    int16_t oldVal = battCurrent;
    battCurrent = buffToInt(buffer2Bytes);
    if(abs(battCurrent) > MAX_CURRENT) {
      String debugMessage = "Current : " + String(battCurrent); 
      publishDebug(debugMessage);
      battCurrent = oldVal;
    }
  }
  delay(100);
}

unsigned long countSongTimeMs(uint8_t* song, uint8_t numNotes){
  unsigned long songTimeMs = 0;
  for(int i = 1; i < numNotes; i += 2){
    songTimeMs = song[i] * 16; // 16 ms ~= 1/64s
  }
  return songTimeMs;
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
  roomba.song(1, a, sizeof(a));
  delay(100);
  roomba.song(2, b, sizeof(b));
  delay(100);


  roomba.fullMode();
  delay(100);
  // play the song
  roomba.playSong(1);
  //delay(countSongTimeMs(a, sizeof(a)));
  delay(4000);

  roomba.fullMode();
  delay(100);
  roomba.playSong(2);
  //delay(countSongTimeMs(b, sizeof(b)));
  delay(4000);

  // Load 2 more songs, was not working loading them at the same time as the other
  roomba.song(3, c, sizeof(c));
  delay(100);
  roomba.song(4, d, sizeof(d));
  delay(100);

  roomba.fullMode();
  delay(100);

  roomba.playSong(3);
  //delay(countSongTimeMs(c, sizeof(c)));
  delay(3300);

  roomba.fullMode();
  delay(100);
  roomba.playSong(4);
  //delay(countSongTimeMs(d, sizeof(d)));
  delay(4000);
  roomba.start();
}

void startCleaning(){
  roomba.start();
  delay(100);
  roomba.safeMode(); // Probably only needed for series 600
  delay(100);
  roomba.cover(); // Sends clean command
  client.publish("roomba/status", "cleaning");
  printlnDebug("Started cleaning");
  delay(100);
}

void goToDock(){
  roomba.start();
  delay(100);
  roomba.safeMode();
  delay(100);
  roomba.coverAndDock(); // Send command to seek dock
  client.publish("roomba/status", "dock");
  printlnDebug("Going to dock");
  delay(100);
}

void stop() {
  roomba.start();
  delay(100);
  roomba.power();
  delay(100);
  client.publish("roomba/status", "power");
  printlnDebug("Stopping roomba");
}

void callback(char* topic, byte* payload, unsigned int length) {
  printlnDebug("Received MQTT message");

  String topicStr = String(topic);
  String payloadStr = String();
  

  payloadStr.reserve(length);
  for(unsigned int i = 0; i < length; i++){
    payloadStr += (char) payload[i];
  }

  printlnDebug("Topic : " + topicStr);
  printlnDebug("Payload : " + payloadStr);

  // client.publish("roomba/debug", payloadStr.c_str());

  if(topicStr == "roomba/commands") {
    if(payloadStr == "start") {
      startCleaning();
    } 
    else if (payloadStr == "stop") { 
      goToDock();
    }
    else if(payloadStr == "power") {
      stop();
    }
    else if(payloadStr == "imperial") {
      playImperialMarch();
    }
    else if (payloadStr == "restart"){
      ESP.restart();
    }
  }
}

void sendMqttInfo(){
  client.publish("roomba/battery/percentage", String((int) battPercentage).c_str());
  client.publish("roomba/battery/capacity", String(battCappacity).c_str());
  client.publish("roomba/battery/charge", String(battCharge).c_str());
  client.publish("roomba/battery/voltage", String(battVoltage).c_str());
  client.publish("roomba/battery/current", String(battCurrent).c_str());
  client.publish("roomba/charge", String(chargingState).c_str());
  printlnDebug("Sent MQTT data");
}

void updateAllRoombaSensors(){
  updateBatteryCharge();
  updateChargingState();
  updateVoltageCurrent();
  printlnDebug("Updated sensors");
}

void setup() {
  printlnDebug("ESP started");
  pinMode(LED, OUTPUT);
  
  // Connect to wifi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  restartIfWifiIsDiconnected();

  printDebug("WiFi connected\nIP : ");
  printlnDebug(WiFi.localIP());

  setupOTA();

  // Setup MQTT client
  client.setServer(MQTT_HOST, 1883);
  client.setCallback(callback);
  restartIfClientDisconnected();
  printlnDebug("MQTT connected"); 
  client.publish("online", "roombaEsp8266"); // Send on boot that we are online, mostly for debugging
  delay(50);

  roomba.start();

  printlnDebug("End of setup");
}

void loop() {
  
  static unsigned long lastMqttUpdate = 0;

  restartIfWifiIsDiconnected();
  restartIfClientDisconnected();

  ArduinoOTA.handle();

  timeClient.update();

  client.loop();

  if(millis() - lastMqttUpdate > TIME_BETWEEN_MQTT_UPDATE) {
    /* TODO : add logic to keep the update delay the same, but 
     * update each sensor at different times to no block the main loop
     * for too long. */
    updateAllRoombaSensors();
    sendMqttInfo();
    lastMqttUpdate = millis();
  }
  
}
