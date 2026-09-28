#include <WiFi.h>
#include <WiFiClient.h>
#include <ESPmDNS.h>
#include <driver/i2s.h>

// ===================
// CONFIGURACIÓN WI-FI Y MDNS
// ===================
const char* ssid = "TEST";
const char* password = "testssss";
const char* hostName = "animatronico"; // Resolverá a http://animatronico.local

// Pines UART
#define RX1_PIN 18  // ESP32-CAM
#define TX1_PIN 17  
#define VELOCIDAD_CAM 230400 

#define TX2_PIN 16  // Raspberry Pi Pico
#define RX2_PIN 15
#define VELOCIDAD_PICO 115200

// Pines I2S (Audio)
#define MIC_WS     42
#define MIC_SD     41
#define MIC_SCK    40
#define MIC_PORT   I2S_NUM_0 

#define SPK_WS     9
#define SPK_DIN    11
#define SPK_BCLK   10
#define SPK_PORT   I2S_NUM_1 

#define SAMPLE_RATE 16000     
#define BUFFER_SIZE 512       

WiFiServer server(80);

// Variables Globales de Estado para el Dashboard
bool estado_mic = false;
bool estado_spk = false;
bool estado_pico = false;
bool estado_shield = false;
bool estado_cam = false;

volatile bool transmitirAudioWeb = false;
WiFiClient clienteAudio;

// Semaphore para proteger el acceso concurrente a la cámara/UART
SemaphoreHandle_t mutexUART = NULL;

// ==========================================
// CONTROL DE LÍMITE DE VELOCIDAD (PREVENCIÓN DE COLAPSOS)
// ==========================================
unsigned long ultimoComando = 0;
const unsigned long DELAY_SERVOS = 75; // Milisegundos entre comandos normales

// ==========================================
// CONFIGURACIÓN Y VALIDACIÓN DE HARDWARE
// ==========================================

void configurarMicrofono() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX), 
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,         
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,          
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,             
    .dma_buf_count = 8,                                   
    .dma_buf_len = 64,                                    
    .use_apll = false
  };
  i2s_pin_config_t pin_config = { .bck_io_num = MIC_SCK, .ws_io_num = MIC_WS, .data_out_num = I2S_PIN_NO_CHANGE, .data_in_num = MIC_SD };
  if (i2s_driver_install(MIC_PORT, &i2s_config, 0, NULL) == ESP_OK && i2s_set_pin(MIC_PORT, &pin_config) == ESP_OK) {
    i2s_start(MIC_PORT);
    estado_mic = true;
    Serial.println("[OK] Micrófono INMP441 iniciado.");
  } else { Serial.println("[ERROR] Falla en Micrófono."); }
}

void configurarBocina() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,         
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,          
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,             
    .dma_buf_count = 8,                                   
    .dma_buf_len = 64,                                    
    .use_apll = false
  };
  i2s_pin_config_t pin_config = { .bck_io_num = SPK_BCLK, .ws_io_num = SPK_WS, .data_out_num = SPK_DIN, .data_in_num = I2S_PIN_NO_CHANGE };
  if (i2s_driver_install(SPK_PORT, &i2s_config, 0, NULL) == ESP_OK && i2s_set_pin(SPK_PORT, &pin_config) == ESP_OK) {
    i2s_start(SPK_PORT);
    estado_spk = true;
    Serial.println("[OK] Bocina MAX98357A iniciada.");
  } else { Serial.println("[ERROR] Falla en Bocina."); }
}

void validarCamara() {
  Serial.println("[SISTEMA] Validando ESP32-CAM...");
  while(Serial1.available() > 0) { Serial1.read(); } 
  Serial1.write('S');
  Serial1.flush();
  
  unsigned long timeout = millis();
  while (Serial1.available() < 4) {
    if (millis() - timeout > 3000) {
      Serial.println("[ERROR] ESP32-CAM no responde.");
      return;
    }
  }
  
  uint32_t tamano_imagen = 0;
  Serial1.readBytes((uint8_t*)&tamano_imagen, 4);
  
  if (tamano_imagen > 0 && tamano_imagen < 150000) {
    estado_cam = true;
    Serial.println("[OK] ESP32-CAM conectada y operativa.");
    uint8_t basura[512];
    uint32_t leidos = 0;
    while(leidos < tamano_imagen) {
      if(Serial1.available()){
        size_t por_leer = std::min((size_t)(tamano_imagen - leidos), sizeof(basura));
        leidos += Serial1.readBytes(basura, por_leer);
      }
    }
  }
}

