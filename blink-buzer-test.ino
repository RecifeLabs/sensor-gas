#include <Arduino.h>
#include <esp_arduino_version.h>

// Teste simples de hardware: LED D2 + buzzer em sincronia
// Ajuste conforme seu circuito
const int LED_PIN = 2;        // LED interno D2
const int BUZZER_PIN = 25;    // Pino do buzzer

// false = buzzer ativo (liga/desliga)
// true  = buzzer passivo (tom PWM)
const bool BUZZER_IS_PASSIVE = false;

// Configuração para buzzer passivo
const int BUZZER_TONE_HZ = 2200;
const int BUZZER_LEDC_CHANNEL = 0;
const int BUZZER_LEDC_RESOLUTION = 8;

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

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  if (BUZZER_IS_PASSIVE) {
    #if ESP_ARDUINO_VERSION_MAJOR >= 3
      ledcAttach(BUZZER_PIN, BUZZER_TONE_HZ, BUZZER_LEDC_RESOLUTION);
    #else
      ledcSetup(BUZZER_LEDC_CHANNEL, BUZZER_TONE_HZ, BUZZER_LEDC_RESOLUTION);
      ledcAttachPin(BUZZER_PIN, BUZZER_LEDC_CHANNEL);
    #endif
  }

  digitalWrite(LED_PIN, LOW);
  buzzerOff();

  Serial.println("=== BLINK BUZZER TEST ===");
  Serial.printf("LED_PIN=%d | BUZZER_PIN=%d | PASSIVO=%s\n",
                LED_PIN,
                BUZZER_PIN,
                BUZZER_IS_PASSIVE ? "true" : "false");
}

void loop() {
  // ON por 300ms
  digitalWrite(LED_PIN, HIGH);
  buzzerOn();
  Serial.println("ON");
  delay(300);

  // OFF por 300ms
  digitalWrite(LED_PIN, LOW);
  buzzerOff();
  Serial.println("OFF");
  delay(300);
}
