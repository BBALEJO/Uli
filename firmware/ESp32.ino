#include <WiFi.h>
#include <WiFiClient.h>
#include <ESPmDNS.h>
#include <driver/i2s.h>
#include <WiFiUdp.h>
#include <math.h>

// ===================
// CONFIGURACIÓN WI-FI Y RED
// ===================
const char* ssid = "TEST";
const char* password = "testssss";
const char* hostName = "animatronico"; 
#define CAM_HOST "uli-cam.local"       

// Variables de auto-aprendizaje de IP para UDP
IPAddress ip_python;
bool ip_aprendida = false;
const int PUERTO_UDP = 5005;
WiFiUDP udpAudio;

// Pines UART (Raspberry Pi Pico)
#define TX2_PIN 16
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

// Variables Globales
bool estado_mic = false;
bool estado_spk = false;
bool estado_pico = false;
bool estado_shield = false;

unsigned long ultimoComando = 0;
const unsigned long DELAY_SERVOS = 75; 

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
    .dma_buf_count = 16,
    .dma_buf_len = 256,
    .use_apll = false,
    .tx_desc_auto_clear = true
  };
  i2s_pin_config_t pin_config = { .bck_io_num = SPK_BCLK, .ws_io_num = SPK_WS, .data_out_num = SPK_DIN, .data_in_num = I2S_PIN_NO_CHANGE };
  if (i2s_driver_install(SPK_PORT, &i2s_config, 0, NULL) == ESP_OK && i2s_set_pin(SPK_PORT, &pin_config) == ESP_OK) {
    i2s_start(SPK_PORT);
    estado_spk = true;
    Serial.println("[OK] Bocina MAX98357A iniciada.");
  } else { Serial.println("[ERROR] Falla en Bocina."); }
}

// ==========================================
// SINTETIZADOR DE ARRANQUE I2S
// ==========================================
void reproducirTono(float frecuencia, int duracion_ms) {
  if (!estado_spk) return;
  int muestras = (SAMPLE_RATE * duracion_ms) / 1000;
  int16_t buffer[64];
  float fase = 0.0;
  float paso = (2.0 * PI * frecuencia) / SAMPLE_RATE;
  
  for (int i = 0; i < muestras; i += 64) {
    int a_escribir = std::min(64, muestras - i);
    for (int j = 0; j < a_escribir; j++) {
      buffer[j] = (int16_t)(8000.0 * sin(fase)); 
      fase += paso;
      if (fase > 2.0 * PI) fase -= 2.0 * PI;
    }
    size_t escritos;
    i2s_write(SPK_PORT, buffer, a_escribir * sizeof(int16_t), &escritos, portMAX_DELAY);
  }
}

void reproducirSonidoArranque() {
  Serial.println("[AUDIO] Reproduciendo sonido de arranque...");
  reproducirTono(523.25, 150); 
  delay(30);
  reproducirTono(659.25, 150); 
  delay(30);
  reproducirTono(783.99, 300); 
  delay(100);
  reproducirTono(1046.50, 400); 
  
  if (estado_spk) {
    uint8_t silencio[1024] = {0};
    size_t escritos;
    for (int i = 0; i < 4; i++) {
      i2s_write(SPK_PORT, silencio, sizeof(silencio), &escritos, portMAX_DELAY);
    }
    i2s_zero_dma_buffer(SPK_PORT); 
  }
}

