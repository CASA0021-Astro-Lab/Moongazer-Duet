#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include "DEV_Config.h"
#include "EPD.h"
#include "GUI_Paint.h"
#include <stdlib.h>
#include "time.h"
#include "imagedata.h"
#include "arduino_secrets.h"

#include <UniversalTelegramBot.h>
#include <WiFiClientSecure.h>

int botRequestDelay = 1000;
unsigned long lastTimeBotRan;

const char *CHAT_ID = SECRET_CHATID;
const char *BOTtoken = SECRET_BOTAPITOKEN;

const char *ssid = SECRET_SSID;
const char *password = SECRET_PASS;

const char *googleApiKey = SECRET_GOOGLEAPIKEY;
const char *moonPhaseApiKey = SECRET_MOONPHASEAPIKEY;

const int buttonPin = 34;
const int servoPin1 = 33;
const int servoPin3 = 32;
const int LEDPin = 16;

Servo servo1;
// Servo servo2;
Servo servo3;

bool lastButtonState = LOW;
bool servoAtInitialPosition = true;
bool isServoMoving = false;

UBYTE *BlackImage;

const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 0;
const int daylightOffset_sec = 3600;
int deviceAngle = 0;
int openAngle = 110;
int closeAngle = 0;

int normalAngle;

WiFiClientSecure client;
UniversalTelegramBot bot(BOTtoken, client);

int DeviceMode = 0;
unsigned long lastTimeServoRun;
unsigned long CurrentTime;
unsigned long LoopTime = 0;
int basicSpeed = 2;
int currentAngle = 0;

void setup()
{
  Serial.begin(115200);
  pinMode(LEDPin, OUTPUT);
  servo1.attach(servoPin1);
  servo3.attach(servoPin3);
  servo3.write(97);
  CoverOn();
  digitalWrite(LEDPin, HIGH);

  // 初始化墨水屏
  DEV_Module_Init();
  Serial.print("模块初始化成功");
  EPD_2in13_V4_Init();
  Serial.print("墨水屏初始化成功");
  EPD_2in13_V4_Clear();

  // 分配图像缓存
  UWORD Imagesize = ((EPD_2in13_V4_WIDTH % 8 == 0) ? (EPD_2in13_V4_WIDTH / 8) : (EPD_2in13_V4_WIDTH / 8 + 1)) * EPD_2in13_V4_HEIGHT;
  BlackImage = (UBYTE *)malloc(Imagesize);
  Paint_NewImage(BlackImage, EPD_2in13_V4_WIDTH, EPD_2in13_V4_HEIGHT, 270, WHITE);
  Paint_SelectImage(BlackImage);
  Paint_Clear(WHITE);

  WiFi.begin(ssid, password);

  int line = 0;
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1000);
    Serial.println("Connecting to Wifi...");
    Paint_DrawString_EN(5, 5, "Connecting to WiFi...", &Font16, WHITE, BLACK);
    Paint_Clear(WHITE);
    EPD_2in13_V4_Display(BlackImage);
  }
  Serial.println("WiFi connected");
  Paint_Clear(WHITE);
  Paint_DrawString_EN(5, 5, "WiFi Connected", &Font16, WHITE, BLACK);
  Paint_DrawString_EN(5, 35, "Getting data from API...", &Font16, WHITE, BLACK);
  EPD_2in13_V4_Display(BlackImage);

  DynamicJsonDocument json(1024);
  json["considerIp"] = false;

  int n = WiFi.scanNetworks();
  JsonArray wifiAccessPoints = json.createNestedArray("wifiAccessPoints");

  for (int i = 0; i < n; i++)
  {
    JsonObject ap = wifiAccessPoints.createNestedObject();
    ap["macAddress"] = WiFi.BSSIDstr(i);
    ap["signalStrength"] = WiFi.RSSI(i);
    ap["channel"] = WiFi.channel(i);
  }

  String requestBody;
  serializeJson(json, requestBody);

  HTTPClient http;
  http.begin("https://www.googleapis.com/geolocation/v1/geolocate?key=" + String(googleApiKey));
  http.addHeader("Content-Type", "application/json");
  int httpResponseCode = http.POST(requestBody);

  String lat, lon;
  if (httpResponseCode > 0)
  {
    String response = http.getString();
    Serial.println("Google Geolocation API响应: ");
    Serial.println(response);

    DynamicJsonDocument doc(1024);
    deserializeJson(doc, response);
    lat = String((float)doc["location"]["lat"], 6);
    lon = String((float)doc["location"]["lng"], 6);
  }
  else
  {
    Serial.print("Fail, the error is: ");
    Serial.println(httpResponseCode);
  }
  http.end();

  if (lat.length() > 0 && lon.length() > 0)
  {
    http.begin("https://moon-phase.p.rapidapi.com/advanced?lat=" + lat + "&lon=" + lon);
    http.addHeader("X-RapidAPI-Key", moonPhaseApiKey);
    http.addHeader("X-RapidAPI-Host", "moon-phase.p.rapidapi.com");

    httpResponseCode = http.GET();
    if (httpResponseCode > 0)
    {
      String response = http.getString();
      Serial.println("Moon Phase API: ");
      Serial.println(response);
    }
    else
    {
      Serial.print("Moon phase API fail: ");
      Serial.println(httpResponseCode);
    }
    http.end();
  }

  //* TelegramBot Setup *//
  client.setCACert(TELEGRAM_CERTIFICATE_ROOT);
  //* TelegramBot Setup *//
  NormalMode();
}