void loopMicrofono(void * pvParameters) {
  int16_t buffer_audio[BUFFER_SIZE];
  size_t bytes_leidos = 0;
  for(;;) {
    if (estado_mic) {
      i2s_read(MIC_PORT, &buffer_audio, sizeof(buffer_audio), &bytes_leidos, pdMS_TO_TICKS(10));
      if (bytes_leidos > 0 && transmitirAudioWeb) {
        if (clienteAudio && clienteAudio.connected()) {
          size_t enviados = clienteAudio.write((uint8_t*)buffer_audio, bytes_leidos);
          if (enviados == 0) {
            transmitirAudioWeb = false;
            clienteAudio.stop();
          }
        } else {
          transmitirAudioWeb = false;
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(2)); 
  }
}

void escribirEncabezadoWAV(WiFiClient &client) {
  uint8_t header[44] = {
    'R','I','F','F', 0xFF,0xFF,0xFF,0xFF, 'W','A','V','E', 'f','m','t',' ', 16,0,0,0, 1,0, 1,0,
    0x80,0x3E,0x00,0x00, 0x00,0x7D,0x00,0x00, 2,0, 16,0, 'd','a','t','a', 0xFF,0xFF,0xFF,0xFF
  };
  client.write(header, 44);
}

void enviarFotoUART(WiFiClient &client) {
  if(!estado_cam) { 
    client.println("HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\n\r\n"); 
    return; 
  }
  
  if (xSemaphoreTake(mutexUART, pdMS_TO_TICKS(2000)) == pdTRUE) {
    while(Serial1.available() > 0) { Serial1.read(); }
    Serial1.write('S');
    Serial1.flush();
    
    unsigned long timeout = millis();
    while (Serial1.available() < 4) { 
      if (millis() - timeout > 3000) { 
        client.println("HTTP/1.1 500 Internal Error\r\nConnection: close\r\n\r\n"); 
        xSemaphoreGive(mutexUART);
        return; 
      } 
    }
    
    uint32_t tamano_imagen = 0;
    Serial1.readBytes((uint8_t*)&tamano_imagen, 4);
    
    if (tamano_imagen == 0 || tamano_imagen > 200000) { 
      client.println("HTTP/1.1 500 Internal Error\r\nConnection: close\r\n\r\n"); 
      xSemaphoreGive(mutexUART);
      return; 
    }
    
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: image/jpeg");
    client.printf("Content-Length: %d\r\n", tamano_imagen);
    client.println("Connection: close\r\n");
    
    uint8_t buffer[2048]; 
    uint32_t bytes_leidos = 0;
    timeout = millis();
    
    while (bytes_leidos < tamano_imagen && client.connected()) {
      if (Serial1.available() > 0) {
        size_t leidos = Serial1.readBytes(buffer, std::min((size_t)(tamano_imagen - bytes_leidos), sizeof(buffer)));
        client.write(buffer, leidos);
        bytes_leidos += leidos;
        timeout = millis(); 
      }
      if (millis() - timeout > 3000) break;
    }
    xSemaphoreGive(mutexUART);
  } else {
    client.println("HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\n\r\n");
  }
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(VELOCIDAD_CAM, SERIAL_8N1, RX1_PIN, TX1_PIN);
  Serial2.begin(VELOCIDAD_PICO, SERIAL_8N1, RX2_PIN, TX2_PIN);
  
  mutexUART = xSemaphoreCreateMutex();

  Serial.println("\n[SISTEMA] Iniciando secuencias de validación...");
  
  Serial.println("[SISTEMA] Esperando Pico...");
  unsigned long t_pico = millis();
  while (millis() - t_pico < 4000) {
    if (Serial2.available() > 0) {
      String res = Serial2.readStringUntil('\n'); res.trim();
      if (res == "STATUS:PICO_READY,SERVOS_OK") { estado_pico = true; estado_shield = true; break; }
      if (res == "STATUS:PICO_READY,SERVOS_FAIL") { estado_pico = true; break; }
    }
  }

  validarCamara();
  configurarMicrofono();
  configurarBocina();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("\nConectando WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  
  Serial.println(String("\n[OK] IP Asignada: ") + WiFi.localIP().toString());

  if (MDNS.begin(hostName)) {
    Serial.printf("[OK] mDNS activo: http://%s.local\n", hostName);
  } else {
    Serial.println("[ERROR] Falló la activación de mDNS.");
  }
  
  server.begin();
  xTaskCreatePinnedToCore(loopMicrofono, "MicTask", 8192, NULL, 1, NULL, 0);
}

void loop() {
  WiFiClient client = server.available();
  if (client) {
    String currentLine = "";
    String peticion = "";
    bool isPost = false;
    int contentLength = 0;
    bool mantenerConexionAbierta = false;

    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        if (peticion.length() < 30) { peticion += c; }
        
        if (c == '\n') {
          if (currentLine.length() == 0) {
            
            if (peticion.indexOf("GET /audio.wav") >= 0) {
              if (transmitirAudioWeb && clienteAudio.connected()) { clienteAudio.stop(); }
              clienteAudio = client;
              clienteAudio.println("HTTP/1.1 200 OK\r\nContent-Type: audio/wav\r\nConnection: close\r\n\r\n");
              escribirEncabezadoWAV(clienteAudio);
              transmitirAudioWeb = true; 
              mantenerConexionAbierta = true; 
              break; 
            } 
            else if (peticion.indexOf("GET /camara.jpg") >= 0) {
              enviarFotoUART(client);
              break;
            }
            else if (peticion.indexOf("POST /comando") >= 0) {
              String comando = client.readString();
              comando.trim();
              
              // ==========================================
              // EXCEPCIÓN DE EMERGENCIA PARA "QUIETO"
              // ==========================================
              // Convierte el comando a minúsculas para evitar problemas si mandas "Quieto" o "QUIETO"
              comando.toLowerCase(); 
              
              // Si el comando NO contiene la palabra "quieto", aplica el delay normal
              if (comando.indexOf("quieto") == -1) {
                while (millis() - ultimoComando < DELAY_SERVOS) {
                  vTaskDelay(pdMS_TO_TICKS(5)); 
                }
              }
              // Si sí contiene "quieto", ignora el while por completo y pasa directo al Serial2
              
              Serial2.println(comando);
              ultimoComando = millis(); // Reinicia el temporizador
              
              client.println("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nComando OK");
              break;
            }
            else if (peticion.indexOf("POST /hablar") >= 0) {
              client.println("HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n");
              uint8_t buf_tts[512];
              int bytesLeidos = 0;
              unsigned long timeoutAudio = millis();
              while (bytesLeidos < contentLength && client.connected()) {
                if (client.available()) {
                  int aLeer = std::min(client.available(), (int)sizeof(buf_tts));
                  aLeer = std::min(aLeer, contentLength - bytesLeidos);
                  int leidosReal = client.read(buf_tts, aLeer);
                  if (estado_spk) {
                    size_t escritos;
                    i2s_write(SPK_PORT, buf_tts, leidosReal, &escritos, portMAX_DELAY);
                  }
                  bytesLeidos += leidosReal;
                  timeoutAudio = millis();
                }
                if (millis() - timeoutAudio > 2000) break;
              }
              break;
            }
            else {
              client.println("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n");
              client.print("<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Dashboard AI</title>");
              client.print("<style>body{font-family:Arial; background:#121212; color:white; text-align:center;}");
              client.print(".grid{display:grid; grid-template-columns:1fr 1fr; gap:10px; max-width:600px; margin:auto;}");
              client.print(".box{background:#1e1e1e; padding:15px; border-radius:8px;}");
              client.print("button{padding:10px 20px; font-weight:bold; cursor:pointer;}");
              client.print("</style></head><body>");
              
              client.print("<h1>🧠 Panel de Control Hardware</h1>");
              client.print("<div class='grid'>");
              
              client.print("<div class='box'><h3>Raspberry Pico</h3>" + String(estado_pico ? "🟢 CONECTADA" : "🔴 ERROR") + "</div>");
              client.print("<div class='box'><h3>Servo Shield</h3>" + String(estado_shield ? "🟢 OK" : "🔴 ERROR") + "</div>");
              client.print("<div class='box'><h3>Micrófono</h3>" + String(estado_mic ? "🟢 OK" : "🔴 ERROR") + "</div>");
              client.print("<div class='box'><h3>Bocina</h3>" + String(estado_spk ? "🟢 OK" : "🔴 ERROR") + "</div>");
              client.print("<div class='box'><h3>Cámara</h3>" + String(estado_cam ? "🟢 OK" : "🔴 ERROR") + "</div>");
              client.print("<div class='box'><h3>Cerebro S3</h3>🟢 OK</div>");
              
              client.print("</div><br><hr><br>");
              client.print("<h2>Test Visual</h2>");
              client.print("<img id='foto' src='' style='width:300px; border:2px solid #555;'><br><br>");
              client.print("<button onclick='document.getElementById(\"foto\").src=\"/camara.jpg?t=\"+new Date().getTime()'>Tomar Foto</button>");
              client.print("</body></html>");
              break;
            }
          } else { 
            if (currentLine.startsWith("POST")) isPost = true;
            if (isPost && currentLine.startsWith("Content-Length: ")) {
              contentLength = currentLine.substring(16).toInt();
            }
            currentLine = ""; 
          }
        } else if (c != '\r') { currentLine += c; }
      }
    }
    
    if (!mantenerConexionAbierta) { client.stop(); }
  }
}