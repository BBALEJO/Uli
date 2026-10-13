// =====================================================================
// ESP32-CAM (AI-Thinker) - Proyecto Uli - Version 2.0.0
//
// Hace dos cosas:
//   1. Detecta caras y manda "FACE x y" (x,y en % del cuadro, 0-100) por UART0 a la Pico
//      (CAM TX / GPIO1 -> Pico GP9). Manda "NOFACE" cuando la pierde.
//   2. Sirve la foto por WiFi:  http://uli-cam.local/camara.jpg  (para Python / OpenAI)
//   3. Video en vivo (MJPEG):   http://uli-cam.local:81/stream   (abrelo en el navegador)
//
// Para PROGRAMARLA: USB-TTL externo + puente GND con IO0 (y desconecta el cable a la Pico).
//
// Configuracion de Arduino IDE (importante):
//   - Placa: "AI Thinker ESP32-CAM"
//   - Core esp32 de Espressif version 2.0.17  (sirve tambien 3.0.x hasta 3.0.7).
//     Desde la 3.1.0 Espressif quito la deteccion de caras (ESP-DL), asi que ahi NO hay deteccion.
//   - PSRAM: Enabled  (con "ESP32 Wrover Module" ya viene activada)
//   - Partition Scheme: "Huge APP (3MB No OTA/1MB SPIFFS)"
//
// Si falta el core correcto o la PSRAM, el codigo compila igual pero SIN deteccion
// (solo foto y video), el LED rojo parpadea 3 veces al arrancar y http://uli-cam.local/ dice el motivo.
// =====================================================================

#include "esp_camera.h"
#include "img_converters.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

// Deteccion de caras en la propia CAM con ESP-DL (el mismo detector del ejemplo oficial CameraWebServer)
#if defined(BOARD_HAS_PSRAM) && __has_include("human_face_detect_msr01.hpp")
  #include <list>
  #include <vector>
  #include "human_face_detect_msr01.hpp"
  #define HAS_FACE_DETECT 1
#else
  #define HAS_FACE_DETECT 0
#endif

// ===================================
// AJUSTES
// ===================================
const char* WIFI_SSID = "TEST";        // la misma red del S3 y del PC
const char* WIFI_PASS = "testssss";
const char* HOSTNAME  = "uli-cam";     // -> http://uli-cam.local

#define BAUD_PICO 115200               // debe coincidir con CAM_BAUD de la Pico
#define LED_ROJO  33                   // LED rojo de la placa (se enciende con LOW)

// Deteccion en baja resolucion (rapida). La foto se toma en alta y se vuelve a bajar.
const framesize_t TAM_DETECCION = FRAMESIZE_VGA;   // 320x240
const framesize_t TAM_FOTO      = FRAMESIZE_VGA;    // 640x480

const unsigned long NOFACE_DESPUES_MS = 500;        // sin cara este tiempo -> manda NOFACE

// ===================================
// PINES (Modelo AI-Thinker)
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

WebServer server(80);
bool webListo = false;
bool hayCara = false;
unsigned long ultimaCara = 0;

#if HAS_FACE_DETECT
static HumanFaceDetectMSR01 *detector = nullptr;
#endif

// ===================================
// STREAM DE VIDEO (MJPEG en el puerto 81): http://uli-cam.local:81/stream
// Reutiliza los mismos frames de la deteccion, asi que no compite por la camara.
// FPS aprox: 2-4 con deteccion activa. Pon STREAM_PAUSA_DETECCION = true para ~10 FPS
// (pero mientras haya alguien viendo el stream, el cuello NO sigue caras).
// ===================================
const bool STREAM_PAUSA_DETECCION = true;
WiFiServer streamServer(81);
WiFiClient clienteStream;
bool streamActivo = false;

void enviarFrameStream(camera_fb_t *fb) {
  if (!streamActivo) return;
  if (!clienteStream.connected()) { streamActivo = false; return; }
  clienteStream.printf("--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", (unsigned)fb->len);
  size_t n = clienteStream.write(fb->buf, fb->len);
  clienteStream.print("\r\n");
  if (n != fb->len) { clienteStream.stop(); streamActivo = false; }
}

void atenderStream() {
  WiFiClient nuevo = streamServer.available();
  if (!nuevo) return;
  delay(10);
  while (nuevo.available()) nuevo.read();    // descarta la peticion HTTP
  if (streamActivo) clienteStream.stop();    // un solo espectador a la vez
  clienteStream = nuevo;
  clienteStream.print("HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace; boundary=frame\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n");
  streamActivo = true;
}

// ===================================
// UTILIDADES
// ===================================
void ledRojo(bool encendido) { digitalWrite(LED_ROJO, encendido ? LOW : HIGH); }

void parpadearLed(int veces, int ms) {
  for (int i = 0; i < veces; i++) {
    ledRojo(true);  delay(ms);
    ledRojo(false); delay(ms);
  }
}

void descartarFrames(int n) {
  for (int i = 0; i < n; i++) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);
  }
}

// ===================================
// CAMARA
// ===================================
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

  config.xclk_freq_hz = 10000000;          // 10MHz: chip frio (sube a 20MHz si quieres mas FPS)
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = TAM_DETECCION;
  config.jpeg_quality = 12;

  if (psramFound()) {
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_LATEST;  // siempre el frame mas reciente
  } else {
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    // Falla de hardware: LED rojo parpadeando rapido para siempre
    while (true) parpadearLed(1, 100);
  }
}

