/*******************************************************
 *  ESP32 + AT-09 (HM-10 BLE UART)  ⇄  MQTT (Mosquitto)
 *  Puente BLE↔WiFi con JSON (ArduinoJson) y LEDs de estado
 *  Integrantes: Pedro Iván Palomino Viera, Luis Antonio Torres Padrón
 *  ID EQUIPO  : PIPV_LATP
 *******************************************************/
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>

// ====== CONFIGURACIÓN DE RED ======
const char* WIFI_SSID     = "Planta_alta";
const char* WIFI_PASSWORD = "65431993";

// NTP para fecha/hora
const char* NTP_SERVER1 = "pool.ntp.org";
const long  GMT_OFFSET  = -6 * 3600; // México Centro
const int   DST_OFFSET  = 0;

// ====== MQTT (Mosquitto público) ======
const char* MQTT_HOST   = "test.mosquitto.org";
const uint16_t MQTT_PORT = 1883;
const char* TEAM_ID     = "PIPV_LATP";
const char* TOPIC_TX    = "PIPV_LATP_TX";   // salidas originadas en el ESP32
const char* TOPIC_RX    = "PIPV_LATP_RX";   // todo RX válido que procese el ESP32
const char* TOPIC_LOG   = "PIPV_LATP_LOG";  // bitácora
const char* TOPIC_EQ_RX = "EQUIPO_RX";      // ENTRADA desde la nube al equipo

WiFiClient espClient;
PubSubClient mqtt(espClient);

// ====== UART con AT-09 / HM-10 ======
HardwareSerial BLE(2);
const int BLE_TX_PIN = 27;  // ESP32 -> AT-09 RX
const int BLE_RX_PIN = 26;  // ESP32 <- AT-09 TX
const uint32_t BLE_BAUD = 9600;

// ====== LEDS ======
const int LED_VERDE   = 4;   // enviado
const int LED_AMARILLO= 16;  // recibido válido
const int LED_ROJO    = 17;  // BT no emparejado
const int LED_AZUL    = 5;   // BT emparejado
const int LED_BLANCO  = 18;  // WiFi buscando / conectado
const int LED_NARANJA = 19;  // recibido con ID inválido
const int LED_VIOLETA = 23;  // control remoto ON/OFF

// ====== TIEMPOS ======
const uint32_t BLINK_HALF   = 500;   // 0.5 s
const uint32_t PULSE_1S     = 1000;  // 1 s

// ====== MÁQUINA DE ESTADOS ======
enum class State { WIFI_CONNECTING, MQTT_CONNECTING, BLE_DISCONNECTED, RUNNING };
State state = State::WIFI_CONNECTING;

// ====== BUFFERS ======
String bleBuffer;
String serialBuffer;

// ====== AUXILIARES NO BLOQUEANTES ======
uint32_t lastBlink = 0;
bool blinkFlag = false;
uint32_t pulseOffAt = 0;

// ====== WiFi: reintentos espaciados ======
bool wifiBegun = false;
unsigned long wifiLastAttempt = 0;
const uint32_t WIFI_RETRY_MS = 10000;
const bool WIFI_AUTO_RECONNECT = true;

// ====== BLE / Conexión ======
bool bleConnected = false;          // Solo lo cambiamos con STATE (o notificaciones si no usas STATE)
uint32_t lastBleActivity = 0;       // Para logs

// ====== (NUEVO) Lectura del pin STATE del AT-09 ======
#define USE_BLE_STATE_PIN 1          // <<-- deja 1 para usar STATE, 0 si no lo conectas
const int BLE_STATE_PIN = 14;        // GPIO del ESP32 conectado a STATE del AT-09

// ---------- Utilidades de LEDs ----------
void allLedsOff() {
  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_AMARILLO, LOW);
  digitalWrite(LED_ROJO, LOW);
  digitalWrite(LED_AZUL, LOW);
  digitalWrite(LED_BLANCO, LOW);
  digitalWrite(LED_NARANJA, LOW);
}

void pulseLed(int pin, uint32_t ms = PULSE_1S) {
  digitalWrite(pin, HIGH);
  pulseOffAt = millis() + ms;
}

void updatePulse() {
  if (pulseOffAt && millis() >= pulseOffAt) {
    digitalWrite(LED_VERDE, LOW);
    digitalWrite(LED_AMARILLO, LOW);
    digitalWrite(LED_NARANJA, LOW);
    pulseOffAt = 0;
  }
}

