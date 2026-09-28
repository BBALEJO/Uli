#Proyecto Uli: Animatrónico Interactivo con IA y Sistemas Embebidos

<div align="center">

![Estado del Proyecto](https://img.shields.io/badge/Estado-En%20Desarrollo-blue)
![Python](https://img.shields.io/badge/Python-3.10%2B-blueviolet)
![C++](https://img.shields.io/badge/C++-Arduino-orange)
![Hardware](https://img.shields.io/badge/Microcontroladores-ESP32--S3%20%7C%20Raspberry%20Pi%20Pico-green)

Uli es un robot animatrónico interactivo diseñado para combinar visión artificial, procesamiento de lenguaje natural, síntesis de voz y control motriz preciso mediante múltiples microcontroladores.

</div>

---

##Arquitectura del Sistema

El sistema está desglosado en una arquitectura modular distribuida:

* **Unidad Central (Procesamiento y Visión):** Gestionada mediante Python, APIs de IA, reconocimiento de voz y procesamiento de imágenes a través de la cámara integrada.
* **Subsistema de Control de Movimiento (Animatrónica):** Control de servos y mecanismos de expresión facial mediante microcontroladores de alta eficiencia.
* **Subsistema de Audio:** Reproducción de respuestas de voz sintetizada en tiempo real.

---

##Componentes de Hardware

* **Microcontroladores:** 
  * `ESP32-S3` (Conectividad y procesamiento multimedia/IA).
  * `Raspberry Pi Pico` (Control en tiempo real de actuadores).
* **Cámara:** `ESP32-CAM` / Módulo OV5647 para visión artificial y detección de entorno.
* **Actuadores:** Servomotores controlados mediante shield de potencia dedicada.
* **Audio:** Módulo de audio dedicado para la interacción verbal de Uli.
* **Alimentación:** Sistema de gestión de energía y baterías adaptado para estabilidad de componentes lógicos y motores.

---

## Stack Tecnológico y Software

* **Lenguajes:** Python (Control general, scripts de integración e IA) y C++ (Firmware de microcontroladores).
* **Librerías y Protocolos:** 
  * Comunicación serial / I2C / UART entre microcontroladores.
  * APIs de Inteligencia Artificial para procesamiento conversacional.
  * Reconocimiento de voz y Text-to-Speech (TTS).

---

##Estructura del Repositorio

```text
Uli-Animatronic-Robot/
│
├── firmware/
│   ├── esp32_s3/          # Código fuente en C++ para el ESP32-S3
│   └── pico_control/      # Firmware para Raspberry Pi Pico y gestión de servos
│
├── software/
│   ├── vision/            # Scripts de procesamiento de imagen y visión artificial
│   ├── ai_integration/    # Conexión con LLMs y motores de voz (STT / TTS)
│   └── main_controller.py # Script central de orquestación en Python
│
├── hardware/
│   ├── schematics/        # Diagramas esquemáticos de conexiones y circuitería
│   └── 3d_models/         # Diseños mecánicos y piezas para impresión 3D
│
└── README.md
