#include "FS.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFiAP.h>
#include <LittleFS.h>
#include <PubSubClient.h>

// my custom header
#include "env.h"

#define FORMAT_LITTLEFS_IF_FAILED true
#ifndef BOARD_LED
#define BOARD_LED 2
#endif

const int TEST_WIFI_TIMEOUT = 5000;

JsonDocument cfg;
bool isFinishSetup = false;
int lastLEDState = HIGH;
String configStr;tt
WebServer server;
WiFiClient espClient;
PubSubClient client(espClient);


String readFile(fs::FS &fs, const char *path) {
  Serial.printf("Reading file: %s\r\n", path);

  File file = fs.open(path);
  if (!file || file.isDirectory()) {
    Serial.println(F("failed to open file for reading"));
    return String();
  }

  Serial.println(F("read from file:"));
  String result;
  while (file.available()) {
    result += char(file.read());
  }
  file.close();
  return result;
}

void writeFile(fs::FS &fs, const char *path, const char *message) {
  Serial.printf("Writing file: %s\r\n", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println(F("failed to open file for writing"));
    return;
  }
  if (file.print(message)) {
    Serial.println(F("file written"));
  } else {
    Serial.println(F("write failed"));
  }
  file.close();
}

bool testWifiCreds(const char *ssid, const char *pass) {
  int time = 0;

  Serial.println(ssid);
  Serial.println(pass);

  delay(100);
  if (pass) {
    log_i("setup wifi using password");
    WiFi.begin(ssid, pass);
  } else {
    log_i("setup wifi without password");
    WiFi.begin(ssid);
  }
  delay(50);
  Serial.print(F("Testing WiFi Credential "));

  // check wifi status every 500ms
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    if (time == TEST_WIFI_TIMEOUT) {
      Serial.printf("\nWifi %s Not Connected\n", ssid);
      return false;
    }
    delay(500);
    time = time + 500;
  }
  Serial.printf("\nWifi %s Connected\n", ssid);
  return true;

}

void handleSetupForm() {
  File file;
  if (server.method() == HTTP_POST) {
    String ssid = server.arg("ssid");
    String pass = server.arg("password");
    if (ssid.isEmpty() || pass.isEmpty()) {
      file = LittleFS.open("/setup-error.html", "r");
      server.streamFile(file, "text/html");
      file.close();
      return;
    }

    if (testWifiCreds(ssid.c_str(), pass.c_str())) {
      server.sendHeader("Location", "/success");
      server.send(302, "text/plain", "Redirecting...");
      isFinishSetup = true;
      cfg["wifi_client_ssid"] = ssid;
      cfg["wifi_client_pass"] = pass;
    } else {
      file = LittleFS.open("/setup-error.html", "r");
      server.streamFile(file, "text/html");
      file.close();
    }
    return;

  } else {
    file = LittleFS.open("/setup.html", "r");
    server.streamFile(file, "text/html");
    file.close();
  }
}

void setupWiFiConn(fs::FS &fs) {
  WiFi.mode(WIFI_MODE_APSTA);
  Serial.print(F("starting WIFI AP : "));
  Serial.println(AP_SSID);

  if (!WiFi.softAP(AP_SSID)) {
    while (1)
      ;
  }

  if (MDNS.begin("aquaponic")) {
    Serial.println(F("MDNS responder started"));
  }

  server.on("/", handleSetupForm);
  server.begin();
  Serial.println(F("server for setup is running!"));

  while (!isFinishSetup) {
    server.handleClient();
    delay(10);
  }

  // cleaning connection
  delay(3000);
  server.close();
  WiFi.eraseAP();
  saveConfig(fs);
}

void saveConfig(fs::FS &fs) {
  serializeJson(cfg, configStr);
  writeFile(LittleFS, CONFIG_PATH, configStr.c_str());
}

void callback(char* topic, byte* message, unsigned int length) {
  Serial.print("Message arrived on topic: ");
  Serial.print(topic);
  Serial.print(". Message: ");
  String messageTemp;
  
  for (int i = 0; i < length; i++) {
    Serial.print((char)message[i]);
    messageTemp += (char)message[i];
  }
  Serial.println();

  if (String(topic) == "aquaponic/feedback") {
    Serial.print("Changing LED to ");
    if(messageTemp == "on"){
      Serial.println("on");
      digitalWrite(BOARD_LED, HIGH);
    }
    else if(messageTemp == "off"){
      Serial.println("off");
      digitalWrite(BOARD_LED, LOW);
    }
  }
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.println("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(DEVICE_ID)) {
      Serial.println("connected");
      // Subscribe
      client.subscribe("aquaponic/feedback");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void setup() {
  pinMode(BOARD_LED, OUTPUT);
  digitalWrite(BOARD_LED, LOW);
  log_i("STARTING!");

  Serial.begin(115200);
  if (!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)) {
    log_e("LittleFS Mount Failed");
    return;
  }

  WiFi.mode(WIFI_MODE_STA);

  if (LittleFS.exists(CONFIG_PATH)) {
    log_i("file exsist!");
    deserializeJson(cfg, readFile(LittleFS, CONFIG_PATH));
    String ssid = cfg["wifi_client_ssid"].as<String>();
    String pass = cfg["wifi_client_pass"].as<String>();
    ssid.trim();
    pass.trim();
    // if not connected enter setup mode
    bool hasValidWifi = testWifiCreds(ssid.c_str(), pass.c_str());
    if (!hasValidWifi) {
      setupWiFiConn(LittleFS);
    }
    serializeJson(cfg, configStr);
  } else {
    setupWiFiConn(LittleFS);
  }

  digitalWrite(BOARD_LED, HIGH);
  Serial.print(F("IP Address: "));
  Serial.println(WiFi.localIP());
  client.setServer(MQTT_BROKER, 1883);
  client.setCallback(callback);

}

JsonDocument result;
String resultStr;
void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  result["uptime"] = millis();
  serializeJson(result, resultStr);
  serializeJson(result, Serial);
  Serial.println();
  client.publish("aquaponic/sensor", resultStr.c_str());
  delay(3000);
}