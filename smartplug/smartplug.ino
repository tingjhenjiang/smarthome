#include <EEPROM.h>
#include <WiFiClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include "time.h"

// Configuration for fallback access point 
// if Wi-Fi connection fails.
#define LED_BUILTIN	2
#define LD2410_PIN 14
#define RELAY2_PIN 12
#define ANALOG_IN_PIN A0
uint8 ld2410_out = 0;
int16_t analogInValue = 0;
char got_ip[16];
const char * AP_ssid = "ESP8266_fallback_AP";
const char * AP_password = "ESP8266_fallback_AP";
IPAddress AP_IP = IPAddress(10,1,1,1);
IPAddress AP_subnet = IPAddress(255,255,255,0);
// might using free https://broker.mqttgo.io/
// const int mqttServer_wssport = 8084;
// const int mqttServer_mqttport = 1883;
bool pubResult;
String mqttPubPayload;
int mqttIntPort;

uint16 loop_i = 0;
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 28800;
char timechars[40];


// Wi-Fi connection parameters.
// It will be read from the flash during setup.
struct WifiConf {
	char wifi_ssid[15];
	char wifi_password[15];
	char hostname[20];
	char mqttServer[50];
	char mqttServer_mqttport[5];
	char thingspeak_clientid[25];
	char thingspeak_username[25];
	char thingspeak_password[25];
	char thingspeak_topic[50];
	// Make sure that there is a 0 
	// that terminatnes the c string
	// if memory is not initalized yet.
	char cstr_terminator = 0; // make sure
};
WifiConf wifiConf;

const char* update_path = "/firmware";
const char* update_username = "admin";
const char* update_password = "admin";

// Web server for editing configuration.
// 80 is the default http port.
ESP8266WebServer httpServer(80);
// ESP8266 sleep problem https://arduino.stackexchange.com/questions/72569/cannot-connect-esp8266-web-server-after-some-time
ESP8266HTTPUpdateServer httpUpdater;

WiFiClient wifiClient;
// PubSubClient mqttClient(wifiClient);
PubSubClient mqttClient;

void timeh_getLocalTime(char * func_timechars, int charlength = 40, const char prefix='\0', const char suffix='\0' ) {
	struct tm timeinfo;
	if (!getLocalTime(&timeinfo)){
		Serial.println("Failed to obtain time");
		// return '\0';
	}
	// strftime(&timechars, 80, "Now it's %F %T.", &timeinfo);
	snprintf(func_timechars, charlength, "%d-%d-%d,%d:%d:%d",
		timeinfo.tm_year+1900,timeinfo.tm_mon+1,timeinfo.tm_mday,
		timeinfo.tm_hour,timeinfo.tm_min,timeinfo.tm_sec);
	Serial.println(timechars);
	// return *timechars;
}

void readWifiConf() {
	// Read wifi conf from flash
	for (int i=0; i<sizeof(wifiConf); i++) {
		((char *)(&wifiConf))[i] = char(EEPROM.read(i));
	}
	// Make sure that there is a 0 
	// that terminates the c string
	// if memory is not initalized yet.
	wifiConf.cstr_terminator = 0;
}

void writeWifiConf() {
	for (int i=0; i<sizeof(wifiConf); i++) {
		EEPROM.write(i, ((char *)(&wifiConf))[i]);
	}
	EEPROM.commit();
}

void setUpAccessPoint() {
	Serial.println("Setting up access point.");
	Serial.printf("SSID: %s\n", AP_ssid);
	Serial.printf("Password: %s\n", AP_password);

	WiFi.mode(WIFI_AP_STA);
	WiFi.softAPConfig(AP_IP, AP_IP, AP_subnet);
	if (WiFi.softAP(AP_ssid, AP_password)) {
		Serial.print("Ready. Access point IP: ");
		Serial.println(WiFi.softAPIP());
	} else {
		Serial.println("Setting up access point failed!");
	}
}

