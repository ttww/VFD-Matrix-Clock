#include "time.h"

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <neotimer.h>
#include <MQTT.h>
#include <HTTPClient.h>

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
// WIFI declarations:
//-----------------------------------------------------------------------------
bool wifi_connected = false;

void setup_after_WIFI_connect();

//-----------------------------------------------------------------------------
// MQTT declarations:
//-----------------------------------------------------------------------------
WiFiClient net;
MQTTClient mqtt;

void setup_MQTT();

void mqtt_log(const char *message);
void mqtt_log(String message);

//-----------------------------------------------------------------------------
// VFD-Display declarations:
//-----------------------------------------------------------------------------
U8G2_GP1287AI_256X50_1_4W_HW_SPI
u8g2(U8G2_R2, /* cs=*/PIN_VFD_CHIPSELECT,
     /* dc=*/PIN_VFD_CLOCK,
     /* reset=*/PIN_VFD_RESET /* U8X8_PIN_NONE , PIN_VFD_RESET */);

//-----------------------------------------------------------------------------
// HTTP declarations:
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// NTP declarations:
// (we are using the ESP internal NTP implementation...)
//-----------------------------------------------------------------------------
void setup_NTP();
String timezone = "UTC0";

// Timezone TZ string, eg. "CET-1CEST,M3.5.0,M10.5.0/3" for "Europe/Berlin".
// Setup via CSV lookup.
String timezone_definition;

struct tm timeinfo; // Updated in loop

//-----------------------------------------------------------------------------
// Clock declarations:
//-----------------------------------------------------------------------------
int ldr, brightness, sec;

//-----------------------------------------------------------------------------
// Utils code:
//-----------------------------------------------------------------------------

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

String http_get_request(String requestUrl)
{
	HTTPClient http;

	log("HTTP Request: %s", requestUrl.c_str());

	// Your Domain name with URL path or IP address with path.
	// Host/IP must have a / before a query parameter!
	http.begin(requestUrl);
	//http.setAuthorization("REPLACE_WITH_SERVER_USERNAME", "REPLACE_WITH_SERVER_PASSWORD");

	int httpResponseCode = http.GET();

	String payload = "";

	log("HTTP Response code: %d", httpResponseCode);

	if (httpResponseCode > 0) {
		payload = http.getString();
		//log("HTTP Payload      : %s", payload.c_str());
	}
	http.end();

	return payload;
}

//-----------------------------------------------------------------------------
// OTA / WIFI code:
//-----------------------------------------------------------------------------

void setup_OTA()
{
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
}

void setup_WIFI()
{
	log("Try to setup WIFI");
	WiFi.mode(WIFI_STA);
	log("Try to connect to SSID %s", ssid);

	WiFi.begin(ssid, password);
}

void loop_WIFI()
{
	if (wifi_connected == false && WiFi.status() == WL_CONNECTED) {
		wifi_connected = true;
		log("Wifi %s connected.", ssid);
		log("IP address  : %s", WiFi.localIP().toString().c_str());
		if (WiFi.dnsIP())
			log("DNS resolver: %s", WiFi.dnsIP().toString().c_str());

		setup_after_WIFI_connect();
	}

	if (wifi_connected == true && WiFi.status() != WL_CONNECTED) {
		log("Lost wifi, reboot...");
		delay(1000);
		ESP.restart();
	}
}

void loop_OTA()
{
	ArduinoOTA.handle();
}

void setup_after_WIFI_connect()
{
	setup_OTA();
	setup_MQTT();
	setup_NTP();
}

//-----------------------------------------------------------------------------
// NTP code:
//-----------------------------------------------------------------------------

// from https://randomnerdtutorials.com/esp32-ntp-timezones-daylight-saving/, adapted
void setTimezone(String timezone)
{
	log("  Setting Timezone to %s\n", timezone.c_str());
	setenv("TZ", timezone.c_str(), 1); //  Now adjust the TZ.  Clock settings are adjusted to show the new local time
	tzset();
}