// Exclusión entre rojo y azul (nunca simultáneos)
void setBlue(bool on) { 
  if (on){ digitalWrite(LED_ROJO, LOW); digitalWrite(LED_AZUL, HIGH); }
  else    { digitalWrite(LED_AZUL, LOW); }
}
void setRed (bool on) { 
  if (on){ digitalWrite(LED_AZUL, LOW); digitalWrite(LED_ROJO, HIGH); }
  else    { digitalWrite(LED_ROJO, LOW); }
}

// ---------- Fecha y hora ----------
void getDateTime(char* outDate, char* outTime) {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) { strcpy(outDate,"00-00-0000"); strcpy(outTime,"00:00:00"); return; }
  strftime(outDate, 11, "%d-%m-%Y", &timeinfo);
  strftime(outTime, 9,  "%H:%M:%S", &timeinfo);
}

// ---------- Construir JSON ----------
String buildJSON(const char* origen, const char* accion) {
  StaticJsonDocument<256> doc;
  char f[11], h[9]; getDateTime(f,h);
  doc["id"]     = TEAM_ID;
  doc["origen"] = origen;
  doc["accion"] = accion;
  doc["fecha"]  = f;
  doc["hora"]   = h;
  String out; serializeJson(doc, out); return out;
}

// ---------- Validar/Procesar JSON ----------
bool processJSON(const String& payload, const char* sourceTag) {
  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) { 
    pulseLed(LED_NARANJA);
    mqtt.publish(TOPIC_LOG, ("JSON inválido desde " + String(sourceTag)).c_str()); 
    return false; 
  }

  const char* id     = doc["id"]     | "";
  const char* accion = doc["accion"] | "";
  if (String(id) != TEAM_ID) { 
    pulseLed(LED_NARANJA); 
    mqtt.publish(TOPIC_LOG, ("ID inválido en mensaje desde " + String(sourceTag)).c_str()); 
    return false; 
  }

  // JSON válido: feedback visual
  pulseLed(LED_AMARILLO);

  // Control LED violeta
  if (strcasecmp(accion, "ON") == 0) digitalWrite(LED_VIOLETA, HIGH);
  else if (strcasecmp(accion, "OFF") == 0) digitalWrite(LED_VIOLETA, LOW);

  // Publicación de RX (salida para monitoreo)
  mqtt.publish(TOPIC_RX, payload.c_str(), true);
  mqtt.publish(TOPIC_LOG, (String("RX válido desde ") + sourceTag).c_str());

  // Mostrar JSON en Serial
  Serial.println("JSON RX (" + String(sourceTag) + "): " + payload);

  return true;
}

// ---------- MQTT ----------
void mqttCallback(char* topic, byte* msg, unsigned int len) {
  String payload; payload.reserve(len);
  for (unsigned int i=0;i<len;i++) payload += (char)msg[i];

  bool ok = processJSON(payload, "MQTT");
  if (ok) {
    BLE.println(payload);
    pulseLed(LED_VERDE);
    mqtt.publish(TOPIC_LOG, "TX a BLE desde MQTT");
  }
}

void ensureMqtt() {
  if (mqtt.connected()) return;
  String clientId = String("ESP32_") + TEAM_ID + "_" + String((uint32_t)ESP.getEfuseMac(), HEX);
  while (!mqtt.connected()) {
    if (mqtt.connect(clientId.c_str())) {
      mqtt.subscribe(TOPIC_EQ_RX);
      mqtt.publish(TOPIC_LOG, "MQTT conectado");
    } else { delay(500); }
  }
}

// ---------- Wi-Fi (reintentos espaciados) ----------
void ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) return;

  if (!wifiBegun) {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    if (WIFI_AUTO_RECONNECT) WiFi.setAutoReconnect(true);
    Serial.printf("Conectando a %s ...\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    wifiBegun = true;
    wifiLastAttempt = millis();
    return;
  }
  if (millis() - wifiLastAttempt > WIFI_RETRY_MS) {
    Serial.println("Reintentando WiFi...");
    WiFi.disconnect(true, true);
    delay(50);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    wifiLastAttempt = millis();
  }
}

