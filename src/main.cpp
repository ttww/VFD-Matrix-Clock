#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <neotimer.h>

#ifdef U8X8_HAVE_HW_SPI
#include <SPI.h>
#endif
#ifdef U8X8_HAVE_HW_I2C
#include <Wire.h>
#endif

#include "arduino_secrets.h"

const char *ssid = SECRET_SSID;
const char *password = SECRET_PASSWD;

/*

         ESP           DISPLAY
Orange    IO33          FILAMENT_EN     #1     Out, high active
Gelb      IO36 ADC2     LII_SW          #11    In, LDR to low
Grün      IO26          RESET           #5     Out. low active
Blau      IO18 SCK      CLOCK           #2
-
Lila      IO23 MOSI     DATA            #4
Grau.     IO05 CS       CHIPSELECT      #3
 */

const byte PIN_VFD_FILAMENT = 33;
const byte PIN_VFD_LDR = 36;
const byte PIN_VFD_RESET = 26;
const byte PIN_VFD_CLOCK = 2;
const byte PIN_VFD_DATA = 32;
const byte PIN_VFD_CHIPSELECT = 5;

U8G2_GP1287AI_256X50_1_4W_HW_SPI
u8g2(U8G2_R2, /* cs=*/PIN_VFD_CHIPSELECT,
     /* dc=*/PIN_VFD_CLOCK,
     /* reset=*/PIN_VFD_RESET /* U8X8_PIN_NONE , PIN_VFD_RESET */);

void setup_OTA_and_WIFI() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  // Port defaults to 3232
  // ArduinoOTA.setPort(3232);

  // Hostname defaults to esp3232-[MAC]
  ArduinoOTA.setHostname("matrix-vfd");

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA
      .onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
          type = "sketch";
        else // U_SPIFFS
          type = "filesystem";

        // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS
        // using SPIFFS.end()
        Serial.println("Start updating " + type);
      })
      .onEnd([]() { Serial.println("\nEnd"); })
      .onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
      })
      .onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR)
          Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR)
          Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR)
          Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR)
          Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR)
          Serial.println("End Failed");
      });

  ArduinoOTA.begin();

  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void loop_OTA() { ArduinoOTA.handle(); }

void setup_VFD() {
  pinMode(PIN_VFD_FILAMENT, OUTPUT);
  pinMode(PIN_VFD_RESET, OUTPUT);

  /*delay(1);
  digitalWrite(PIN_VFD_RESET, LOW);
  delay(1);
  digitalWrite(PIN_VFD_RESET, HIGH);
  delay(1);
*/
  digitalWrite(PIN_VFD_FILAMENT, HIGH);

  digitalWrite(PIN_VFD_LDR, HIGH);
  pinMode(PIN_VFD_LDR, INPUT_PULLUP);

  u8g2.begin();
  u8g2.enableUTF8Print(); // enable UTF8 support for the Arduino print()
  // function
}

int x;
int y = 20;

void loop_VFD() {

  // u8g2.setFont(u8g2_font_ncenB14_tr);
  u8g2.setFont(u8g2_font_unifont_t_weather);

  u8g2.firstPage();
  do {
    // for (int i=0; i<50; i++) {
    //    u8g2.drawLine(0, i, 255, i);
    // }
    // u8g2.drawPixel(0, 0);
    // u8g2.drawPixel(255, 0);
    // u8g2.drawPixel(0, 49);
    // u8g2.drawPixel(255, 49);

    u8g2.drawLine(0, 0, x, y);
    u8g2.drawLine(255, 0, x, y);
    u8g2.drawLine(0, 49, x, y);
    u8g2.drawLine(255, 49, x, y);
    x++;
    if (x >= 256) {
      x = 0;
      y++;
      if (y >= 50)
        y = 0;
    }
    u8g2.setCursor(x, y);
    u8g2.print("x ☃  Ä Ö Ü ä ö ü ß € x");
  } while (u8g2.nextPage());
}

void setup() {

  Serial.begin(115200);

  Serial.printf("\n\nRunning....\n");

  setup_OTA_and_WIFI();

  setup_VFD();
}

int ldr, brightness;


Neotimer ldr_timer = Neotimer(150);  // 75ms second timer
Neotimer sec_timer = Neotimer(1000); // 1s second timer

void loop() {
  loop_OTA();

  if (ldr_timer.repeat()) {

    ldr = analogRead(PIN_VFD_LDR);
    int raw_brightness = map(ldr, 0, 1500, 130, 1);
    if (raw_brightness < 1)
      raw_brightness = 1;


    if (brightness > raw_brightness)
      brightness--;
    if (brightness < raw_brightness)
      brightness++;
    
    if (brightness != raw_brightness) u8g2.setContrast(brightness);
  }

  int sec = millis() / 1000;
  if (sec_timer.repeat()) {
    Serial.printf("Alive...%ld   LDR = %d --> %d\n", sec, ldr, brightness);
    loop_VFD();
  }

}