void handleWebServerRequest() {
	bool save = false;
	// char currentTime = timeh_getLocalTime();
	if (httpServer.hasArg("wifi_ssid") && httpServer.hasArg("wifi_password")) {
		httpServer.arg("wifi_ssid").toCharArray(
			wifiConf.wifi_ssid,
			sizeof(wifiConf.wifi_ssid));
		httpServer.arg("wifi_password").toCharArray(
			wifiConf.wifi_password,
			sizeof(wifiConf.wifi_password));
		httpServer.arg("hostname").toCharArray(
			wifiConf.hostname,
			sizeof(wifiConf.hostname));
		httpServer.arg("mqttServer").toCharArray(
			wifiConf.mqttServer,
			sizeof(wifiConf.mqttServer));
		httpServer.arg("mqttServer_mqttport").toCharArray(
			wifiConf.mqttServer_mqttport,
			sizeof(wifiConf.mqttServer_mqttport));
		httpServer.arg("thingspeak_clientid").toCharArray(
			wifiConf.thingspeak_clientid,
			sizeof(wifiConf.thingspeak_clientid));
		httpServer.arg("thingspeak_username").toCharArray(
			wifiConf.thingspeak_username,
			sizeof(wifiConf.thingspeak_username));
		httpServer.arg("thingspeak_password").toCharArray(
			wifiConf.thingspeak_password,
			sizeof(wifiConf.thingspeak_password));
		httpServer.arg("thingspeak_topic").toCharArray(
			wifiConf.thingspeak_topic,
			sizeof(wifiConf.thingspeak_topic));

		Serial.println(httpServer.arg("wifi_ssid"));
		Serial.println(wifiConf.wifi_ssid);

		writeWifiConf();
		save = true;
	}
	analogInValue = analogRead(ANALOG_IN_PIN);
	Serial.print("analogInValue is: ");
	Serial.println(analogInValue);

	String message = "";
	message += "<!DOCTYPE html>";
	message += "<html>";
	message += "<head>";
	message += "<title>ESP8266 configuration</title>";
	message += "</head>";
	message += "<body>";
	if (save) {
		message += "<div>Saved! Rebooting...</div>";
	} else {
		timeh_getLocalTime(timechars);
		message += "<h1>Network configuration</h1>";
		message += "<fieldset><legend>Wi-Fi</legend>";
		message += "<form action='/' method='POST'>";
		message += "<label>SSID: </label>";
		message += "<input type='text' name='wifi_ssid' value='" + String(wifiConf.wifi_ssid) + "'/>";
		message += "<label>Password: </label>";
		message += "<input type='password' name='wifi_password' value='" + String(wifiConf.wifi_password) + "'/></div>";
		message += "<label>Hostname: </label>";
		message += "<input type='text' name='hostname' value='" + String(wifiConf.hostname) + "'/>";
		message += "</fieldset>";
		message += "<fieldset><legend>MQTT configuration</legend>";
		message += "<label>mqttServer: </label>";
		message += "<input type='text' name='mqttServer' value='" + String(wifiConf.mqttServer) + "'/></div>";
		message += "<div><label>mqttServer_mqttport: </label>";
		message += "<input type='text' name='mqttServer_mqttport' value='" + String(wifiConf.mqttServer_mqttport) + "'/></div>";
		message += "<div><label>thingspeak_clientid: </label>";
		message += "<input type='text' name='thingspeak_clientid' value='" + String(wifiConf.thingspeak_clientid) + "'/></div>";
		message += "<div><label>thingspeak_username: </label>";
		message += "<input type='text' name='thingspeak_username' value='" + String(wifiConf.thingspeak_username) + "'/></div>";
		message += "<div><label>thingspeak_password: </label>";
		message += "<input type='password' name='thingspeak_password' value='" + String(wifiConf.thingspeak_password) + "'/></div>";
		message += "<div><label>thingspeak_topic: </label>";
		message += "<input type='text' name='thingspeak_topic' value='" + String(wifiConf.thingspeak_topic) + "'/></fieldset>";
		message += "<div><input type='submit' value='Save'/></div>";
		message += "</form>";
		message += "<h1><a href='" + String(update_path) + "'>Update Firmware</a></h1>";
		message += "<div>NTP Time: ";
		message += timechars;
		message += "</div><div>MAC address: ";
		message += WiFi.macAddress();
		message += "</div><div>IP Address: " + String(got_ip);
		message += "</div>";
	}
	message += "</body>";
	message += "</html>";
	httpServer.send(200, "text/html", message);

	if (save) {
		Serial.println("Wi-Fi configuration saved. Rebooting...");
		delay(1000);
		ESP.restart();
	}
}