// ---------- BLE (AT-09/HM-10): notificaciones por UART ----------
void handleBleStatusLines(const String& line) {
  String L = line; L.toUpperCase();
#if USE_BLE_STATE_PIN
  // Si usas STATE, solo logueamos (no cambiamos LEDs aquí).
  if (L.indexOf("OK+CONN") >= 0 || L.indexOf("STATE:CONNECTED") >= 0 || L.indexOf("CONNECTED") >= 0) {
    mqtt.publish(TOPIC_LOG, "BLE: (UART) CONN");
  } else if (L.indexOf("OK+LOST") >= 0 || L.indexOf("STATE:DISCONNECTED") >= 0 || L.indexOf("DISCONN") >= 0 || L.indexOf("DISCONNECTED") >= 0) {
    mqtt.publish(TOPIC_LOG, "BLE: (UART) LOST");
  }
#else
  // Si NO usas STATE, sí actualizamos estado/LEDs con estas cadenas:
  if (L.indexOf("OK+CONN") >= 0 || L.indexOf("STATE:CONNECTED") >= 0 || L.indexOf("CONNECTED") >= 0) {
    bleConnected = true;  lastBleActivity = millis();
    setBlue(true); setRed(false);
    mqtt.publish(TOPIC_LOG, "BLE: CONN");
    state = State::RUNNING;
  } else if (L.indexOf("OK+LOST") >= 0 || L.indexOf("STATE:DISCONNECTED") >= 0 || L.indexOf("DISCONN") >= 0 || L.indexOf("DISCONNECTED") >= 0) {
    bleConnected = false;
    setBlue(false); setRed(true);
    mqtt.publish(TOPIC_LOG, "BLE: LOST");
    state = State::BLE_DISCONNECTED;
  }
#endif
}

// ---------- Envío desde Serial (consola) ----------
void handleConsoleLine(String line) {
  line.trim(); if (line.length()==0) return;
  String accion = (line.equalsIgnoreCase("ON")) ? "ON" : (line.equalsIgnoreCase("OFF")) ? "OFF" : "";
  String jsonToSend;

  if (accion.length()) {
    jsonToSend = buildJSON("Consola serial", accion.c_str());
    // También refleja en el LED violeta
    if (strcasecmp(accion.c_str(), "ON") == 0) digitalWrite(LED_VIOLETA, HIGH);
    else                                       digitalWrite(LED_VIOLETA, LOW);
  } else {
    StaticJsonDocument<256> doc;
    DeserializationError e = deserializeJson(doc, line);
    if (!e) {
      doc["id"]     = TEAM_ID;
      doc["origen"] = doc["origen"] | "Consola serial";
      doc["accion"] = doc["accion"] | "OFF";
      char f[11], h[9]; getDateTime(f,h);
      doc["fecha"]  = doc["fecha"] | f;
      doc["hora"]   = doc["hora"]  | h;
      serializeJson(doc, jsonToSend);
    } else { Serial.println(F("Comando no válido. Escribe ON u OFF, o pega un JSON.")); return; }
  }

  BLE.println(jsonToSend);
  if (mqtt.connected()) {
    mqtt.publish(TOPIC_TX, jsonToSend.c_str(), true);
    mqtt.publish(TOPIC_LOG, "TX desde consola -> BLE & MQTT");
  }
  pulseLed(LED_VERDE);
  Serial.println("JSON TX (Consola): " + jsonToSend);
}

// ---------- Procesar línea recibida por BLE ----------
void handleBleLine(String line) {
  line.trim(); if (!line.length()) return;
  Serial.println("BLE RX raw: " + line);

  lastBleActivity = millis();
  handleBleStatusLines(line);

#if !USE_BLE_STATE_PIN
  // Si NO usamos STATE, podemos asumir conexión por actividad
  if (!bleConnected) {
    bleConnected = true;
    setBlue(true);
    setRed(false);
    state = State::RUNNING;
    mqtt.publish(TOPIC_LOG, "BLE: ACTIVITY -> asumido conectado");
  }
#endif

  // Aceptar "ON"/"OFF" simples
  if (line.equalsIgnoreCase("ON") || line.equalsIgnoreCase("OFF")) {
    String json = buildJSON("Smartphone", line.c_str());
    BLE.println(json);                 // eco opcional
    Serial.println("JSON RX (BLE): " + json);
    processJSON(json, "BLE/Smartphone");
    return;
  }

  // Si parece JSON, procesarlo
  if (line.indexOf('{') >= 0) {
    Serial.println("JSON RX (BLE raw): " + line);
    if (processJSON(line, "BLE/Smartphone")) {
      BLE.println(line);              // eco opcional del JSON válido
    }
  }
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);

  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_AMARILLO, OUTPUT);
  pinMode(LED_ROJO, OUTPUT);
  pinMode(LED_AZUL, OUTPUT);
  pinMode(LED_BLANCO, OUTPUT);
  pinMode(LED_NARANJA, OUTPUT);
  pinMode(LED_VIOLETA, OUTPUT);
  allLedsOff();
  digitalWrite(LED_VIOLETA, LOW);

