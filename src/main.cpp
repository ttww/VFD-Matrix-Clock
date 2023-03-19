#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <U8g2lib.h>
#include <NTP.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <neotimer.h>
#include <MQTT.h>

#ifdef U8X8_HAVE_HW_SPI
#include <SPI.h>
#endif
#ifdef U8X8_HAVE_HW_I2C
#include <Wire.h>
#endif

#include "arduino_secrets.h"

const char *ssid = SECRET_SSID;
const char *password = SECRET_PASSWD;
const char *mqtt_host = MQTT_HOST;

const char *hostname = "matrix-vfd";

String mqtt_topic = "clock/matrix-vfd";
String mqtt_log_topic = "log/matrix-vfd/debug";

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
// MQTT declarations:
//-----------------------------------------------------------------------------
WiFiClient net;
MQTTClient mqtt;

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
NTP ntp(ntpUDP);

//-----------------------------------------------------------------------------
// Clock declarations:
//-----------------------------------------------------------------------------
int ldr, brightness, sec;

//-----------------------------------------------------------------------------
// Utils code:
//-----------------------------------------------------------------------------

void mqtt_log(const char *message);
void mqtt_log(String message);

void log(const char *format, ...)
{
	va_list arg;
	va_start(arg, format);

	char buf[100];
	vsnprintf(buf, sizeof(buf) - 1, format, arg);
	buf[sizeof(buf) - 1] = '\0';
	va_end(arg);

	mqtt_log(buf);
}

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
	ArduinoOTA.setHostname(hostname);

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
	ntp.ruleDST("CEST", Last, Sun, Mar, 2, 120); // last sunday in march 2:00, timetone +120min (+1 GMT + 1h summertime offset)
	ntp.ruleSTD("CET", Last, Sun, Oct, 3, 60); // last sunday in october 3:00, timezone +60min (+1 GMT)
	ntp.begin();
}

void loop_NTP()
{
	ntp.update();
}

//-----------------------------------------------------------------------------
// MQTT code:
//-----------------------------------------------------------------------------

void messageReceived(String &topic, String &payload)
{
	Serial.println("incoming: " + topic + " - " + payload);
}

void mqtt_publish(const char *topic, const char *message)
{
	if (mqtt.connected() == false)
		Serial.printf("MQTT: %s: %s\n", topic, message);
	else
		mqtt.publish(mqtt_topic + String(topic), message);
}
void mqtt_log(const char *message)
{
	Serial.printf("LOG: %s\n", message);
	if (mqtt.connected())
		mqtt.publish(mqtt_log_topic, message);
}
void mqtt_log(String message)
{
	mqtt_log(message.c_str());
}

void mqtt_subscribe()
{
	log("started...");
}
void mqtt_last_will()
{
	mqtt.setWill("/status/alive", "false");
}

bool mqtt_validate()
{
	if (mqtt.connected())
		return true;

	Serial.printf("MQTT: Try connect to %s...\n", mqtt_host);
	mqtt.connect(hostname);
	delay(100);

	if (mqtt.connected()) {
		Serial.printf("MQTT: connect done.\n");
		mqtt_last_will();
		mqtt_subscribe();
	}
	return mqtt.connected();
}

void setup_MQTT()
{
	mqtt.begin(mqtt_host, net);
	mqtt.onMessage(messageReceived);

	mqtt_validate();
}

