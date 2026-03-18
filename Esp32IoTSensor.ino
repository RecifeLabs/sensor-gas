#include <WiFi.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <esp_arduino_version.h>

const char* ssid = "iot.labs.recife.br";
const char* password = "iot.PE.br6351461484Z+";
const char* serverUrl = "http://SEU_IP_AQUI:3000/alerta"; // Use o IP do seu Pop!_OS
const char* mqttBroker = "test.mosquitto.org";
const int mqttPort = 1883;
const char* mqttTopicAlert = "recifelabs/sensor-gas/alerta";
const char* mqttTopicLedState = "recifelabs/sensor-gas/led/state";
const char* mqttTopicBuzzerTest = "recifelabs/sensor-gas/buzzer/test";
const char* mqttTopicSensorStatus = "recifelabs/sensor-gas/sensor/status";

const int MQ2_PIN = 34;    // A0 do sensor
const int BUZZER_PIN = 25; // Pino de saída para buzzer (GPIO34 não serve: somente entrada)
const int LED_PIN = 2;     // LED Interno D2
const int THRESHOLD_ABSOLUTE = 40;    // Threshold absoluto mínimo para segurança GLP (muito baixo para detectar vazamentos)
const int THRESHOLD_RELATIVE = 250;   // Aumento percentual acima da baseline (ex: 250 = 2.5x)
const unsigned long SENSOR_READ_INTERVAL_MS = 250;
const unsigned long TELEMETRY_PUBLISH_INTERVAL_MS = 1000;
const unsigned long API_ALERT_MIN_INTERVAL_MS = 2000;
const unsigned long CALIBRATION_TIME_MS = 30000; // 30 segundos para calibração de baseline
const bool BUZZER_IS_PASSIVE = false; // false=ativo, true=passivo
const int BUZZER_TONE_HZ = 2200;
const int BUZZER_LEDC_CHANNEL = 0;
const int BUZZER_LEDC_RESOLUTION = 8;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

bool ledAlertActive = false;
unsigned long ledAlertStartMs = 0;
const unsigned long LED_ALERT_DURATION_MS = 10000;
const unsigned long LED_ALERT_ON_MS = 166;
const unsigned long LED_ALERT_OFF_MS = 167;
bool ledForcedMode = false;
bool ledForcedState = false;

bool buzzerAlertActive = false;
unsigned long buzzerAlertStartMs = 0;
unsigned long buzzerAlertDurationMs = 10000;
const unsigned long BUZZER_ALERT_ON_MS = 200;
const unsigned long BUZZER_ALERT_OFF_MS = 300;

unsigned long lastAlertMs = 0;
unsigned long lastSensorReadMs = 0;
unsigned long lastTelemetryPublishMs = 0;
unsigned long lastApiAlertMs = 0;
unsigned long calibrationStartMs = 0;
bool isCalibrated = false;
int baselineGasLevel = 0;
int maxBaselineReading = 0;

void mqttCallback(char* topic, byte* payload, unsigned int length);
void reconnectMqtt();
void startLedAlertPattern();
void updateLedAlertPattern();
void startBuzzerAlertPattern(unsigned long durationMs = 10000);
void updateBuzzerAlertPattern();
void triggerLocalAlert(unsigned long now, int nivelGas, const String& alertReason);
void publishSensorStatus(unsigned long now, int nivelGas, bool calibrating, bool alertTriggered, int percentageIncrease);
void buzzerOn();
void buzzerOff();