// void setTime(int yr, int month, int mday, int hr, int minute, int sec, int isDst)
// {
// 	struct tm tm;

// 	tm.tm_year = yr - 1900; // Set date
// 	tm.tm_mon = month - 1;
// 	tm.tm_mday = mday;
// 	tm.tm_hour = hr; // Set time
// 	tm.tm_min = minute;
// 	tm.tm_sec = sec;
// 	tm.tm_isdst = isDst; // 1 or 0
// 	time_t t = mktime(&tm);

// 	log("Setting time: %s", asctime(&tm));
// 	struct timeval now = { .tv_sec = t };
// 	settimeofday(&now, NULL);
// }

void initTime(String timezone)
{
	log("Setting up time");
	configTime(0, 0, "pool.ntp.org"); // First connect to NTP server, with 0 TZ offset
	if (!getLocalTime(&timeinfo)) {
		log("  Failed to obtain time");
		return;
	}
	log("  Got the time from NTP");
	setTimezone(timezone);
}

// void printLocalTime()
// {
// 	struct tm timeinfo;
// 	if (!getLocalTime(&timeinfo)) {
// 		log("Failed to obtain time 1");
// 		return;
// 	}
// 	Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S zone %Z %z ");
// }

String get_timezone_definition(String timezone)
{
	const char *timezone_url = "https://raw.githubusercontent.com/nayarsystems/posix_tz_db/master/zones.csv";

	String csv_content = http_get_request(timezone_url);

	// Content of file is:
	//	"Africa/Abidjan","GMT0"
	//	"Africa/Accra","GMT0"
	//	"Africa/Addis_Ababa","EAT-3"
	// ...
	String search_pattern = "\"" + timezone + "\",\"";
	int idx = csv_content.indexOf(search_pattern);
	if (idx != -1) {
		idx += search_pattern.length();
		int idx_end = csv_content.indexOf("\"", idx);
		if (idx_end != -1) {
			return csv_content.substring(idx, idx_end);
		}
	}

	return "UTC0";
}

// Automatic timezone selection:
// We are doing a geo location of our router address.
// 1. Get the external router address.
// 2. We lookup the timezone of this IP.
// 3. We lookup the timezone in an timezones.csv for the exact TZ definition (DST etc...)
void setup_timezone()
{
	// Get outside IP for geo location:
	const char *resolveExternalIpUrl = "http://api.ipify.org/?format=text";
	const char *resolveTimezoneViaUrl = "https://timeapi.io/api/TimeZone/ip?ipAddress=";

	log("Get external IP");
	String external_ip = http_get_request(resolveExternalIpUrl);
	if (external_ip != "") {
		log("External IP is %s", external_ip.c_str());

		String timezone_json = http_get_request(resolveTimezoneViaUrl + external_ip);

		// Direct lookup, don't use full blown json lib for this...:
		const char *search_pattern = "\"timeZone\":\"";
		int idx = timezone_json.indexOf(search_pattern);
		if (idx != -1) {
			idx += strlen(search_pattern);
			int idx_end = timezone_json.indexOf("\"", idx);
			if (idx_end != -1) {
				timezone = timezone_json.substring(idx, idx_end);
				timezone_definition = get_timezone_definition(timezone);
				log("timezone is            %s", timezone.c_str());
				log("timezone_definition is %s", timezone_definition.c_str());
				//printLocalTime();
				setTimezone(timezone_definition);
				getLocalTime(&timeinfo);
				//printLocalTime();
			}
		}
	}
}

void setup_NTP()
{
	initTime(timezone);

	setup_timezone();
}