void loop_MQTT()
{
	if (mqtt_validate()) {
		mqtt.loop();
	}
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

void draw_horizontal_segment(int x, int y, int w)
{
	u8g2.drawHLine(x, y, w);
	if (w > 5) {
		u8g2.drawHLine(x + 1, y - 1, w - 2);
		u8g2.drawHLine(x + 1, y + 1, w - 2);
	}
}

void draw_vertical_segment(int x, int y, int h)
{
	u8g2.drawVLine(x, y, h);
	if (h > 5) {
		u8g2.drawVLine(x - 1, y + 1, h - 2);
		u8g2.drawVLine(x + 1, y + 1, h - 2);
	}
}

void draw_segments(int x, int y, const char *digits, int dw, int dwv, int dh, int dhv)
{
	// u8g2.drawFrame(x,y, dw + 2 * dwv, 2 * dh + 2 * dhv);
	x += dwv + 1;
	y += 1;

	if (digits[0] != ' ') { // A segment
		draw_horizontal_segment(x, y, dw);
	}
	if (digits[1] != ' ') { // B segment
		draw_vertical_segment(x + dw + dwv - 1, y + dhv, dh);
	}
	if (digits[2] != ' ') { // C segment
		draw_vertical_segment(x + dw + dwv - 1, y + dh + dhv + dhv + dhv - 1, dh);
	}
	if (digits[3] != ' ') { // D segment
		draw_horizontal_segment(x, y + dh + dh + dhv + dhv + dhv + dhv - 2, dw);
	}
	if (digits[4] != ' ') { // E segment
		draw_vertical_segment(x - dwv, y + dh + dhv + dhv + dhv - 1, dh);
	}
	if (digits[5] != ' ') { // F segment
		draw_vertical_segment(x - dwv, y + dhv, dh);
	}
	if (digits[6] != ' ') { // G segment
		draw_horizontal_segment(x, y + dh + dhv + dhv - 1, dw);
	}
}

void draw_digit(int x, int y, int digit, int dw, int dwv, int dh, int dhv)
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

	draw_segments(x, y, segments, dw, dwv, dh, dhv);
}

void draw_2_numbers(int x, int y, int value, int dw, int dwv, int dh, int dhv)
{
	draw_digit(x, y, value / 10, dw, dwv, dh, dhv);
	draw_digit(x + dw + 4 * dwv + 2, y, value % 10, dw, dwv, dh, dhv);
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
		else if (brightness < raw_brightness)
			brightness++;

		if (brightness != raw_brightness) {
			log("brightness = %d", brightness);
			u8g2.setContrast(brightness);
		}
	}
}

void loop_VFD()
{
	adjust_vfd_brightness();
}

void draw_current_time(int x, int y)
{
	int dw = 14;
	int dwv = 1;
	int dh = 12;
	int dhv = 1;

	int xv = dw * 3 + 4 * dwv + dw / 2 + 2;

	draw_2_numbers(x, y, ntp.hours() , dw, dwv, dh, dhv);
	x += xv;
	draw_2_numbers(x, y, ntp.minutes() , dw, dwv, dh, dhv);
	x += xv;
	draw_2_numbers(x, y, ntp.seconds() , dw, dwv, dh, dhv);
}

void loop_VFD_1sec()
{
	// u8g2.setFont(u8g2_font_ncenB14_tr);
	int loops = 0;
	u8g2.firstPage();
	do {
		loops++;

		draw_current_time(0, 0);

		// draw_digit(1, 1, sec % 11, 3, 0, 5, 0);
		// draw_digit(10, 1, sec % 11, 3, 1, 5, 1);
		// draw_digit(20, 1, sec % 11, 10, 2, 12, 2);
		// draw_digit(40, 1, sec % 11, 14, 1, 12, 1);

		// if (sec % 2 == 0) {
		// 	u8g2.drawPixel(1, 1);
		// 	u8g2.drawPixel(10, 1);
		// 	u8g2.drawPixel(20, 1);
		// 	u8g2.drawPixel(40, 1);
		// }

		// u8g2.setFont(u8g2_font_10x20_me);

		//u8g2.setCursor(60, 30);
		//u8g2.print("Abc 123 ÄÖÜ äöü");
	} while (u8g2.nextPage());

	log("Loops %d, Time= %s\n", loops, ntp.formattedTime("%A %C %F %H"));
}

//-----------------------------------------------------------------------------
// Arduino setup & loop code:
//-----------------------------------------------------------------------------

void setup()
{
	Serial.begin(115200);

	Serial.printf("\n\nRunning....\n");

	setup_OTA_and_WIFI();
	setup_VFD();
	setup_NTP();
	setup_MQTT();
}

//Neotimer sec_timer = Neotimer(500);
Neotimer alive_timer = Neotimer(1000 * 60 * 10);

int last_sec = -1;

void loop()
{
	loop_OTA();
	loop_NTP();
	loop_MQTT();
	loop_VFD();

	sec = millis() / 1000;

	if (sec != last_sec) {
		last_sec = sec;
		loop_VFD_1sec();
	}

	if (alive_timer.repeat()) {
		mqtt_publish("/status/alive", "true");
	}
}
