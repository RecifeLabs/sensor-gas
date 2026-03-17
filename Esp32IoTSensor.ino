#include <WiFi.h>
#include <HTTPClient.h>
#include <PubSubClient.h>

const char* ssid = "iot.labs.recife.br";
const char* password = "iot.PE.br6351461484Z+";
const char* serverUrl = "http://SEU_IP_AQUI:3000/alerta"; // Use o IP do seu Pop!_OS
const char* mqttBroker = "test.mosquitto.org";
const int mqttPort = 1883;
const char* mqttTopicAlert = "recifelabs/sensor-gas/alerta";
const char* mqttTopicLedState = "recifelabs/sensor-gas/led/state";

const int MQ2_PIN = 34;    // A0 do sensor
const int BUZZER_PIN = 13; // Sirene
const int LED_PIN = 2;     // LED Interno D2
const int THRESHOLD = 1100;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

bool ledAlertActive = false;
unsigned long ledAlertStartMs = 0;
const unsigned long LED_ALERT_DURATION_MS = 10000;
const unsigned long LED_ALERT_ON_MS = 166;
const unsigned long LED_ALERT_OFF_MS = 167;
bool ledForcedMode = false;
bool ledForcedState = false;

void mqttCallback(char* topic, byte* payload, unsigned int length);
void reconnectMqtt();
void startLedAlertPattern();
void updateLedAlertPattern();

void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);

  // --- TESTE DE HARDWARE AO LIGAR ---
  Serial.println("🛠️ Testando Hardware...");
  digitalWrite(LED_PIN, HIGH);
  digitalWrite(BUZZER_PIN, HIGH);
  delay(1000); 
  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  // ---------------------------------

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ WiFi Conectado!");

  mqttClient.setServer(mqttBroker, mqttPort);
  mqttClient.setCallback(mqttCallback);
  reconnectMqtt();
}

void loop() {
  if (WiFi.status() == WL_CONNECTED && !mqttClient.connected()) {
    reconnectMqtt();
  }

  mqttClient.loop();
  if (!ledForcedMode) {
    updateLedAlertPattern();
  } else {
    digitalWrite(LED_PIN, ledForcedState ? HIGH : LOW);
  }

  int leitura = 0;
  for(int i=0; i<10; i++) { leitura += analogRead(MQ2_PIN); delay(10); }
  int nivelGas = leitura / 10;

  Serial.printf("Nível de Gás: %d\n", nivelGas);

  if (nivelGas > THRESHOLD) {
    Serial.println("⚠️ ALERTA ACIONADO!");
    if (!ledForcedMode) {
      startLedAlertPattern();
    }
    
    // Enviar para API
    HTTPClient http;
    http.begin(serverUrl);
    http.addHeader("Content-Type", "application/json");
    String payload = "{\"local\":\"Cozinha Escola\",\"valor\":" + String(nivelGas) + "}";
    int httpResponseCode = http.POST(payload);
    http.end();

    // Alarme sonoro intermitente (10 segundos)
    for(int j=0; j<20; j++) {
      digitalWrite(BUZZER_PIN, HIGH); delay(200);
      digitalWrite(BUZZER_PIN, LOW); delay(300);
      if (!ledForcedMode) {
        updateLedAlertPattern();
      }
      mqttClient.loop();
    }

    delay(30000); // Cooldown
  }
  delay(2000);
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String incoming = "";
  for (unsigned int i = 0; i < length; i++) {
    incoming += (char)payload[i];
  }

  Serial.printf("📩 [MQTT] %s -> %s\n", topic, incoming.c_str());

  if (String(topic) == mqttTopicAlert && incoming.indexOf("ALERTA_GAS") >= 0) {
    if (!ledForcedMode) {
      startLedAlertPattern();
    }
  }

  if (String(topic) == mqttTopicLedState) {
    if (incoming.indexOf("\"state\":\"on\"") >= 0 || incoming == "on") {
      ledForcedMode = true;
      ledForcedState = true;
      ledAlertActive = false;
      digitalWrite(LED_PIN, HIGH);
      Serial.println("💡 [LED] Estado forçado ON por tópico");
    } else if (incoming.indexOf("\"state\":\"off\"") >= 0 || incoming == "off") {
      ledForcedMode = true;
      ledForcedState = false;
      ledAlertActive = false;
      digitalWrite(LED_PIN, LOW);
      Serial.println("💡 [LED] Estado forçado OFF por tópico");
    } else if (incoming.indexOf("\"state\":\"auto\"") >= 0 || incoming == "auto") {
      ledForcedMode = false;
      digitalWrite(LED_PIN, LOW);
      Serial.println("💡 [LED] Modo automático restaurado");
    }
  }
}

void reconnectMqtt() {
  while (!mqttClient.connected()) {
    String clientId = "esp32-gas-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    Serial.print("🔌 [MQTT] Conectando...");
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("conectado");
      mqttClient.subscribe(mqttTopicAlert);
      mqttClient.subscribe(mqttTopicLedState);
      Serial.printf("📡 [MQTT] Inscrito em %s\n", mqttTopicAlert);
      Serial.printf("💡 [MQTT] Inscrito em %s\n", mqttTopicLedState);
    } else {
      Serial.printf("falha rc=%d. Tentando em 3s\n", mqttClient.state());
      delay(3000);
    }
  }
}

void startLedAlertPattern() {
  ledAlertActive = true;
  ledAlertStartMs = millis();
  Serial.println("💡 [LED] Alerta visual iniciado: 3 piscadas/seg por 10s");
}

void updateLedAlertPattern() {
  if (!ledAlertActive) {
    return;
  }

  unsigned long elapsed = millis() - ledAlertStartMs;
  if (elapsed >= LED_ALERT_DURATION_MS) {
    digitalWrite(LED_PIN, LOW);
    ledAlertActive = false;
    Serial.println("💡 [LED] Alerta visual finalizado");
    return;
  }

  unsigned long cycle = LED_ALERT_ON_MS + LED_ALERT_OFF_MS;
  unsigned long pos = elapsed % cycle;
  digitalWrite(LED_PIN, pos < LED_ALERT_ON_MS ? HIGH : LOW);
}