void setUpOverTheAirProgramming() {

	// Change OTA port. 
	// Default: 8266
	// ArduinoOTA.setPort(8266);

	// Change the name of how it is going to 
	// show up in Arduino IDE.
	// Default: esp8266-[ChipID]
	// ArduinoOTA.setHostname("myesp8266");

	// Re-programming passowrd. 
	// No password by default.
	// ArduinoOTA.setPassword("123");

	ArduinoOTA.begin();
}

bool connectToWiFi() {
	Serial.print("Connecting to ");
	Serial.println(wifiConf.wifi_ssid);

	WiFi.mode(WIFI_STA);
	WiFi.hostname(wifiConf.hostname);
	WiFi.setSleepMode(WIFI_MODEM_SLEEP);
	WiFi.begin(wifiConf.wifi_ssid, wifiConf.wifi_password);
	if (WiFi.waitForConnectResult() == WL_CONNECTED) {
		Serial.print("Connected. IP: ");
		Serial.println(WiFi.localIP());
		return true;
	} else {
		Serial.println("Connection Failed!");
		return false;
	}
}

void setUpWebServer() {
	httpServer.on("/", handleWebServerRequest);
	httpServer.on("/publish", httpDisplayPublishMQTT);
	httpUpdater.setup(&httpServer, update_path, update_username, update_password);
	httpServer.begin();
}

void prepareConnectToMQTT() {
	mqttClient.setClient(wifiClient);
	// int intPort = *wifiConf.mqttServer_mqttport - '0';
	mqttIntPort = atoi(wifiConf.mqttServer_mqttport);
	// setup MQTT server
	mqttClient.setServer(wifiConf.mqttServer, mqttIntPort);
	// setup MQTT subscribe callback function
	// mqttClient.setCallback(receiveCallback);

	// connect MQTT server
	reconnectMQTTserver();
}

bool publishmqtt() {
	// prepareConnectToMQTT();
	// String ld2410_out_str = "";
	// ld2410_out_str = String(ld2410_out);
	reconnectMQTTserver();
	ld2410_out = digitalRead(LD2410_PIN);
	analogInValue = analogRead(ANALOG_IN_PIN);
	mqttPubPayload = "field1="+String(ld2410_out)+"&field2="+String(analogInValue)+"&status=MQTTPUBLISH";
	pubResult = mqttClient.publish(wifiConf.thingspeak_topic, mqttPubPayload.c_str());
	return pubResult;
}

void httpDisplayPublishMQTT() {
	String message = "";
	message += "<!DOCTYPE html><html><head><title>MQTT publish</title></head><body>publish ";
	pubResult = publishmqtt();
	if (pubResult) {
		message += ("succeed");
	} else {
		message += ("failed");
	}
	message += "</body></html>";
	httpServer.send(200, "text/html", message);
}

