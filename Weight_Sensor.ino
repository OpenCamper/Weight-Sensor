// Required Libarys:
// async-mqtt-client:  https://codeload.github.com/marvinroger/async-mqtt-client/zip/master
// ESPAsyncTCP:        https://codeload.github.com/me-no-dev/ESPAsyncTCP/zip/master
// HX711:              https://codeload.github.com/bogde/HX711/zip/master

#include <FS.h>                   //this needs to be first, or it all crashes and burns...
#include <ESP8266WiFi.h>          //ESP8266 Core WiFi Library
#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <WiFiClient.h>
#include <ESP8266mDNS.h>
#include <EEPROM.h>
#include <Ticker.h>
#include <AsyncMqttClient.h>
#include <HX711.h>
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <Esp.h>

AsyncMqttClient amqttClient;
WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;
Ticker mqttReconnectTimer;
Ticker wifiReconnectTimer;

HX711 scale(5, 4);

#define mqtt_topic_load_qos 0
#define mqtt_topic_setup_qos 0 // Keep 0 if you don't know what it is doing
#define MQTT_RECONNECT_TIME 2 // in seconds
#define mqtt_clientid "OpenCamper_Gas"
#define mqtt_user ""
#define mqtt_pass ""
#define mqtt_topic_load_a "wowa/gas/load/a"
#define mqtt_topic_load_b "wowa/gas/load/b"
#define mqtt_topic_setup_a "wowa/gas/setup/a"
#define mqtt_topic_setup_b "wowa/gas/setup/b"
char mqtt_server[40] = "10.10.31.1";
char mqtt_port[6] = "1883";
bool shouldSaveConfig = false;
float sum_a, sum_b, average_a, average_b;
char tara_a[10], tara_b[10], oldResult_a[10], oldResult_b[10], calibration_a[10] = "87100", calibration_b[10] = "87100", result_a[10], result_b[10];
unsigned char samples_a, samples_b;

