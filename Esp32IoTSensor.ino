#include <WiFi.h>
#include <HTTPClient.h>

const char* ssid = "iot.labs.recife.br";
const char* password = "iot.PE.br6351461484Z+";
const char* serverUrl = "http://SEU_IP_AQUI:3000/alerta"; // Use o IP do seu Pop!_OS

const int MQ2_PIN = 34;    // A0 do sensor
const int BUZZER_PIN = 13; // Sirene
const int LED_PIN = 2;     // LED Interno D2
const int THRESHOLD = 1100;

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
}

void loop() {
  int leitura = 0;
  for(int i=0; i<10; i++) { leitura += analogRead(MQ2_PIN); delay(10); }
  int nivelGas = leitura / 10;

  Serial.printf("Nível de Gás: %d\n", nivelGas);

  if (nivelGas > THRESHOLD) {
    Serial.println("⚠️ ALERTA ACIONADO!");
    digitalWrite(LED_PIN, HIGH);
    
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
    }
    
    digitalWrite(LED_PIN, LOW);
    delay(30000); // Cooldown
  }
  delay(2000);
}