// ===================================
// DETECCION DE CARAS
// ===================================
#if HAS_FACE_DETECT
// Devuelve true y la posicion (% del cuadro) de la cara mas grande
bool detectarCara(int &cx, int &cy) {
  if (!detector) return false;
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) return false;
  enviarFrameStream(fb);                     // si alguien ve el stream, recibe este mismo frame

  int w = fb->width, h = fb->height;
  uint8_t *rgb = (uint8_t *)malloc((size_t)w * h * 3);   // va a PSRAM (como en el ejemplo oficial)
  if (!rgb) { esp_camera_fb_return(fb); return false; }

  bool ok = fmt2rgb888(fb->buf, fb->len, fb->format, rgb);
  esp_camera_fb_return(fb);                  // el frame ya no hace falta
  if (!ok) { free(rgb); return false; }

  std::list<dl::detect::result_t> &caras = detector->infer(rgb, {h, w, 3});

  bool hay = false;
  long mejorArea = 0;
  for (auto &c : caras) {
    int x1 = c.box[0], y1 = c.box[1], x2 = c.box[2], y2 = c.box[3];
    long area = (long)(x2 - x1) * (y2 - y1);
    if (area > mejorArea) {
      mejorArea = area;
      cx = constrain((100 * (x1 + x2) / 2) / w, 0, 100);
      cy = constrain((100 * (y1 + y2) / 2) / h, 0, 100);
      hay = true;
    }
  }

  free(rgb);
  return hay;
}
#endif

// ===================================
// SERVIDOR WEB
// ===================================
void manejarFoto() {
  sensor_t *s = esp_camera_sensor_get();
  s->set_framesize(s, TAM_FOTO);
  descartarFrames(3);                        // el primer frame tras cambiar de tamano sale malo

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    server.send(500, "text/plain", "Error de camara");
  } else {
    server.sendHeader("Cache-Control", "no-store");
    server.setContentLength(fb->len);
    server.send(200, "image/jpeg", "");
    server.client().write(fb->buf, fb->len);
    esp_camera_fb_return(fb);
  }

  s->set_framesize(s, TAM_DETECCION);        // volver a la resolucion de deteccion
  descartarFrames(2);
}

void manejarRaiz() {
  String t = "Uli CAM OK\nDeteccion de caras: ";
#if HAS_FACE_DETECT
  t += "ACTIVA (ESP-DL)";
#elif !defined(BOARD_HAS_PSRAM)
  t += "DESACTIVADA (PSRAM no habilitada en Herramientas)";
#else
  t += "DESACTIVADA (tu core no trae ESP-DL: usa 2.0.17)";
#endif
  t += "\nFoto: /camara.jpg\n";
  server.send(200, "text/plain", t);
}

void iniciarWeb() {
  if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);
  server.on("/", manejarRaiz);
  server.on("/camara.jpg", manejarFoto);
  server.begin();
  streamServer.begin();
  webListo = true;
  // La Pico ignora lineas que no empiezan con FACE; sirve para ver la IP con un USB-TTL
  Serial.print("CAM_IP:");
  Serial.println(WiFi.localIP());
}

// ===================================
// SETUP / LOOP
// ===================================
void setup() {
  pinMode(LED_ROJO, OUTPUT);
  ledRojo(false);

  Serial.begin(BAUD_PICO);                   // UART0 (GPIO1 TX) -> Pico GP9
  delay(100);
  Serial.println();                          // cierra cualquier linea sucia del arranque

  iniciarCamara();

#if HAS_FACE_DETECT
  detector = new HumanFaceDetectMSR01(0.3F, 0.5F, 10, 0.2F);   // 1 etapa: rapido (umbral, nms, top_k, escala)
  if (!detector) parpadearLed(3, 150);       // sin memoria para el detector
#else
  parpadearLed(3, 150);                      // aviso: sin deteccion de caras
#endif

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setSleep(false);                      // menos latencia
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  // Esperar WiFi como maximo 8 s: el seguimiento NO depende del WiFi
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 8000) delay(250);
  if (WiFi.status() == WL_CONNECTED) iniciarWeb();
}

void loop() {
  // Si el WiFi conecto tarde, levantar el servidor en ese momento
  if (!webListo && WiFi.status() == WL_CONNECTED) iniciarWeb();
  if (webListo) { server.handleClient(); atenderStream(); }

#if HAS_FACE_DETECT
  // Modo video fluido: mientras alguien ve el stream, no se detecta (el cuello no sigue caras)
  if (STREAM_PAUSA_DETECCION && streamActivo) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) { enviarFrameStream(fb); esp_camera_fb_return(fb); }
    return;
  }

  int x = 0, y = 0;
  if (detectarCara(x, y)) {
    Serial.printf("FACE %d %d\n", x, y);
    hayCara = true;
    ultimaCara = millis();
    ledRojo(true);
  } else if (hayCara && millis() - ultimaCara > NOFACE_DESPUES_MS) {
    Serial.println("NOFACE");
    hayCara = false;
    ledRojo(false);
  }
#else
  if (streamActivo) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) { enviarFrameStream(fb); esp_camera_fb_return(fb); }
  } else {
    delay(10);
  }
#endif
}
