//RECORDAR PARA PROGRAMAR LA CAM SE DEBE USAR UN CONTROLADOR APARTE PUENTIAR MEMORIA

#include "esp_camera.h"
#include <HardwareSerial.h>

// ===================================
// DEFINICIÓN DE PINES (Modelo AI-Thinker)
// ===================================
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// Velocidad de comunicación UART entre ambas placas
#define VELOCIDAD_SERIAL 230400 

void iniciarCamara() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  
  // 10MHz mantiene el chip frío y evita ruido térmico en el sensor
  config.xclk_freq_hz = 10000000; 
  
  config.pixel_format = PIXFORMAT_JPEG; 
  config.frame_size = FRAMESIZE_SVGA;  // Resolución: 800x600
  config.jpeg_quality = 12;            // Calidad (0-63)
  config.fb_count = 1;

  // Inicializar la estructura de la cámara
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    // Si falla el hardware de la cámara, se detiene aquí
    while (1) {
      delay(1000);
    }
  }
}

void setup() {
  // Usamos el puerto Serial nativo (pines 1 y 3) para comunicar con el S3
  Serial.begin(VELOCIDAD_SERIAL);
  iniciarCamara();
}

void loop() {
  // Esperar a que el ESP32-S3 envíe el carácter 'S' (Snapshot)
  if (Serial.available() > 0) {
    char comando = Serial.read();
    
    if (comando == 'S') {
      // 1. Capturar la imagen en este instante exacto
      camera_fb_t * fb = esp_camera_fb_get();
      
      if (!fb) {
        // Si falla la captura, enviamos un tamaño de 0 bytes como señal de error
        uint32_t error_size = 0;
        Serial.write((uint8_t*)&error_size, 4);
        return;
      }

      // 2. Limpiar residuos previos en el buffer de transmisión
      Serial.flush(); 

      // 3. Enviar el tamaño de la imagen ocupando exactamente 4 bytes (uint32_t)
      uint32_t tamano_imagen = fb->len;
      Serial.write((uint8_t*)&tamano_imagen, 4);
      
      // Forzar salida de los 4 bytes y dar un respiro al buffer del S3
      Serial.flush(); 
      delay(5); 

      // 4. Enviar el paquete binario completo del JPEG
      Serial.write(fb->buf, fb->len);
      Serial.flush();
      
      // 5. Liberar el buffer de la cámara para apagar el sensor y enfriar
      esp_camera_fb_return(fb);
    }
  }
}