// 連接MQTT服務器並訂閱
void reconnectMQTTserver() {
	const char *clientId = wifiConf.thingspeak_clientid; //"esp8266-" + WiFi.macAddress();

	// 連接MQTT服務器
	while (!mqttClient.connected()) {
		Serial.print("Attempting MQTT connection with clientid ");
		Serial.print(clientId);
		Serial.println("...");
		if (mqttClient.connect(clientId, wifiConf.thingspeak_username, wifiConf.thingspeak_password)) { //.c_str()
			Serial.println("MQTT Server Connected.");
			Serial.print("Server Address: ");
			Serial.println(wifiConf.mqttServer);
			Serial.print("ClientId: ");
			Serial.println(clientId);
			// subscribeTopic(); // 訂閱指定主題 mqttClient.loop();
		} else {
			Serial.print("MQTT Server Connect Failed. Client State: ");
			Serial.println(mqttClient.state());
			Serial.println(" try again in 5 seconds");
			// delay(5000);
		}
		break;
	}

}

// 收到訊息後的callback
void receiveCallback(char* topic, byte* payload, unsigned int length) {
	Serial.print("Message Received [");
	Serial.print(topic);
	Serial.print("] ");
	for (int i = 0; i < length; i++) {
		Serial.print((char)payload[i]);
	}
	Serial.println("");
	Serial.print("Message Length(Bytes) ");
	Serial.println(length);
}

// 訂閱指定主題
void subscribeTopic(){

	String topicString = wifiConf.thingspeak_topic; //"Taichi-Maker-Sub-" + WiFi.macAddress();
	char subTopic[topicString.length() + 1];  
	strcpy(subTopic, topicString.c_str());

	// 通過serial output是否成功訂閱主題以及訂閱的主題名稱
	if(mqttClient.subscribe(subTopic)){
		Serial.println("Subscribed Topic: ");
		Serial.println(subTopic);
	} else {
		Serial.print("Subscribing Failed...");
	}  
}

void setup() {
	Serial.begin(115200);
	Serial.println("Booting...");
	pinMode(LED_BUILTIN, OUTPUT);
	pinMode(LD2410_PIN, INPUT_PULLUP);
	pinMode(RELAY2_PIN, OUTPUT);
	pinMode(RELAY2_PIN, OUTPUT);

	// init EEPROM object 
	// to read/write wifi configuration.
	EEPROM.begin(512);
	readWifiConf();

	if (!connectToWiFi()) {
		setUpAccessPoint();
	} else {
		strcpy(got_ip, WiFi.localIP().toString().c_str());
	}
	configTime(gmtOffset_sec, 0, ntpServer);

	prepareConnectToMQTT();
	MDNS.begin(AP_ssid);
	setUpWebServer();
	// setUpOverTheAirProgramming();
	MDNS.addService("http", "tcp", 80);

	Serial.printf("HTTPUpdateServer ready! Open %s or http://%s.local%s in your browser and login with username '%s' and password '%s'\n",
		got_ip, AP_ssid, update_path, update_username, update_password);
}

void loop() {
	// Give processing time for ArduinoOTA.
	// This must be called regularly
	// for the Over-The-Air upload to work.
	// ArduinoOTA.handle();

	// Give processing time for the webserver.
	// This must be called regularly
	// for the webserver to work.
	httpServer.handleClient();
	yield();
	MDNS.update();
	yield();

	if (loop_i % 8 == 0) {
		ld2410_out = digitalRead(LD2410_PIN);
		if (ld2410_out==1) {
			digitalWrite(RELAY2_PIN, HIGH);
		} else {
			digitalWrite(RELAY2_PIN, LOW);
		}
		yield();
	}
	if (loop_i % 160 == 0) {
		publishmqtt();
		yield();
	}

	digitalWrite(LED_BUILTIN, LOW);
	delay(75);
	yield();
	delay(75);
	yield();
	delay(75);
	yield();
	digitalWrite(LED_BUILTIN, HIGH);
	delay(75);
	yield();
	delay(75);
	yield();
	delay(75);
	yield();
	if (loop_i>=65535) {
		loop_i = 0;
	} else {
		loop_i++;
	}
}