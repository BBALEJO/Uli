# 🗺️ Arquitectura de Hardware - Proyecto Uli

En este documento se detalla la interconexión física de todas las placas, sensores y actuadores que componen a Uli. El sistema está diseñado de forma modular, separando la lógica central, el procesamiento visual y el control motriz.

## ⚡ Diagrama de Alimentación y Conexiones

![Diagrama de Conexiones de Uli](docs/diagrama_energia_uli.jpg)
*(El sistema utiliza un bus común de 5V y GND para alimentar modularmente todas las placas. Únicamente el micrófono I2S se alimenta del regulador de 3.3V del ESP32-S3).*

---

## 🔌 1. Comunicación Serial (UART)

El cerebro del sistema (ESP32-S3) actúa como maestro, coordinando a los otros dos microcontroladores mediante dos puertos seriales de hardware independientes.

### Cerebro (ESP32-S3) ↔ Visión (ESP32-CAM)
*   **Protocolo:** UART (Baudrate: `230400`)
*   **Propósito:** Envío del comando de captura ('S') y recepción de los bytes de la imagen JPEG.
*   **Conexiones:**
    *   S3 **Pin 17 (TX1)** ➜ CAM **Pin 3 (U0RXD)**
    *   S3 **Pin 18 (RX1)** ➜ CAM **Pin 1 (U0TXD)**
*   *Nota: Para programar el ESP32-CAM se requiere un módulo USB-TTL externo y puentear GND con IO0.*

### Cerebro (ESP32-S3) ↔ Cuerpo (Raspberry Pi Pico)
*   **Protocolo:** UART (Baudrate: `115200`)
*   **Propósito:** Envío de comandos de movimiento en texto (`HABLAR`, `QUIETO`, `PARPADEAR`) y validación de estado.
*   **Conexiones:**
    *   S3 **Pin 16 (TX2)** ➜ Pico **GP1 (RX)**
    *   S3 **Pin 15 (RX2)** ➜ Pico **GP0 (TX)**

---

## 🎧 2. Periféricos de Audio (I2S)

El manejo de voz se realiza directamente desde el ESP32-S3 utilizando el protocolo I2S para garantizar alta fidelidad de entrada y salida, minimizando la latencia.

### Micrófono (INMP441) - Entrada
*   S3 **Pin 42 (MIC_WS)** ➜ Mic **WS** (Word Select / L-R)
*   S3 **Pin 41 (MIC_SD)** ➜ Mic **SD** (Data)
*   S3 **Pin 40 (MIC_SCK)** ➜ Mic **SCK** (Clock)
*   *Ajuste físico:* El pin L/R del INMP441 debe ir conectado a GND (Canal Izquierdo).

### Amplificador de Bocina (MAX98357A) - Salida
*   S3 **Pin 9 (SPK_WS)** ➜ Amp **LRC** (Word Select)
*   S3 **Pin 11 (SPK_DIN)** ➜ Amp **DIN** (Data In)
*   S3 **Pin 10 (SPK_BCLK)** ➜ Amp **BCLK** (Bit Clock)

---

## 🤖 3. Control de Servomotores (I2C)

La Raspberry Pi Pico está dedicada exclusivamente al control de la animación. Envía los pulsos a una Servo Shield de 16 canales a través de I2C.

### Raspberry Pi Pico ↔ Adafruit Servo Shield (PCA9685)
*   **Protocolo:** I2C (Dirección `0x40`)
*   **Conexiones:**
    *   Pico **GP4** ➜ Shield **SDA**
    *   Pico **GP5** ➜ Shield **SCL**

### Asignación de Canales PWM en la Shield
*   **Canal 1:** Oreja Derecha
*   **Canal 2:** Oreja Izquierda
*   **Canal 4:** Párpados
*   **Canal 5:** Boca
*   **Canal 7:** Ojo Derecho
*   **Canal 8:** Ojo Izquierdo
*   **Canal 10:** Movimientos Laterales
*   **Canal 11:** Base