#if USE_BLE_STATE_PIN
  pinMode(BLE_STATE_PIN, INPUT_PULLDOWN);   // Estado HIGH conectado, LOW desconectado
#endif

  WiFi.onEvent([](WiFiEvent_t e, WiFiEventInfo_t info){
    switch (e) {
      case ARDUINO_EVENT_WIFI_STA_START:         Serial.println("WiFi: STA_START"); break;
      case ARDUINO_EVENT_WIFI_STA_CONNECTED:     Serial.println("WiFi: CONNECTED"); break;
      case ARDUINO_EVENT_WIFI_STA_GOT_IP:        Serial.print("WiFi: IP "); Serial.println(WiFi.localIP()); break;
      case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        Serial.printf("WiFi: DISCONNECTED, reason=%d\n", info.wifi_sta_disconnected.reason);
        state = State::WIFI_CONNECTING;
        digitalWrite(LED_BLANCO, LOW);
        mqtt.disconnect();
        break;
      default: break;
    }
  });

  BLE.begin(BLE_BAUD, SERIAL_8N1, BLE_RX_PIN, BLE_TX_PIN);

  ensureWifi();
  configTime(GMT_OFFSET, DST_OFFSET, NTP_SERVER1);

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);

  // Estado inicial
  state = State::WIFI_CONNECTING;
  setRed(true);   // BLE no emparejado por defecto
}

// ---------- Loop principal ----------
void loop() {
  uint32_t now = millis();

  // Parpadeo blanco (WiFi buscando)
  if (now - lastBlink >= BLINK_HALF) {
    lastBlink = now;
    blinkFlag = !blinkFlag;

    if (state == State::WIFI_CONNECTING) {
      digitalWrite(LED_BLANCO, blinkFlag ? HIGH : LOW);
    }
  }
  updatePulse();

  // ====== Estado Wi-Fi ======
  if (state == State::WIFI_CONNECTING) {
    if (WiFi.status() != WL_CONNECTED) {
      ensureWifi();
    } else {
      digitalWrite(LED_BLANCO, HIGH);    // WiFi estable
      state = State::MQTT_CONNECTING;
    }
  }

  // ====== Estado MQTT ======
  if (state == State::MQTT_CONNECTING) {
    ensureMqtt();
    if (mqtt.connected()) {
      state = State::BLE_DISCONNECTED;
      bleConnected = false;
      setRed(true);
      setBlue(false);
    }
  }

  // ====== Servicio MQTT ======
  if (WiFi.status() == WL_CONNECTED) {
    if (!mqtt.connected()) ensureMqtt();
    mqtt.loop();
  }

  // ====== Lectura de BLE (líneas) ======
  while (BLE.available()) {
    char c = (char)BLE.read();

    // Acumular SIEMPRE
    bleBuffer += c;

    // Determinar fin de línea:
    //  - por salto de línea (\n o \r)
    //  - o por cierre de JSON '}' (si antes apareció '{')
    bool lineComplete = (c == '\n' || c == '\r');
    if (!lineComplete && c == '}' && bleBuffer.indexOf('{') >= 0) {
      lineComplete = true;
    }

    if (lineComplete) {
      String line = bleBuffer;
      line.trim();
      bleBuffer = "";
      if (line.length()) handleBleLine(line);
    }
  }

  // ====== Lectura desde consola Serial (líneas) ======
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialBuffer.length()) { handleConsoleLine(serialBuffer); serialBuffer = ""; }
    } else serialBuffer += c;
  }

#if USE_BLE_STATE_PIN
  // ====== Lectura del pin STATE para fijar estado/LEDs ======
  int s = digitalRead(BLE_STATE_PIN);  // 1 = conectado, 0 = desconectado
  if (s && !bleConnected) {
    bleConnected = true;
    setBlue(true); setRed(false);
    state = State::RUNNING;
    mqtt.publish(TOPIC_LOG, "BLE: STATE=HIGH (conectado)");
  } else if (!s && bleConnected) {
    bleConnected = false;
    setBlue(false); setRed(true);
    state = State::BLE_DISCONNECTED;
    mqtt.publish(TOPIC_LOG, "BLE: STATE=LOW (desconectado)");
  }

  // Refrescar LEDs por estado (evita inconsistencias si algo cambió)
  if (state == State::BLE_DISCONNECTED) { setRed(true);  setBlue(false); }
  if (state == State::RUNNING)          { setBlue(true); setRed(false);  }
#endif
}