void loop()
{

  if (millis() - LoopTime > 12000)
  {
    if (DeviceMode == 0)
    {
      NormalMode();
    }
    else if (DeviceMode == 1)
    {
      TestMode(basicSpeed);
    }
    else if (DeviceMode == 2)
    {
      CaliMode();
    }
    LoopTime = millis();
    Serial.println(DeviceMode);
  }
  TelegramBotLoop();
}

void TelegramBotLoop()
{
  if (millis() > lastTimeBotRan + botRequestDelay)
  {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    while (numNewMessages)
    {
      Serial.println("got response");
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }
    lastTimeBotRan = millis();
  }
}

// happens when bot recieves new message
void handleNewMessages(int numNewMessages)
{
  Serial.println("handleNewMessages");
  Serial.println(String(numNewMessages));

  for (int i = 0; i < numNewMessages; i++)
  {
    // Chat id of the requester
    String chat_id = String(bot.messages[i].chat_id);
    // Serial.println(chat_id);
    if (chat_id != CHAT_ID)
    {
      bot.sendMessage(chat_id, "Unauthorized user", "");
      continue;
    }

    // Print the received message
    String text = bot.messages[i].text;
    Serial.println(text);

    String from_name = bot.messages[i].from_name;
    LoopTime = 0;
    if (text == "/start")
    {
      String welcome = "Welcome, " + from_name + ".\n\n";
      welcome += "/moonnormalmode : \nUse with mode number to change moon-rotation mode.\n(0 means normal, 1 means test, 2 means calibration)\n\n";
      welcome += "/moonrotatemode : \nUse with the speed (-20 to 20) to change to rotating mode.\n(like /earthrotatemode 5)\n\n";
      welcome += "/mooncali : \nUse with the degree (like /mooncali 15) to calibrate moon phase angle.\n\n";
      welcome += "/mooncoveropen : \nOpen the Cover.\n\n";
      welcome += "/mooncoverclose : \nClose the Cover.\n\n";
      welcome += "/earthnormalmode : \nChange to normal mode.\n\n";
      welcome += "/earthrotatemode : \nUse with the speed (-20 to 20) to change to rotating mode.\n(like /earthrotatemode 5)\n\n";
      welcome += "/earthcali : \nUse with the degree (like /earthcali 15) to calibrate earth phase angle.\n\n";
      welcome += "If there is no response, please make sure your device is on and connected to WiFi.";
      bot.sendMessage(chat_id, welcome, "");
    }
    // Mode 0
    if (text == "/moonnormalmode")
    {
      DeviceMode = 0;
      bot.sendMessage(chat_id, "Mode has changed to Normal.", "");
    }
    // Mode 1
    if (text.startsWith("/moonrotatemode"))
    {
      int spaceIndex = text.indexOf(' ');
      if (spaceIndex != -1)
      {
        String numberString = text.substring(spaceIndex + 1);
        int number = numberString.toInt();
        DeviceMode = 1;
        basicSpeed = number;
        bot.sendMessage(chat_id, "Mode has changed to Rotating.", "");
      }
      else
      {
        DeviceMode = 1;
        basicSpeed = 10;
        bot.sendMessage(chat_id, "Mode has changed to Rotating.", "");
      }
    }
    // Mode 2
    if (text.startsWith("/mooncali"))
    {
      if (DeviceMode != 2)
      {
        DeviceMode = 2;
        servo3.write(97);
        bot.sendMessage(chat_id, "Please check the rotate angle and resent.", "");
      }
      else
      {
        int spaceIndex = text.indexOf(' ');
        if (spaceIndex != -1)
        {
          String numberString = text.substring(spaceIndex + 1);
          int number = numberString.toInt();
          RotateAngle(number);
          bot.sendMessage(chat_id, "Calibration applied. If finished, remember to change to other modes.", "");
        }
      }
    }
    if (text == "/mooncoveropen")
    {
      CoverOn();
      bot.sendMessage(chat_id, "Cover opened.", "");
    }
    if (text == "/mooncoverclose")
    {
      CoverOff();
      bot.sendMessage(chat_id, "Cover closed.", "");
    }
    // test mode
    if (text.startsWith("/covertest"))
    {
      int spaceIndex = text.indexOf(' ');
      if (spaceIndex != -1)
      {
        String numberString = text.substring(spaceIndex + 1);
        int number = numberString.toInt();
        servo1.write(number);
        // servo2.write(-number);
        currentAngle = number;
        bot.sendMessage(chat_id, "change angle", "");
      }
    }
    if (text.startsWith("/servo1test"))
    {
      int spaceIndex = text.indexOf(' ');
      if (spaceIndex != -1)
      {
        String numberString = text.substring(spaceIndex + 1);
        int number = numberString.toInt();
        servo1.write(number);
        currentAngle = number;
        bot.sendMessage(chat_id, "change angle", "");
      }
    }
    if (text.startsWith("/servo2test"))
    {
      int spaceIndex = text.indexOf(' ');
      if (spaceIndex != -1)
      {
        String numberString = text.substring(spaceIndex + 1);
        int number = numberString.toInt();
        // servo2.write(number);
        currentAngle = number;
        bot.sendMessage(chat_id, "change angle", "");
      }
    }
  }
}