void loop_NTP()
{
	// NTP is running in the background, started by configTime() in initTime()
	getLocalTime(&timeinfo);
	//ntp.update();
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

long lastLogMs = 0;

void mqtt_log(const char *message)
{
	char full[20 + 100];

	long usedMs = millis() - lastLogMs;

	snprintf(full, sizeof(full), "+%2d.%03d: %s", (int)(usedMs / 1000), (int)(usedMs % 1000), message);
	lastLogMs = millis();
	full[sizeof(full) - 1] = '\0';

	Serial.printf("LOG: %s\n", full);
	if (mqtt.connected())
		mqtt.publish(mqtt_log_topic, full);
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

	log("MQTT: Not connected, try connect to %s...", mqtt_host);
	mqtt.connect(hostname);

	// Try connect for 200ms:
	int tries = 0;
	while (!mqtt.connected() && tries++ < 200) {
		delay(1);
	}

	if (mqtt.connected()) {
		log("MQTT: connect done.");
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

	digitalWrite(PIN_VFD_FILAMENT, HIGH);

	digitalWrite(PIN_VFD_LDR, HIGH);
	pinMode(PIN_VFD_LDR, INPUT_PULLUP);

	u8g2.begin();

	u8g2.setDisplayRotation(U8G2_R0);

	u8g2.enableUTF8Print(); // enable UTF8 support for the Arduino print() function
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
			//log("brightness = %d", brightness);
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

	// ToDo: Cleanups....

	int xv = (int)(dw * 2.5 + 4 * dwv + dw / 2);
	int xvp = (int)xv - (dw * 0.30);

	int yv = (int)(dh * 2 + 4 * dhv);
	int dpy = (int)(yv * 0.2);

	draw_2_numbers(x, y, timeinfo.tm_hour, dw, dwv, dh, dhv);
	u8g2.drawBox(x + xvp, y + yv / 2 - dpy, 2, 2);
	u8g2.drawBox(x + xvp, y + yv / 2 + dpy, 2, 2);
	x += xv;

	draw_2_numbers(x, y, timeinfo.tm_min, dw, dwv, dh, dhv);
	u8g2.drawBox(x + xvp, y + yv / 2 - dpy, 2, 2);
	u8g2.drawBox(x + xvp, y + yv / 2 + dpy, 2, 2);
	x += xv;

	draw_2_numbers(x, y, timeinfo.tm_sec, dw, dwv, dh, dhv);
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

		//		u8g2.setFont(u8g2_font_10x20_me);
		u8g2.setFont(u8g2_font_6x10_tf);

		u8g2.setCursor(0, 48);
		u8g2.printf("Free Memory = %ld  ", ESP.getFreeHeap());

		u8g2.setFont(u8g2_font_5x8_tf);
		u8g2.setCursor(154, 10);
		u8g2.printf("%s        ", timezone.c_str());

	} while (u8g2.nextPage());

	//log("Loops %d, Time= %s", loops, ntp.formattedTime("%A %C %F %H"));
}

//-----------------------------------------------------------------------------
// Arduino setup & loop code:
//-----------------------------------------------------------------------------

void setup()
{
	Serial.begin(115200);
	delay(50);
	Serial.printf("\n\n");
	Serial.printf("Running....\n");
	Serial.printf("---------------------------------------------------------\n");

	setup_WIFI();
	setup_VFD();
}

//Neotimer sec_timer = Neotimer(500);
Neotimer alive_timer = Neotimer(1000 * 60 * 10);

int last_sec = -1;

void loop()
{
	loop_WIFI();

	if (wifi_connected) {
		loop_OTA();
		loop_MQTT();
		loop_NTP();
		sec = timeinfo.tm_sec;
	}
	else {
		sec = millis() / 1000;
	}

	loop_VFD();

	if (sec != last_sec) {
		last_sec = sec;
		loop_VFD_1sec();

		// Retry timezone lookup:
		if (sec == 0 && timezone_definition == "") {
			setup_timezone();
		}
	}

	if (alive_timer.repeat()) {
		mqtt_publish("/status/alive", "true");
	}
}
