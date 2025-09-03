# Fundamentos-y-aplicacion-del-modulo-Bluetooth-AT09
Esta práctica implementa un puente BLE↔Wi-Fi: el AT-09 recibe/entrega datos vía UART BLE con un smartphone (Serial Bluetooth Terminal), el ESP32 formatea y valida el contenido en JSON, y lo publica/suscribe en un broker MQTT (Mosquitto). Se añaden LEDs para telemetría visual (TX, RX válido/ inválido, estados Wi-Fi y BLE, acción ON/OFF).