void RotateAngle(int Angle)
{
  if (Angle >= 0)
  {
    float RotateTime = 7.8 * Angle - 27; // !!Need to modify 675
    servo3.write(102);
    Serial.println(Angle);
    delay(RotateTime);
    servo3.write(97);
  }
  else
  {
    float RotateTime = -7.8 * Angle - 27;
    servo3.write(92);
    delay(RotateTime);
    servo3.write(97);
  }
}

void NormalMode()
{
  printf("Paint_NewImage\r\n");
  Paint_NewImage(BlackImage, EPD_2in13_V4_WIDTH, EPD_2in13_V4_HEIGHT, ROTATE_90, WHITE);
  EPD_2in13_V4_Init_Fast();
  Paint_SelectImage(BlackImage);
  Paint_Clear(WHITE);
  Paint_DrawBitMap(gImage_main);
  EPD_2in13_V4_Display_Fast(BlackImage);
  delay(3000);

  printf("Paint_NewImage\r\n");
  Paint_NewImage(BlackImage, EPD_2in13_V4_WIDTH, EPD_2in13_V4_HEIGHT, ROTATE_90, WHITE);
  EPD_2in13_V4_Init_Fast();
  Paint_SelectImage(BlackImage);
  Paint_Clear(WHITE);
  Paint_DrawBitMap(gImage_full);
  EPD_2in13_V4_Display_Fast(BlackImage);
  delay(3000);

  printf("Paint_NewImage\r\n");
  Paint_NewImage(BlackImage, EPD_2in13_V4_WIDTH, EPD_2in13_V4_HEIGHT, ROTATE_90, WHITE);
  EPD_2in13_V4_Init_Fast();
  Paint_SelectImage(BlackImage);
  Paint_Clear(WHITE);
  Paint_DrawBitMap(gImage_Apollo);
  EPD_2in13_V4_Display_Fast(BlackImage);
  delay(3000);

  printf("Paint_NewImage\r\n");
  Paint_NewImage(BlackImage, EPD_2in13_V4_WIDTH, EPD_2in13_V4_HEIGHT, ROTATE_90, WHITE);
  EPD_2in13_V4_Init_Fast();
  Paint_SelectImage(BlackImage);
  Paint_Clear(WHITE);
  Paint_DrawBitMap(gImage_step);
  EPD_2in13_V4_Display_Fast(BlackImage);
  delay(3000);

  printf("Paint_NewImage\r\n");
  Paint_NewImage(BlackImage, EPD_2in13_V4_WIDTH, EPD_2in13_V4_HEIGHT, ROTATE_90, WHITE);
  EPD_2in13_V4_Init_Fast();
  Paint_SelectImage(BlackImage);
  Paint_Clear(WHITE);
  Paint_DrawBitMap(gImage_314);
  EPD_2in13_V4_Display_Fast(BlackImage);
  delay(3000);

  printf("Paint_NewImage\r\n");
  Paint_NewImage(BlackImage, EPD_2in13_V4_WIDTH, EPD_2in13_V4_HEIGHT, ROTATE_90, WHITE);
  EPD_2in13_V4_Init_Fast();
  Paint_SelectImage(BlackImage);
  Paint_Clear(WHITE);
  Paint_DrawBitMap(gImage_315);
  EPD_2in13_V4_Display_Fast(BlackImage);
  delay(3000);

  CurrentTime = millis();
  if ((CurrentTime - lastTimeServoRun) >= 50000000)
  {
    lastTimeServoRun = millis();
    RotateAngle(12);
  }
}

void TestMode(int speed)
{
  Serial.println("Enter Test Mode.");
  Paint_Clear(WHITE);
  int SpeedAdd = speed + 97;
  servo3.write(SpeedAdd);
}

void CaliMode()
{
  Serial.println("Enter Calibration Mode.");
  Paint_Clear(WHITE);
  delay(3000);
}

void CoverOn()
{
  digitalWrite(LEDPin, HIGH);
  for (int pos = currentAngle; pos <= openAngle; pos++)
  { // roate from 0 to 180
    servo1.write(pos);
    // servo2.write(180-pos);
    delay(15);
    currentAngle = pos;
  }
}

void CoverOff()
{
  for (int pos = currentAngle; pos >= closeAngle; pos--)
  { // rotate from 180 to 0
    servo1.write(pos);
    // servo2.write(180-pos);
    delay(15);
    currentAngle = pos;
  }
  digitalWrite(LEDPin, LOW);

  servo3.write(97);
  DeviceMode = 3;
  delay(100);
}
