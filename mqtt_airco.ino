#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>

// === WiFi & MQTT ===
const char* ssid = "";
const char* password = "";
const char* mqtt_server = "";
const char* mqtt_user = "";
const char* mqtt_password = "";

// === Pins ===
#define RELAY_FASE D1
#define RELAY_GEEL D2
#define RELAY_WIT  D3
#define RELAY_BRUIN D4
#define DHTPIN D5
#define DHTTYPE DHT22
#define RELAY_WATER D6
#define WATER_SENSOR A0
DHT dht(DHTPIN, DHTTYPE);

// === Waterniveausensor ===
const int levelOnThreshold = 95;
const int levelOffThreshold = 10;
bool autoModeWater = true;
bool relayWaterState = false;
bool lastRelayWaterState = false;

// === Topics ===
const char* topicTemperature = "homeassistant/airco/temperature/state";
const char* topicHumidity = "homeassistant/airco/humidity/state";
const char* topicWaterLevel = "homeassistant/airco/waterlevel/state";
const char* topicWaterControl = "homeassistant/airco/waterpump/control";
const char* topicWaterStatus = "homeassistant/airco/waterpump/status";
const char* topicAircoControl = "homeassistant/airco/main/control";
const char* topicStatusAirco = "homeassistant/airco/main/status";
const char* topicStatusKnob = "homeassistant/airco/mode/status";
const char* topicSelectMode = "homeassistant/airco/mode/select";

// === Timers ===
unsigned long lastDHTUpdate = 0;
unsigned long lastWaterUpdate = 0;
const unsigned long dhtInterval = 30000;
const unsigned long waterInterval = 5000;

WiFiClient espClient;
PubSubClient client(espClient);

// === Status Airco ===
String currentMode = "uit";
bool faseAan = false;

// === Functies ===

void setup_wifi() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi verbonden: ");
  Serial.println(WiFi.localIP());
}

void resetOutputs() {
  digitalWrite(RELAY_GEEL, LOW);
  digitalWrite(RELAY_WIT, LOW);
  digitalWrite(RELAY_BRUIN, LOW);
}

void publishStatus() {
  client.publish(topicStatusAirco, faseAan ? "ON" : "OFF", true);
  client.publish(topicStatusKnob, currentMode.c_str(), true);
}

void publishDiscovery() {
  String devId = "wemosairco";
  String deviceName = "Wemos Airco Controller";
  String deviceJson = "\"device\":{\"identifiers\":[\"" + devId + "\"],\"name\":\"" + deviceName + "\"}";

  client.publish("homeassistant/switch/airco/config",
    ("{\"name\":\"Airco\",\"command_topic\":\"" + String(topicAircoControl) +
     "\",\"state_topic\":\"" + String(topicStatusAirco) +
     "\",\"payload_on\":\"ON\",\"payload_off\":\"OFF\",\"retain\":true,\"unique_id\":\"aircoswitch\"," + deviceJson + "}").c_str(), true);

  client.publish("homeassistant/select/airco_mode/config",
    ("{\"name\":\"Airco Stand\",\"command_topic\":\"" + String(topicSelectMode) +
     "\",\"state_topic\":\"" + String(topicStatusKnob) +
     "\",\"options\":[\"uit\",\"fan I\",\"fan II\",\"airco I\",\"airco II\"],"
     "\"retain\":true,\"unique_id\":\"aircomodeselect\"," + deviceJson + "}").c_str(), true);

  client.publish("homeassistant/sensor/airco_temperature/config",
    ("{\"name\":\"Airco Temperatuur\",\"state_topic\":\"" + String(topicTemperature) +
     "\",\"unit_of_measurement\":\"°C\",\"device_class\":\"temperature\",\"unique_id\":\"tempsensor\"," + deviceJson + "}").c_str(), true);

  client.publish("homeassistant/sensor/airco_humidity/config",
    ("{\"name\":\"Airco Vochtigheid\",\"state_topic\":\"" + String(topicHumidity) +
     "\",\"unit_of_measurement\":\"%\",\"device_class\":\"humidity\",\"unique_id\":\"humiditysensor\"," + deviceJson + "}").c_str(), true);

  client.publish("homeassistant/sensor/airco_waterlevel/config",
    ("{\"name\":\"Waterniveau\",\"state_topic\":\"" + String(topicWaterLevel) +
     "\",\"unit_of_measurement\":\"%\",\"device_class\":\"humidity\",\"unique_id\":\"waterniveausensor\"," + deviceJson + "}").c_str(), true);
}

