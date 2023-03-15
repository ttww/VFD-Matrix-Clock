#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <U8g2lib.h>
#include <NTPClient.h>
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

//-----------------------------------------------------------------------------
// VFD-Display declarations:
//-----------------------------------------------------------------------------
U8G2_GP1287AI_256X50_1_4W_HW_SPI
u8g2(U8G2_R2, /* cs=*/PIN_VFD_CHIPSELECT,
     /* dc=*/PIN_VFD_CLOCK,
     /* reset=*/PIN_VFD_RESET /* U8X8_PIN_NONE , PIN_VFD_RESET */);

//-----------------------------------------------------------------------------
// NTP declarations:
//-----------------------------------------------------------------------------
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

//-----------------------------------------------------------------------------
// Clock declarations:
//-----------------------------------------------------------------------------
int ldr, brightness, sec;

//-----------------------------------------------------------------------------
// OTA / WIFI code:
//-----------------------------------------------------------------------------

static void setup_OTA_and_WIFI()
{
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
		.onProgress([](unsigned int progress, unsigned int total) { Serial.printf("Progress: %u%%\r", (progress / (total / 100))); })
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

void loop_OTA()
{
	ArduinoOTA.handle();
}

//-----------------------------------------------------------------------------
// OTA / WIFI code:
//-----------------------------------------------------------------------------

void setup_NTP()
{
  timeClient.begin();
}

void loop_NTP()
{
  timeClient.update();
}

//-----------------------------------------------------------------------------
// VFD display code:
//-----------------------------------------------------------------------------

void setup_VFD()
{
	pinMode(PIN_VFD_FILAMENT, OUTPUT);
	pinMode(PIN_VFD_RESET, OUTPUT);

	/*
    delay(1);
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

void drawHorizontalSegment(int x, int y, int w)
{
	u8g2.drawHLine(x, y, w);
	if (w > 5) {
		u8g2.drawHLine(x + 1, y - 1, w - 2);
		u8g2.drawHLine(x + 1, y + 1, w - 2);
	}
}

void drawVerticalSegment(int x, int y, int h)
{
	u8g2.drawVLine(x, y, h);
	if (h > 5) {
		u8g2.drawVLine(x - 1, y + 1, h - 2);
		u8g2.drawVLine(x + 1, y + 1, h - 2);
	}
}

void drawSegments(int x, int y, const char *digits, int dw, int dwv, int dh, int dhv)
{
	// u8g2.drawFrame(x,y, dw + 2 * dwv, 2 * dh + 2 * dhv);
	x += dwv + 1;
	y += 1;

	if (digits[0] != ' ') { // A segment
		drawHorizontalSegment(x, y, dw);
	}
	if (digits[1] != ' ') { // B segment
		drawVerticalSegment(x + dw + dwv - 1, y + dhv, dh);
	}
	if (digits[2] != ' ') { // C segment
		drawVerticalSegment(x + dw + dwv - 1, y + dh + dhv + dhv + dhv - 1, dh);
	}
	if (digits[3] != ' ') { // D segment
		drawHorizontalSegment(x, y + dh + dh + dhv + dhv + dhv + dhv - 2, dw);
	}
	if (digits[4] != ' ') { // E segment
		drawVerticalSegment(x - dwv, y + dh + dhv + dhv + dhv - 1, dh);
	}
	if (digits[5] != ' ') { // F segment
		drawVerticalSegment(x - dwv, y + dhv, dh);
	}
	if (digits[6] != ' ') { // G segment
		drawHorizontalSegment(x, y + dh + dhv + dhv - 1, dw);
	}
}

void drawDigit(int x, int y, int digit, int dw, int dwv, int dh, int dhv)
{
	const char *digits[] = {
		"...... ", // 0
		" ..    ", // 1
		".. .. .", // 2
		"....  .", // 3
		" ..  ..", // 4
		". .. ..", // 5
		". .....", // 6
		"...    ", // 7
		".......", // 8
		"...  ..", // 9
	};

	const char *segments;
	if (digit >= 0 && digit <= 9)
		segments = digits[digit];
	else
		segments = ".  .  .";

	drawSegments(x, y, segments, dw, dwv, dh, dhv);
}

Neotimer ldr_timer = Neotimer(150); // 75ms second timer

void adjust_vfd_brightness()
{
	if (ldr_timer.repeat()) {
		ldr = analogRead(PIN_VFD_LDR);
		int raw_brightness = map(ldr, 0, 1500, 40, 1);
		if (raw_brightness < 1)
			raw_brightness = 1;

		if (brightness > raw_brightness)
			brightness--;
		if (brightness < raw_brightness)
			brightness++;

		if (brightness != raw_brightness)
			u8g2.setContrast(brightness);
	}
}

void loop_VFD()
{
	adjust_vfd_brightness();
}

void loop_VFD_1sec()
{
	// u8g2.setFont(u8g2_font_ncenB14_tr);

	u8g2.firstPage();
	do {
		drawDigit(1, 1, sec % 11, 3, 0, 5, 0);
		drawDigit(10, 1, sec % 11, 3, 1, 5, 1);
		drawDigit(20, 1, sec % 11, 10, 2, 12, 2);
		drawDigit(40, 1, sec % 11, 10, 1, 12, 1);

		if (sec % 2 == 0) {
			u8g2.drawPixel(1, 1);
			u8g2.drawPixel(10, 1);
			u8g2.drawPixel(20, 1);
			u8g2.drawPixel(40, 1);
		}

		u8g2.setFont(u8g2_font_10x20_me);

		//u8g2.setCursor(60, 30);
		//u8g2.print("Abc 123 ÄÖÜ äöü");
	} while (u8g2.nextPage());

  //Serial.printf("Time= %s\n", timeClient.getFormattedTime());
}

//-----------------------------------------------------------------------------
// Arduino setup & loop code:
//-----------------------------------------------------------------------------

void setup()
{
	Serial.begin(115200);

	Serial.printf("\n\nRunning....\n");

	setup_OTA_and_WIFI();
	setup_NTP();
	setup_VFD();
}

Neotimer sec_timer = Neotimer(500);

void loop()
{
	loop_OTA();
	loop_NTP();
	loop_VFD();

	sec = millis() / 1000;

	if (sec_timer.repeat()) {
		Serial.printf("Alive...%ld   LDR = %d --> %d\n", sec, ldr, brightness);
		loop_VFD_1sec();
	}
}