void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);

  if (BUZZER_IS_PASSIVE) {
    #if ESP_ARDUINO_VERSION_MAJOR >= 3
      ledcAttach(BUZZER_PIN, BUZZER_TONE_HZ, BUZZER_LEDC_RESOLUTION);
    #else
      ledcSetup(BUZZER_LEDC_CHANNEL, BUZZER_TONE_HZ, BUZZER_LEDC_RESOLUTION);
      ledcAttachPin(BUZZER_PIN, BUZZER_LEDC_CHANNEL);
    #endif
  }

  // --- TESTE DE HARDWARE AO LIGAR ---
  Serial.println("🛠️ Testando Hardware...");
  digitalWrite(LED_PIN, HIGH);
  buzzerOn();
  delay(1000); 
  digitalWrite(LED_PIN, LOW);
  buzzerOff();
  // ---------------------------------

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ WiFi Conectado!");
  Serial.println("\n🔧 Iniciando CALIBRAÇÃO de baseline...");
  Serial.println("⏱️  Aguarde 30 segundos para estabilizar leituras...");
  Serial.printf("📊 Threshold ABSOLUTO: %d ppm | Threshold RELATIVO: %d%% acima da baseline\n", THRESHOLD_ABSOLUTE, (THRESHOLD_RELATIVE / 10));
  calibrationStartMs = millis();

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
  updateBuzzerAlertPattern();

  const unsigned long now = millis();
  if (now - lastSensorReadMs < SENSOR_READ_INTERVAL_MS) {
    return;
  }
  lastSensorReadMs = now;

  int leitura = 0;
  for (int i = 0; i < 5; i++) {
    leitura += analogRead(MQ2_PIN);
    delay(1);
  }
  int nivelGas = leitura / 5;

  const bool absoluteCritical = nivelGas >= THRESHOLD_ABSOLUTE;
  int percentageIncrease = 0;

  // === FASE DE CALIBRAÇÃO (primeiros 30s) ===
  if (!isCalibrated && (now - calibrationStartMs) < CALIBRATION_TIME_MS) {
    if (nivelGas > maxBaselineReading) {
      maxBaselineReading = nivelGas;
    }
    unsigned long remainingMs = CALIBRATION_TIME_MS - (now - calibrationStartMs);
    Serial.printf("📊 [CALIBRAÇÃO] Nível: %d ppm | Máx: %d ppm | Tempo restante: %lus\n", nivelGas, maxBaselineReading, remainingMs / 1000);

    if (absoluteCritical) {
      String reason = String(nivelGas) + " ppm acima do mínimo absoluto (" + THRESHOLD_ABSOLUTE + " ppm) durante calibração";
      triggerLocalAlert(now, nivelGas, reason);
    }

    publishSensorStatus(now, nivelGas, true, absoluteCritical, percentageIncrease);
    return;
  }

  // === CALIBRAÇÃO COMPLETA ===
  if (!isCalibrated) {
    baselineGasLevel = maxBaselineReading;
    isCalibrated = true;
    Serial.printf("\n✅ CALIBRAÇÃO CONCLUÍDA!\n🎯 Baseline de referência: %d ppm\n", baselineGasLevel);
    Serial.println("🚨 Sistema em modo ALERTA SENSÍVEL para GLP\n");
  }

  // === LÓGICA DE DETECÇÃO INTELIGENTE ===
  bool alertTriggered = false;
  String alertReason = "";

  // Critério 1: Threshold absoluto mínimo (segurança)
  if (absoluteCritical) {
    alertTriggered = true;
    alertReason = String(nivelGas) + " ppm acima do mínimo absoluto (" + THRESHOLD_ABSOLUTE + " ppm)";
  }

  // Critério 2: Aumento relativo acima da baseline (detecção de mudanças)
  if (nivelGas > baselineGasLevel) {
    percentageIncrease = ((nivelGas - baselineGasLevel) * 1000) / max(baselineGasLevel, 1);
    if (percentageIncrease >= THRESHOLD_RELATIVE) {
      alertTriggered = true;
      alertReason = String(nivelGas) + " ppm = +" + (percentageIncrease / 10) + "% acima do baseline (" + baselineGasLevel + " ppm)";
    }
    Serial.printf("Nível: %d ppm | Baseline: %d ppm | Aumento: %d.%d%% (limite: %d%%)\n", 
                  nivelGas, baselineGasLevel, percentageIncrease / 10, percentageIncrease % 10, THRESHOLD_RELATIVE / 10);
  } else {
    Serial.printf("Nível: %d ppm | Normal (baseline: %d ppm)\n", nivelGas, baselineGasLevel);
  }

  publishSensorStatus(now, nivelGas, false, alertTriggered, percentageIncrease);

  if (alertTriggered) {
    triggerLocalAlert(now, nivelGas, alertReason);
  }
}

void triggerLocalAlert(unsigned long now, int nivelGas, const String& alertReason) {
  Serial.println("\n🚨 ALERTA ACIONADO! PERIGO DE VAZAMENTO DE GLP!");
  Serial.printf("   Motivo: %s\n\n", alertReason.c_str());

  if (ledForcedMode) {
    ledForcedMode = false;
    Serial.println("💡 [LED] Override local: saindo de modo forçado para alerta de gás");
  }

  if (!ledAlertActive) {
    startLedAlertPattern();
  }
  if (!buzzerAlertActive) {
    startBuzzerAlertPattern(10000);
  }
  lastAlertMs = now;

  if ((now - lastApiAlertMs) >= API_ALERT_MIN_INTERVAL_MS) {
    HTTPClient http;
    http.begin(serverUrl);
    http.addHeader("Content-Type", "application/json");
    String payload = "{\"local\":\"Cozinha Escola\",\"valor\":" + String(nivelGas) + ",\"baseline\":" + baselineGasLevel + ",\"razao\":\"" + alertReason + "\"}";
    int httpResponseCode = http.POST(payload);
    Serial.printf("📤 API status: %d\n", httpResponseCode);
    http.end();
    lastApiAlertMs = now;
  }
}