// ==========================================
// TAREA VAD (FILTRO LOCAL) Y UDP EN CORE 0
// ==========================================
void loopMicrofono(void * pvParameters) {
  int16_t buffer_audio[BUFFER_SIZE];
  size_t bytes_leidos = 0;
  for(;;) {
    if (estado_mic) {
      i2s_read(MIC_PORT, &buffer_audio, sizeof(buffer_audio), &bytes_leidos, pdMS_TO_TICKS(10));
      
      if (bytes_leidos > 0) {
        long suma_energia = 0;
        int muestras = bytes_leidos / 2;
        
        for (int i = 0; i < muestras; i++) {
          suma_energia += abs(buffer_audio[i]);
        }
        int volumen_promedio = suma_energia / muestras;

        if (volumen_promedio > 200 && ip_aprendida) {
          udpAudio.beginPacket(ip_python, PUERTO_UDP);
          udpAudio.write((uint8_t*)buffer_audio, bytes_leidos);
          udpAudio.endPacket();
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

String leerBody(WiFiClient &client, int contentLength, unsigned long timeoutMs = 500) {
  String body;
  if (contentLength <= 0) return body;
  body.reserve(contentLength);
  unsigned long t = millis();
  while ((int)body.length() < contentLength && client.connected() && millis() - t < timeoutMs) {
    while (client.available() && (int)body.length() < contentLength) {
      body += (char)client.read();
      t = millis();
    }
    delay(1);
  }
  return body;
}

// ==========================================
// RECIBIR PCM POR TCP Y REPRODUCIRLO (/hablar)
// ==========================================
void manejarHablar(WiFiClient &client, int contentLength) {
  static uint8_t buf_tts[2048];
  static uint8_t silencio[1024] = {0};
  size_t recibido = 0;
  bool hayPendiente = false;     
  uint8_t pendiente = 0;
  unsigned long t = millis();

  // Le avisa a la Pico que empiece la animación de hablar
  Serial2.println("HABLAR");

  while (millis() - t < 10000) {
    if (contentLength > 0 && recibido >= (size_t)contentLength) break;

    int disp = client.available();
    if (disp > 0) {
      int off = 0;
      if (hayPendiente) { buf_tts[0] = pendiente; off = 1; hayPendiente = false; }

      int maxLeer = std::min(disp, (int)sizeof(buf_tts) - off);
      int n = client.read(buf_tts + off, maxLeer);
      if (n <= 0) continue;
      recibido += n;

      int total = n + off;
      if (total & 1) {           
        pendiente = buf_tts[total - 1];
        hayPendiente = true;
        total--;
      }
      if (estado_spk && total > 0) {
        size_t escritos;
        i2s_write(SPK_PORT, buf_tts, total, &escritos, portMAX_DELAY);
      }
      t = millis();
    } else if (!client.connected()) {
      break;                     
    } else {
      vTaskDelay(pdMS_TO_TICKS(2)); // Evita reinicios por Watchdog (WDT)
    }
  }

  // Vaciar el DMA con silencio para que salgan las últimas muestras
  if (estado_spk) {
    size_t escritos;
    for (int i = 0; i < 16; i++) {
      i2s_write(SPK_PORT, silencio, sizeof(silencio), &escritos, portMAX_DELAY);
    }
  }

  // Responder a Python que terminó la transferencia
  client.print("HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n");

  // Ordena a la Pico detener el movimiento de hablar y volver a la normalidad
  Serial2.println("QUIETO");
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(VELOCIDAD_PICO, SERIAL_8N1, RX2_PIN, TX2_PIN);

  Serial.println("\n[SISTEMA] Iniciando secuencias de validación...");
  Serial.println("[SISTEMA] Esperando Pico...");
  unsigned long t_pico = millis();
  while (millis() - t_pico < 6000) {
    if (Serial2.available() > 0) {
      String res = Serial2.readStringUntil('\n'); res.trim();
      if (res == "STATUS:PICO_READY,SERVOS_OK") { estado_pico = true; estado_shield = true; break; }
      if (res == "STATUS:PICO_READY,SERVOS_FAIL") { estado_pico = true; break; }
    }
  }
  if (!estado_pico) Serial.println("[ERROR] La Pico no responde.");

  configurarMicrofono();
  configurarBocina();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("\nConectando WiFi");
  unsigned long t_wifi = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - t_wifi > 30000) {
      Serial.println("\n[ERROR] WiFi no conecta. Reiniciando...");
      ESP.restart();
    }
  }

  WiFi.setSleep(false);

  Serial.println(String("\n[OK] IP Asignada: ") + WiFi.localIP().toString());
  if (MDNS.begin(hostName)) {
    Serial.printf("[OK] mDNS activo: http://%s.local\n", hostName);
  }

  reproducirSonidoArranque();

  server.begin();
  xTaskCreatePinnedToCore(loopMicrofono, "MicTask", 8192, NULL, 1, NULL, 0);
}

// ==========================================
// CEREBRO EN CORE 1 (SOLO INTERFAZ Y COMANDOS TCP)
// ==========================================
void loop() {
  WiFiClient client = server.available();
  if (client) {
    ip_python = client.remoteIP();
    ip_aprendida = true;

    String currentLine = "";
    String peticion = "";
    int contentLength = 0;

    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        if (peticion.length() < 30) { peticion += c; }

        if (c == '\n') {
          if (currentLine.length() == 0) {

            if (peticion.indexOf("POST /comando") >= 0) {
              String comando = leerBody(client, contentLength);
              comando.trim();
              if (comando.length() > 0) {
                String cmdMin = comando;
                cmdMin.toLowerCase();
                if (cmdMin.indexOf("quieto") == -1) {
                  while (millis() - ultimoComando < DELAY_SERVOS) {
                    vTaskDelay(pdMS_TO_TICKS(5));
                  }
                }
                Serial2.println(comando);
                ultimoComando = millis();
              }
              client.println("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nComando OK");
              break;
            }
            else if (peticion.indexOf("POST /hablar") >= 0) {
              manejarHablar(client, contentLength);
              break;
            }
            else {
              client.println("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n");
              client.print("<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Dashboard AI</title>");
              client.print("<style>body{font-family:Arial; background:#121212; color:white; text-align:center;}");
              client.print(".grid{display:grid; grid-template-columns:1fr 1fr; gap:10px; max-width:600px; margin:auto;}");
              client.print(".box{background:#1e1e1e; padding:15px; border-radius:8px;}");
              client.print("a{color:#00ffcc;} button{padding:10px 20px; font-weight:bold; cursor:pointer;}");
              client.print("</style></head><body>");
              client.print("<h1>🧠 Panel de Control Hardware (UDP Auto-IP)</h1>");
              client.print("<div class='grid'>");
              client.print("<div class='box'><h3>Raspberry Pico</h3>" + String(estado_pico ? "🟢 CONECTADA" : "🔴 ERROR") + "</div>");
              client.print("<div class='box'><h3>Servo Shield</h3>" + String(estado_shield ? "🟢 OK" : "🔴 ERROR") + "</div>");
              client.print("<div class='box'><h3>Micrófono</h3>" + String(estado_mic ? "🟢 OK (Filtro UDP Activo)" : "🔴 ERROR") + "</div>");
              client.print("<div class='box'><h3>IP Python</h3>" + String(ip_aprendida ? ip_python.toString() : "🔴 ESPERANDO PING") + "</div>");
              client.print("<div class='box'><h3>Cámara</h3><a href='http://" CAM_HOST "'>" CAM_HOST "</a></div>");
              client.print("<div class='box'><h3>Cerebro S3</h3>🟢 OK</div>");
              client.print("</div><br><hr><br>");
              client.print("<h2>Test Visual</h2>");
              client.print("<img id='foto' src='' style='width:300px; border:2px solid #555;'><br><br>");
              client.print("<button onclick='document.getElementById(\"foto\").src=\"http://" CAM_HOST "/camara.jpg?t=\"+new Date().getTime()'>Tomar Foto</button>");
              client.print("</body></html>");
              break;
            }
          } else {
            String low = currentLine;
            low.toLowerCase();
            if (low.startsWith("content-length:")) {
              contentLength = low.substring(15).toInt();
            }
            currentLine = "";
          }
        } else if (c != '\r') { currentLine += c; }
      }
    }
    client.stop();
  }
}
