#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
// Version 1.0.1
// --- DECLARACIÓN DE BUSES I2C ---
arduino::MbedI2C Wire1(4, 5); 
Adafruit_PWMServoDriver sshield = Adafruit_PWMServoDriver(0x40, Wire1);

// --- DEFINICIÓN DE CANALES ---
#define OREJA_DER  1
#define OREJA_IZQ  2
#define PARPADOS   4
#define BOCA       5
#define OJO_DER    7
#define OJO_IZQ    8
#define LATERALES  10
#define BASE       11

// --- VALORES PWM PARA CADA SERVO ---
// VALORES SUJETOS A CAMBIOS POR POCION
// lATERALES
#define LAT_NEU 150
#define LAT_IZ 250
#define LAT_DE 100

// Oreja Derecha
#define OD_ADE 300   // Adelante
#define OD_ATR 100   // Atrás
#define OD_NEU 150   // Neutro

// Oreja Izquierda
#define OI_ADE 300   // Adelante
#define OI_ATR 400   // Atrás
#define OI_NEU 340   // Neutro

// Párpados
#define PAR_ABI 400  // Abiertos
#define PAR_CER 50   // Cerrados
#define PAR_NEU 200  // Neutro

// Boca
#define BOC_CER 400  // Cerrado (Asumido como neutro)
#define BOC_ABI 100  // Abierto

// Ojos
#define ODER_CEN 100 // Mirada centro
#define ODER_NEU 400 // Mirada neutral
#define OIZQ_CEN 300 // Mirada centro
#define OIZQ_NEU 200 // Mirada neutral

// Variable de estado global
String estadoActual = "QUIETO";

// Función central para mover los servos
void setServoPulso(int canal, int pulso) {
  sshield.setPWM(canal, 0, pulso);
}

// Pone toda la cara en posición de descanso
void estado_neutro() {
  setServoPulso(OREJA_DER, OD_NEU);
  setServoPulso(OREJA_IZQ, OI_NEU);
  setServoPulso(PARPADOS, PAR_NEU);
  setServoPulso(BOCA, BOC_CER);
  setServoPulso(OJO_DER, ODER_NEU);
  setServoPulso(OJO_IZQ, OIZQ_NEU);
  setServoPulso(LATERALES, LAT_NEU);
}

// --- RUTINAS DE MOVIMIENTO ---

void rutina_hablar() {
  Serial.println("[EJECUTANDO] Hablando...");
  
  // 1. Boca abierta, párpados abiertos, OREJA DER ADELANTE, OREJA IZQ ATRÁS
  setServoPulso(BOCA, BOC_ABI);
  setServoPulso(PARPADOS, PAR_ABI);
  setServoPulso(OREJA_DER, OD_ADE);
  setServoPulso(OREJA_IZQ, OI_ATR); // Cruzadas
  delay(1000); 

  // 2. Boca cerrada, párpados neutros, OREJA DER ATRÁS, OREJA IZQ ADELANTE
  setServoPulso(BOCA, BOC_CER);
  setServoPulso(PARPADOS, PAR_NEU);
  setServoPulso(OREJA_DER, OD_ATR);
  setServoPulso(OREJA_IZQ, OI_ADE); // Cruzadas
  delay(1000); 
}

void rutina_parpadear() {
  Serial.println("[EJECUTANDO] Parpadeo");
  
  setServoPulso(PARPADOS, PAR_CER); // Cierra los ojos
  delay(1000);                       // Pausa muy breve
  setServoPulso(PARPADOS, PAR_NEU); // Vuelve a abrirlos (neutro)
}

void rutina_orejas_adelante() {
  Serial.println("[EJECUTANDO] Orejas en alerta (Adelante)");
  setServoPulso(OREJA_DER, OD_ADE);
  setServoPulso(OREJA_IZQ, OI_ADE);
}

void rutina_rotar_izquierda() {
  setServoPulso(LATERALES, LAT_IZ);
}

void rutina_rotar_derecha() {
  setServoPulso(LATERALES, LAT_DE);
}

// --- CONFIGURACIÓN Y LOOP ---

void setup() {
  // Inicializar puertos seriales
  Serial1.begin(115200); // Para el ESP32 (Pines GP0 y GP1)
  Serial.begin(115200);  // Para el Monitor Serial del PC (USB)
  
  // Iniciar bus I2C para la Servo Shield
  Wire1.begin(); // I2C1 (GP4/GP5)
  
  delay(3000); // Espera 3 segundos
  Serial.println("\n[SISTEMA] Iniciando secuencia de validacion...");
  // ==========================================
  // VALIDACIÓN DE LA SERVO SHIELD (PICO)
  // ==========================================
  Wire1.beginTransmission(0x40); // 0x40 es la dirección de la shield PCA9685
  byte errorServo = Wire1.endTransmission();

  if (errorServo == 0) {
    // La placa respondió, inicializamos todo normalmente
    sshield.begin();
    sshield.setOscillatorFrequency(27000000);
    sshield.setPWMFreq(50);  
    estado_neutro();
    
    // Enviar confirmación al ESP32-S3
    Serial1.println("STATUS:PICO_READY,SERVOS_OK");
    Serial.println("Pico Mbed: Hardware OK. Reporte enviado al ESP32. Esperando comandos...");
  } else {
    // La placa no respondió
    Serial1.println("STATUS:PICO_READY,SERVOS_FAIL");
    Serial.println("ALERTA: Servo Shield no detectada en I2C1 (0x40). Revisa las conexiones.");
  }
}

void loop() {
  String comandoRecibido = "";

  // 1A. Escuchar comandos desde el ESP32-S3 (Pines físicos RX/TX)
  if (Serial1.available() > 0) {
    comandoRecibido = Serial1.readStringUntil('\n');
  }
  
  // 1B. Escuchar comandos desde el Monitor Serial de tu PC (USB)
  if (Serial.available() > 0) {
    comandoRecibido = Serial.readStringUntil('\n');
  }

  // Si llegó un comando por cualquier vía, lo procesamos
  if (comandoRecibido != "") {
    comandoRecibido.trim(); 
    comandoRecibido.toUpperCase(); // Convierte todo a mayúsculas
    
    estadoActual = comandoRecibido;
    Serial.print("Comando recibido y guardado: ");
    Serial.println(estadoActual);
  }
  
  // 2. Máquina de estados (Ejecución de las rutinas)
  if (estadoActual == "HABLAR") {
    rutina_hablar(); 
    // Como es un bucle, si dice HABLAR lo seguirá haciendo hasta que mandes QUIETO
  } 
  else if (estadoActual == "QUIETO" || estadoActual == "DETENER") {
    estado_neutro();
    estadoActual = "REPOSO"; // Pasa a reposo para no enviar PWM infinito
  }
  else if (estadoActual == "PARPADEAR") {
    rutina_parpadear();
    estadoActual = "REPOSO_MANTENER"; // Hace el parpadeo una vez y se queda esperando
  }
  else if (estadoActual == "OREJAS_ADELANTE") {
    rutina_orejas_adelante();
    estadoActual = "REPOSO_MANTENER"; // Las pone adelante y se queda esperando
  }
  else if (estadoActual == "ROTAR_IZQUIERDA") {
    rutina_rotar_izquierda();
    estadoActual = "REPOSO_MANTENER"; 
  }
  else if (estadoActual == "ROTAR_DERECHA") {
    rutina_rotar_derecha();
    estadoActual = "REPOSO_MANTENER"; 
  }
  
  delay(400);
}