void publishSensorStatus(unsigned long now, int nivelGas, bool calibrating, bool alertTriggered, int percentageIncrease) {
  if (!mqttClient.connected()) {
    return;
  }

  if (!alertTriggered && (now - lastTelemetryPublishMs) < TELEMETRY_PUBLISH_INTERVAL_MS) {
    return;
  }

  lastTelemetryPublishMs = now;
  String payload = "{";
  payload += "\"local\":\"Cozinha Escola\",";
  payload += "\"nivel\":" + String(nivelGas) + ",";
  payload += "\"baseline\":" + String(baselineGasLevel) + ",";
  payload += "\"calibrating\":" + String(calibrating ? "true" : "false") + ",";
  payload += "\"critical\":" + String(alertTriggered ? "true" : "false") + ",";
  payload += "\"increasePermille\":" + String(percentageIncrease) + ",";
  payload += "\"thresholdAbsolute\":" + String(THRESHOLD_ABSOLUTE) + ",";
  payload += "\"thresholdRelative\":" + String(THRESHOLD_RELATIVE) + ",";
  payload += "\"timestampMs\":" + String(now);
  payload += "}";

  mqttClient.publish(mqttTopicSensorStatus, payload.c_str(), false);
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String incoming = "";
  for (unsigned int i = 0; i < length; i++) {
    incoming += (char)payload[i];
  }

  Serial.printf("📩 [MQTT] %s -> %s\n", topic, incoming.c_str());

  if (String(topic) == mqttTopicAlert && incoming.indexOf("ALERTA_GAS") >= 0) {
    Serial.println("🚨 [BACKEND] Nível inseguro recebido via MQTT");
    if (!ledForcedMode) {
      startLedAlertPattern();
    }
    startBuzzerAlertPattern(10000);
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

  if (String(topic) == mqttTopicBuzzerTest) {
    unsigned long duration = 3000;
    int idx = incoming.indexOf("\"durationMs\":");
    if (idx >= 0) {
      String digits = "";
      for (unsigned int i = idx + 13; i < incoming.length(); i++) {
        char c = incoming[i];
        if (c >= '0' && c <= '9') {
          digits += c;
        } else if (digits.length() > 0) {
          break;
        }
      }
      if (digits.length() > 0) {
        duration = digits.toInt();
      }
    }

    if (duration < 500) duration = 500;
    if (duration > 15000) duration = 15000;

    Serial.printf("🧪 [BUZZER TEST] Recebido via MQTT (%lums)\n", duration);
    startBuzzerAlertPattern(duration);
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
      mqttClient.subscribe(mqttTopicBuzzerTest);
      Serial.printf("📡 [MQTT] Inscrito em %s\n", mqttTopicAlert);
      Serial.printf("💡 [MQTT] Inscrito em %s\n", mqttTopicLedState);
      Serial.printf("🔊 [MQTT] Inscrito em %s\n", mqttTopicBuzzerTest);
      Serial.printf("📈 [MQTT] Publicando em %s\n", mqttTopicSensorStatus);
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

void startBuzzerAlertPattern(unsigned long durationMs) {
  buzzerAlertActive = true;
  buzzerAlertStartMs = millis();
  buzzerAlertDurationMs = durationMs;
  Serial.printf("🔊 [BUZZER] Alerta sonoro iniciado por %lums\n", buzzerAlertDurationMs);
}

void updateBuzzerAlertPattern() {
  if (!buzzerAlertActive) {
    return;
  }

  unsigned long elapsed = millis() - buzzerAlertStartMs;
  if (elapsed >= buzzerAlertDurationMs) {
    buzzerOff();
    buzzerAlertActive = false;
    Serial.printf("🔊 [BUZZER] Alerta sonoro finalizado (elapsed=%lums)\n", elapsed);
    return;
  }

  unsigned long cycle = BUZZER_ALERT_ON_MS + BUZZER_ALERT_OFF_MS;
  unsigned long pos = elapsed % cycle;
  if (pos < BUZZER_ALERT_ON_MS) {
    buzzerOn();
  } else {
    buzzerOff();
  }
}

void buzzerOn() {
  if (BUZZER_IS_PASSIVE) {
    #if ESP_ARDUINO_VERSION_MAJOR >= 3
      ledcWriteTone(BUZZER_PIN, BUZZER_TONE_HZ);
    #else
      ledcWriteTone(BUZZER_LEDC_CHANNEL, BUZZER_TONE_HZ);
    #endif
  } else {
    digitalWrite(BUZZER_PIN, HIGH);
  }
}

void buzzerOff() {
  if (BUZZER_IS_PASSIVE) {
    #if ESP_ARDUINO_VERSION_MAJOR >= 3
      ledcWriteTone(BUZZER_PIN, 0);
    #else
      ledcWriteTone(BUZZER_LEDC_CHANNEL, 0);
    #endif
  } else {
    digitalWrite(BUZZER_PIN, LOW);
  }
}