void publishDHT() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();

  if (!isnan(h)) {
    String hs = String(h, 1);
    client.publish(topicHumidity, hs.c_str(), true);
  }
  if (!isnan(t)) {
    String ts = String(t, 1);
    client.publish(topicTemperature, ts.c_str(), true);
  }
}

void publishWaterLevel() {
  int val = analogRead(WATER_SENSOR);
  int percent = map(val, 0, 1023, 0, 100);
  percent = constrain(percent, 0, 100);

  char buffer[8];
  snprintf(buffer, sizeof(buffer), "%d", percent);
  client.publish(topicWaterLevel, buffer, true);

  if (autoModeWater) {
    if (percent >= levelOnThreshold) relayWaterState = true;
    else if (percent <= levelOffThreshold) relayWaterState = false;
  }

  if (relayWaterState != lastRelayWaterState) {
    digitalWrite(RELAY_WATER, relayWaterState ? HIGH : LOW);
    String s = relayWaterState ? "Relais AAN" : "Relais UIT";
    s += autoModeWater ? " via automatisch" : " via handmatig";
    client.publish(topicWaterStatus, s.c_str(), true);
    lastRelayWaterState = relayWaterState;
  }
}

void setMode(String mode) {
  if (mode == "uit") {
    resetOutputs();
    digitalWrite(RELAY_FASE, LOW);  // Hoofdfase uit
    faseAan = false;
    currentMode = "uit";
    publishStatus();
    return;
  }

  if (currentMode != mode) resetOutputs();
  currentMode = mode;

  if (!faseAan) {
    publishStatus();
    return;
  }

  if (mode == "fan I") digitalWrite(RELAY_GEEL, HIGH);
  else if (mode == "fan II") digitalWrite(RELAY_WIT, HIGH);
  else if (mode == "airco I") { digitalWrite(RELAY_GEEL, HIGH); digitalWrite(RELAY_BRUIN, HIGH); }
  else if (mode == "airco II") { digitalWrite(RELAY_WIT, HIGH); digitalWrite(RELAY_BRUIN, HIGH); }

  publishStatus();
}

void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) message += (char)payload[i];
  message.trim();

  if (String(topic) == topicAircoControl) {
    faseAan = (message == "ON");
    digitalWrite(RELAY_FASE, faseAan ? HIGH : LOW);
    setMode(faseAan ? currentMode : "uit");
  }

  if (String(topic) == topicSelectMode) {
    if (message == "fan I" || message == "fan II" || message == "airco I" || message == "airco II" || message == "uit") {
      setMode(message);
    }
  }

  if (String(topic) == topicWaterControl) {
    if (message == "aan") {
      autoModeWater = false;
      relayWaterState = true;
    } else if (message == "uit") {
      autoModeWater = false;
      relayWaterState = false;
    } else if (message == "auto") {
      autoModeWater = true;
    }
  }
}

void reconnect() {
  while (!client.connected()) {
    if (client.connect("wemosClient", mqtt_user, mqtt_password)) {
      client.subscribe(topicAircoControl);
      client.subscribe(topicSelectMode);
      client.subscribe(topicWaterControl);
      publishStatus();
      publishDiscovery();
    } else {
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_FASE, OUTPUT);
  pinMode(RELAY_GEEL, OUTPUT);
  pinMode(RELAY_WIT, OUTPUT);
  pinMode(RELAY_BRUIN, OUTPUT);
  pinMode(RELAY_WATER, OUTPUT);
  resetOutputs();
  digitalWrite(RELAY_FASE, LOW);
  digitalWrite(RELAY_WATER, LOW);
  dht.begin();
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  unsigned long now = millis();
  if (now - lastDHTUpdate > dhtInterval) {
    lastDHTUpdate = now;
    publishDHT();
  }

  if (now - lastWaterUpdate > waterInterval) {
    lastWaterUpdate = now;
    publishWaterLevel();
  }
}
