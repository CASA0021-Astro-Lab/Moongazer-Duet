#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ezTime.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <Adafruit_NeoPixel.h>
#include <Servo.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

Servo myservo;
int botRequestDelay = 1000;
unsigned long lastTimeBotRan;
int DeviceMode = 1;
unsigned long lastTimeServoRun;
unsigned long CurrentTime;
unsigned long LoopTime = 0;
int basicSpeed = 7;
int currentAngle = 0;



// Wifi and TelegramBot
#include "arduino_secrets.h"
/*
**** please enter your sensitive data in the Secret tab/arduino_secrets.h
**** using format below

#define SECRET_SSID "ssid name"
#define SECRET_PASS "ssid password"
#define SECRET_CHATID "botID";
#define SECRET_BOTAPITOKEN "botAPItoken";
 */

const char* ssid     = SECRET_SSID;
const char* password = SECRET_PASS;
const char* CHAT_ID  = SECRET_CHATID;
const char* BOTtoken = SECRET_BOTAPITOKEN;
const char* googleApiKey = SECRET_GOOGLEAPIKEY;
const char* moonPhaseApiKey = SECRET_MOONPHASEAPIKEY;

ESP8266WebServer server(80);
WiFiClient espClient;

// Date and time
Timezone GB;

// Telegram Bot Part
X509List cert(TELEGRAM_CERTIFICATE_ROOT);
WiFiClientSecure BOTClient;
UniversalTelegramBot bot(BOTtoken, BOTClient);

void setup() {
  // open serial connection for debug info
  Serial.begin(115200);
  delay(100);

  myservo.attach(12);
  
  //run initialisation functions
  startWifi();
  syncDate();

  // setup telegram bot
  configTime(0, 0, "pool.ntp.org");      // get UTC time via NTP
  BOTClient.setTrustAnchors(&cert); // Add root certificate for api.telegram.org

  GetDataFromAPI();
}

void loop() {

  if(millis() - LoopTime > 3000){  
    if(DeviceMode == 0){
      NormalMode();
    }else if(DeviceMode == 1){
      TestMode(basicSpeed);
    }
    LoopTime = millis();
    Serial.println(DeviceMode);
  }
  delay(1000);
  // telegram bot part
  if (millis() > lastTimeBotRan + botRequestDelay)  {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    while(numNewMessages) {
      Serial.println("got response");
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }
    lastTimeBotRan = millis();
  }
}

void syncDate() {
  // get real date and time
  waitForSync();
  Serial.println("UTC: " + UTC.dateTime());
  GB.setLocation("Europe/London");
  Serial.println("London time: " + GB.dateTime());
}

// happens when bot recieves new message
void handleNewMessages(int numNewMessages) {
  Serial.println("handleNewMessages");
  Serial.println(String(numNewMessages));

  for (int i=0; i<numNewMessages; i++) {
    // Chat id of the requester
    String chat_id = String(bot.messages[i].chat_id);
    //Serial.println(chat_id);
    if (chat_id != CHAT_ID){
      bot.sendMessage(chat_id, "Unauthorized user", "");
      continue;
    }
    
    // Print the received message
    String text = bot.messages[i].text;
    Serial.println(text);

    String from_name = bot.messages[i].from_name;

    // if (text == "/start"){
    //   String welcome = "Welcome, " + from_name + ".\n\n";
    //   welcome += "/moonmode : \nuse with mode number to change moon-rotation mode.\n(0 means normal, 1 means test, 2 means calibration)\n\n";
    //   welcome += "/mooncali : \nuse with the degree (like /mooncali 15) to calibrate moon phase angle.\n\n";
    //   welcome += "/earthnormalmode : \nchange to normal mode.\n\n";
    //   welcome += "/earthrotatemode : \nwith the speed (-20 to 20) to change to rotating mode.\n(like /earthrotatemode 5)\n\n";
    //   welcome += "/earthcali : \nuse with the degree (like /earthcali 15) to calibrate earth phase angle.\n\n";
    //   welcome += "If there is no response, please make sure your device is on and connected to WiFi.";
    //   bot.sendMessage(chat_id, welcome, "");
    // }
    // Mode 0
    if (text == "/earthnormalmode"){
      DeviceMode = 0;
      bot.sendMessage(chat_id, "Mode has changed to Normal.", "");
    }
    // Mode 1
    if (text.startsWith("/earthrotatemode")) {
      int spaceIndex = text.indexOf(' '); 
      if (spaceIndex != -1) { 
        String numberString = text.substring(spaceIndex + 1); 
        int number = numberString.toInt(); 
        DeviceMode = 1;
        basicSpeed = number;
        bot.sendMessage(chat_id, "Mode has changed to Rotating.", "");
      } else{
        DeviceMode = 1;
        basicSpeed = 10;
        bot.sendMessage(chat_id, "Mode has changed to Rotating.", "");
      }
    }
    // Mode 2
    if (text.startsWith("/earthcali")) {
      if(DeviceMode != 2){
          DeviceMode = 2;
          myservo.write(90);
          bot.sendMessage(chat_id, "Please check the rotate angle and resent.", "");
      }else{
        int spaceIndex = text.indexOf(' '); 
        if (spaceIndex != -1) { 
          String numberString = text.substring(spaceIndex + 1); 
          int number = numberString.toInt(); 
          RotateAngle(myservo,number);
          bot.sendMessage(chat_id, "Calibration applied. If finished, remember to change to other modes.", "");
        }
      }
    }
  }
}