void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}
void onWifiConnect(const WiFiEventStationModeGotIP& event) {
  Serial.println("Connected to Wi-Fi.");
  connectToMqtt();
}
void onWifiDisconnect(const WiFiEventStationModeDisconnected& event) {
  Serial.println("Disconnected from Wi-Fi.");
  mqttReconnectTimer.detach(); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
}
void connectToMqtt() {
  Serial.println("[MQTT] Connecting to MQTT...");
  amqttClient.connect();

  if (mqtt_user != "") {
    Serial.println("Authenticating with credentials to MQTT");
    amqttClient.setCredentials(mqtt_user, mqtt_pass);
  }
  else {
    Serial.println("Authenticating without credentials to MQTT");
  }
}
void onMqttConnect(bool sessionPresent) {
  Serial.println("[MQTT] Connected to MQTT!");
  mqttReconnectTimer.detach();
  Serial.print("Session present: ");
  Serial.println(sessionPresent);
  amqttClient.subscribe(mqtt_topic_setup_a, mqtt_topic_setup_qos);
  Serial.print("Subscribing to ");
  Serial.println(mqtt_topic_setup_a);
  amqttClient.subscribe(mqtt_topic_setup_b, mqtt_topic_setup_qos);
  Serial.print("Subscribing to ");
  Serial.println(mqtt_topic_setup_b);
}
void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  Serial.println("[MQTT] Disconnected from MQTT!");

  if (WiFi.isConnected()) {
    Serial.println("[MQTT] Trying to reconnect...");
    mqttReconnectTimer.once(MQTT_RECONNECT_TIME, connectToMqtt);
  }
}
void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
  if (!strcmp(topic, mqtt_topic_setup_a)) {
    Serial.print("Payload: ");
    Serial.println(payload);
    if(strcmp(payload, "Tara")) {
      //scale.set_gain(128);
      //scale.tare();
      Serial.println("Tara Channel A");
    }
  }
  else if (!strcmp(topic, mqtt_topic_setup_b)) {
    Serial.print("Payload: ");
    Serial.println(payload);
    if(strcmp(payload, "Tara")) {
      //scale.set_gain(32);
      //scale.tare();
      Serial.println("Tara Channel B");
    }
  }
}
void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  Serial.println(myWiFiManager->getConfigPortalSSID());
}
void MQTT_Publish(char result[10], char send_topic[50]) {
  if (amqttClient.connected()) {
    amqttClient.publish(send_topic, mqtt_topic_load_qos, true, result);
    Serial.print("Pushing new result: ");
    Serial.print(send_topic);
    Serial.print(" : ");
    Serial.println(result);
  }
  else {
    Serial.print("Can't pushing new result: ");
    Serial.println(result);
  }
}
void setup() {
  Serial.begin(115200);
  EEPROM.begin(512);
  Serial.println("Startup!");

  Serial.println("mounting FS...");
  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        std::unique_ptr<char[]> buf(new char[size]);
        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        if (json.success()) {
          Serial.println("\nparsed json");
          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(mqtt_user, json["mqtt_user"]);
          strcpy(mqtt_pass, json["mqtt_pass"]);
          strcpy(mqtt_topic_load_a, json["mqtt_topic_load_a"]);
          strcpy(mqtt_topic_load_b, json["mqtt_topic_load_b"]);
          strcpy(mqtt_topic_setup_a, json["mqtt_topic_setup_a"]);
          strcpy(mqtt_topic_setup_b, json["mqtt_topic_setup_b"]);
          strcpy(calibration_a, json["calibration_a"]);
          strcpy(calibration_b, json["calibration_b"]);
          strcpy(tara_a, json["tara_a"]);
          strcpy(tara_b, json["tara_b"]);
          Serial.println("[JSON] Read File");
          json.printTo(Serial);
        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }

  WiFiManagerParameter custom_mqtt_server("server", "10.10.31.1", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "1883", mqtt_port, 6);
  WiFiManagerParameter custom_mqtt_user("user", "mqtt user", mqtt_user, 32);
  WiFiManagerParameter custom_mqtt_pass("pass", "mqtt pass", mqtt_pass, 32);
  WiFiManagerParameter custom_calibration_a("Calibration A", "87100", calibration_a, 16);
  WiFiManagerParameter custom_calibration_b("Calibration B", "87100", calibration_b, 16);

  WiFiManager wifiManager;
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_mqtt_pass);
  wifiManager.addParameter(&custom_calibration_a);
  wifiManager.addParameter(&custom_calibration_b);

  if (!wifiManager.autoConnect()) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    ESP.reset();
    delay(5000);
  }

  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_user, custom_mqtt_user.getValue());
  strcpy(mqtt_pass, custom_mqtt_pass.getValue());
  strcpy(calibration_a, custom_calibration_a.getValue());
  strcpy(calibration_b, custom_calibration_b.getValue());

  if (shouldSaveConfig) {
    Serial.println("[JSON] saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["mqtt_user"] = mqtt_user;
    json["mqtt_pass"] = mqtt_pass;
    json["mqtt_topic_load_a"] = mqtt_topic_load_a;
    json["mqtt_topic_load_b"] = mqtt_topic_load_b;
    json["mqtt_topic_setup_a"] = mqtt_topic_setup_a;
    json["mqtt_topic_setup_b"] = mqtt_topic_setup_b;
    json["calibration_a"] = calibration_a;
    json["calibration_b"] = calibration_b;
    json["tara_a"] = "0";
    json["tara_b"] = "0";

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }
  wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
  wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);
  if (mqtt_server != "" && atoi(mqtt_port) > 0) {
    amqttClient.onConnect(onMqttConnect);
    amqttClient.onDisconnect(onMqttDisconnect);
    amqttClient.onMessage(onMqttMessage);
    amqttClient.setServer(mqtt_server, atoi(mqtt_port));
    if (mqtt_user != "" or mqtt_pass != "") amqttClient.setCredentials(mqtt_user, mqtt_pass);
    amqttClient.setClientId(mqtt_clientid);
    connectToMqtt();
  }
  Serial.print("mqtt_topic_load_a: ");
  Serial.println(mqtt_topic_load_a);
  Serial.print("mqtt_topic_load_b: ");
  Serial.println(mqtt_topic_load_b);
  Serial.print("mqtt_topic_setup_a: ");
  Serial.println(mqtt_topic_setup_a);
  Serial.print("mqtt_topic_setup_b: ");
  Serial.println(mqtt_topic_setup_b);
}
void loop() {
  scale.set_gain(128);
  scale.set_scale(int(calibration_a));
  scale.set_offset(long(tara_a));
  sprintf(result_a, "%f", scale.get_units(10));
  if (strcmp(oldResult_a, result_a) != 0) {
    MQTT_Publish(result_a, mqtt_topic_load_a);
    strcpy(oldResult_a, result_a);
  }

  scale.set_gain(32);
  scale.set_scale(int(calibration_b));
  scale.set_offset(long(tara_b));
  sprintf(result_b, "%f", scale.get_units(10));
  if (strcmp(oldResult_b, result_b) != 0) {
    MQTT_Publish(result_b, mqtt_topic_load_b);
    strcpy(oldResult_b, result_b);
  }

  delay(100);
}
