[README.md](https://github.com/user-attachments/files/22106726/README.md)
# README – Fundamentos y aplicación del módulo Bluetooth AT-09 con ESP32 y MQTT

**Equipo:** 4 
**Integrantes:** Pedro Iván Palomino Viera, Luis Antonio Torres Padrón  

---

## 1. Objetivo General
Diseñar e implementar un gateway BLE↔WiFi con ESP32 + AT-09 que formatee mensajes en JSON y publique/suscriba por MQTT, incorporando indicadores LED y validación por ID para operación confiable en entornos inteligentes.

---

## 2. Objetivos Específicos
1. Configurar el módulo AT-09 en modo UART BLE y enlazarlo con ESP32.  
2. Establecer conectividad Wi-Fi y sincronizar hora por NTP en el ESP32.  
3. Integrar un cliente MQTT (publicación y suscripción) con tópicos del equipo.  
4. Construir y validar mensajes JSON con ArduinoJson, filtrando por ID de equipo.  
5. Implementar una máquina de estados no bloqueante para Wi-Fi/MQTT/BLE.  
6. Indicar el estado del sistema con LEDs (TX, RX válido/ inválido, Wi-Fi, BLE, acción).  
7. Documentar resultados de prueba en consola y broker, y buenas prácticas de seguridad.  

---

## 3. Competencias
- Integración BLE (AT-09) + ESP32 en sistemas IoT.  
- Diseño de protocolos de aplicación con JSON y MQTT.  
- Programación asíncrona con máquina de estados y gestión de reconexión.  
- Instrumentación y verificación con indicadores LED y logs.  
- Documentación técnica con estructura profesional del README.  

---

## 4. Tabla de Contenidos
1. Objetivo General  
2. Objetivos Específicos  
3. Competencias  
4. Tabla de Contenidos  
5. Descripción  
6. Requisitos  
7. Instalación y Configuración  
8. Conexiones de Hardware  
9. Parámetros Técnicos del AT-09  
10. Uso y ejemplos de Código  
11. Resultados de Prueba  
12. Consideraciones Éticas y de Seguridad  
13. Formato de Salida (JSON)  
14. Solución de Problemas  
15. Contribuciones  
16. Referencias  

---

## 5. Descripción
Esta práctica implementa un puente BLE↔Wi-Fi: el AT-09 recibe/entrega datos vía UART BLE con un smartphone (Serial Bluetooth Terminal), el ESP32 formatea y valida el contenido en JSON, y lo publica/suscribe en un broker MQTT (Mosquitto).  
Se añaden LEDs para telemetría visual (TX, RX válido/ inválido, estados Wi-Fi y BLE, acción ON/OFF) y una máquina de estados asegura operación no bloqueante y robusta.  

---

## 6. Requisitos

### Hardware necesario
- ESP32 DevKit  
- AT-09 / HM-10 (BLE UART)  
- 7 LEDs: verde, amarillo, rojo, azul, blanco, naranja, violeta (+ resistencias)  
- Protoboard y cables Dupont  
- Smartphone con **Serial Bluetooth Terminal** (Android)  
- Fuente 5 V/USB con GND común  

### Software y bibliotecas requeridas
- Arduino IDE  
- WiFi.h  
- PubSubClient.h  
- ArduinoJson.h  
- Broker MQTT público (`test.mosquitto.org`)  
- Aplicación BLE (Serial Bluetooth Terminal)  

### Conocimientos previos imprescindibles
- C/C++ en Arduino  
- Wi-Fi y MQTT  
- BLE básico  
- JSON  

---

## 7. Instalación y Configuración
1. Clona/descarga el repositorio del proyecto.  
2. Abre el sketch en **Arduino IDE** e instala las bibliotecas requeridas.  
3. Configura credenciales Wi-Fi, host/puerto MQTT, `TEAM_ID` y tópicos.  
4. Configura pines BLE:  
   - GPIO27 (TX→AT-09 RX)  
   - GPIO26 (RX←AT-09 TX)  
   - GPIO14 (STATE)  
5. Carga en el ESP32 y abre la consola a **115200 baud**.  
6. Empareja el AT-09 con el smartphone y prueba con **Serial Bluetooth Terminal**.  

---

## 8. Conexiones de Hardware

| Señal del módulo | Pin ESP32 | Función |
|------------------|-----------|---------|
| AT-09 VCC        | 3.3 V     | Alimentación |
| AT-09 GND        | GND       | Tierra común |
| AT-09 TXD        | GPIO26    | UART → ESP32 |
| AT-09 RXD        | GPIO27    | UART ← ESP32 |
| AT-09 STATE      | GPIO14    | Conectado=HIGH / Desconectado=LOW |
| LED VERDE        | GPIO4     | TX (pulso 1s) |
| LED AMARILLO     | GPIO16    | RX válido (pulso 1s) |
| LED ROJO         | GPIO17    | BLE no emparejado |
| LED AZUL         | GPIO5     | BLE emparejado |
| LED BLANCO       | GPIO18    | Wi-Fi buscando/estable |
| LED NARANJA      | GPIO19    | ID inválido (pulso 1s) |
| LED VIOLETA      | GPIO23    | Acción ON/OFF |

---

## 9. Parámetros Técnicos del AT-09

| Parámetro            | Valor típico | Unidad |
|----------------------|--------------|--------|
| Voltaje de operación | 3.3          | V      |
| Interfaz             | UART (BLE 4.x) | -    |
| Velocidad UART       | 9600         | baudios |
| Banda RF             | 2.4          | GHz   |

---

## 10. Uso y ejemplos de Código
- **Consola serial:** escribir `ON`/`OFF` → genera JSON, lo envía por BLE y MQTT, enciende LED violeta, pulsa verde.  
- **Desde celular:** enviar `ON`/`OFF` o JSON válido → valida, pulsa amarillo, publica MQTT, refleja LED violeta.  
- **ID inválido:** pulsa naranja 1s y genera un log en el tópico.  

---

## 11. Resultados de Prueba
Se observó funcionamiento correcto:  
- Consola imprime TX/RX JSON y eventos de estado.  
- Broker MQTT recibe publicaciones en `PIPV_LATP_TX`, `PIPV_LATP_RX`, `PIPV_LATP_LOG`.  
- LEDs encienden según la condición esperada.  

---

## 12. Consideraciones Éticas y de Seguridad
- No incluir datos personales en mensajes JSON.  
- Evitar redes Wi-Fi públicas, usar TLS en producción.  
- No versionar credenciales reales, usar archivos seguros.  
- Validar campos `id` y formato JSON para evitar inyección.  
- Uso educativo, responsable y ético del sistema.  

---

## 13. Formato de Salida (JSON)

Ejemplo de mensaje:

```json
{
  "id": "PIPV_LATP",
  "origen": "Consola serial | Smartphone",
  "accion": "ON | OFF",
  "fecha": "DD-MM-AAAA",
  "hora": "HH:MM:SS"
}
```

| Campo   | Tipo de dato | Descripción |
|---------|--------------|-------------|
| id      | String       | Identificador único del equipo. Debe coincidir con el `TEAM_ID` configurado. |
| origen  | String       | Fuente que generó el mensaje: `"Consola serial"` o `"Smartphone"`. |
| accion  | String       | Acción solicitada: `"ON"` enciende el LED violeta, `"OFF"` lo apaga. |
| fecha   | String       | Fecha del evento en formato `DD-MM-AAAA`, obtenida desde NTP. |
| hora    | String       | Hora del evento en formato `HH:MM:SS`, obtenida desde NTP. |

---

## 14. Solución de Problemas

| Problema              | Causa probable     | Solución recomendada |
|------------------------|-------------------|-----------------------|
| No conecta Wi-Fi       | SSID/clave incorrectos | Verificar credenciales y señal |
| No conecta a MQTT      | Host/puerto inválidos  | Revisar parámetros MQTT y conectividad |
| ID inválido (naranja)  | Campo `id` distinto    | Corregir JSON enviado |
| LED azul no enciende   | STATE no cableado      | Conectar STATE a GPIO14 y GND común |
| No llegan mensajes BLE | TX/RX invertidos       | Revisar conexiones UART |
| LED violeta no cambia  | Campo `accion` ausente | Enviar JSON con `accion: ON/OFF` |
| JSON inválido          | Sintaxis incorrecta    | Verificar formato de llaves y comillas |

---

## 15. Contribuciones
1. Haz fork del repositorio.  
2. Crea una rama `feature/mi-mejora`.  
3. Sigue convenciones de commits claros.  
4. Abre un Pull Request describiendo cambios y pruebas.  

---

## 16. Referencias
- Guía de práctica BLE (PDF Universidad de Colima).  
- Ejemplo de README previo (RFID + ESP32).  
- Espressif Systems. *ESP32 Arduino Core Documentation*.  
- OASIS. *MQTT v3.1.1 Specification*.  
- Blanchon, B. *ArduinoJson Documentation*.  
- Bluetooth SIG. *Bluetooth Core Specification*.  
- Jinou Electronics. *AT-09 BLE datasheet*.  

---