void RotateAngle(Servo servo, int Angle){
  if(Angle >= 0){
    float RotateTime = 7.8*Angle-27; // !!Need to modify 675
    servo.write(95);
    delay(RotateTime);
    servo.write(90);
  }else{
    float RotateTime = -7.8*Angle-27;
    servo.write(85);
    delay(RotateTime);
    servo.write(90);
  }
}

void ServoControl(Servo servo, int modeNumber, int servoSpeed){
  if(modeNumber == 0){
    servo.write(90);
  }else if(modeNumber == 1){
    servo.write(90+servoSpeed);
  }else if(modeNumber == -1){
    servo.write(90-servoSpeed);
  }
}

void startWifi() {
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  // check to see if connected and wait until you are
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.println(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void NormalMode(){

  //*这边要加API*//
  CurrentTime = millis();
  if ((CurrentTime - lastTimeServoRun)>= 1000){
    lastTimeServoRun = millis();
    RotateAngle(myservo,12);
  }
}

void TestMode(int speed){
  int SpeedAdd = speed + 90;
  myservo.write(SpeedAdd);
}

void GetDataFromAPI(){
    // 第一步: 获取经纬度
  DynamicJsonDocument json(1024);
  json["considerIp"] = false;

  int n = WiFi.scanNetworks();
  JsonArray wifiAccessPoints = json.createNestedArray("wifiAccessPoints");

  for (int i = 0; i < n; i++) {
    JsonObject ap = wifiAccessPoints.createNestedObject();
    ap["macAddress"] = WiFi.BSSIDstr(i);
    ap["signalStrength"] = WiFi.RSSI(i);
    ap["channel"] = WiFi.channel(i);
  }

  String requestBody;
  serializeJson(json, requestBody);

  HTTPClient http;
  http.begin(espClient,"https://www.googleapis.com/geolocation/v1/geolocate?key=" + String(googleApiKey));
  http.addHeader("Content-Type", "application/json");
  int httpResponseCode = http.POST(requestBody);

  String lat, lon;
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("Google Geolocation API响应: ");
    Serial.println(response);

    DynamicJsonDocument doc(1024);
    deserializeJson(doc, response);
    lat = String((float)doc["location"]["lat"], 6);
    lon = String((float)doc["location"]["lng"], 6);
  } else {
    Serial.print("请求失败，错误码: ");
    Serial.println(httpResponseCode);
  }
  http.end();

  // 第二步: 使用经纬度获取月相信息
  if (lat.length() > 0 && lon.length() > 0) {
    http.begin(espClient,"https://moon-phase.p.rapidapi.com/advanced?lat=" + lat + "&lon=" + lon);
    http.addHeader("X-RapidAPI-Key", moonPhaseApiKey);
    http.addHeader("X-RapidAPI-Host", "moon-phase.p.rapidapi.com");

    httpResponseCode = http.GET();
    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.println("Moon Phase API响应: ");
      Serial.println(response);
    } else {
      Serial.print("月相API请求失败，错误码: ");
      Serial.println(httpResponseCode);
    }
    http.end();
  }
}
