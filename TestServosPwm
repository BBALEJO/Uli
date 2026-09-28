#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

// =====================================================
// I2C
// =====================================================

arduino::MbedI2C Wire1(4, 5);
Adafruit_PWMServoDriver sshield = Adafruit_PWMServoDriver(0x40, Wire1);

// =====================================================
// CANALES
// =====================================================

#define OREJA_DER  1
#define OREJA_IZQ  2
#define OJO_DER    3
#define OJO_IZQ    4
#define PARPADOS   5
#define BOCA       6
#define DEL_ATR    9
#define LATERALES  10
#define BASE       11

// =====================================================
// MOVER SERVO
// =====================================================

void moverServo(int canal, int pwm) {

  // Seguridad: PCA9685 trabaja de 0 a 4095
  pwm = constrain(pwm, 0, 4095);

  sshield.setPWM(canal, 0, pwm);

  Serial.print("Canal ");
  Serial.print(canal);
  Serial.print(" -> PWM ");
  Serial.println(pwm);
}

// =====================================================
// AYUDA
// =====================================================

void mostrarAyuda() {

  Serial.println();
  Serial.println("========================================");
  Serial.println("       CALIBRACION MANUAL SERVOS");
  Serial.println("========================================");
  Serial.println();
  Serial.println("Escribe:");
  Serial.println();
  Serial.println("CANAL PWM");
  Serial.println();
  Serial.println("Ejemplos:");
  Serial.println("1 300");
  Serial.println("1 150");
  Serial.println("2 340");
  Serial.println("5 200");
  Serial.println("11 250");
  Serial.println();
  Serial.println("Canales:");
  Serial.println("1  = Oreja derecha");
  Serial.println("2  = Oreja izquierda");
  Serial.println("3  = Ojo derecho");
  Serial.println("4  = Ojo izquierdo");
  Serial.println("5  = Parpados");
  Serial.println("6  = Boca");
  Serial.println("9  = Delante/Atras");
  Serial.println("10 = Laterales");
  Serial.println("11 = Base");
  Serial.println();
  Serial.println("Escribe H para volver a mostrar esta ayuda.");
  Serial.println("========================================");
  Serial.println();
}

// =====================================================
// SETUP
// =====================================================

void setup() {

  Serial.begin(115200);
  Serial1.begin(115200);

  Wire1.begin();

  delay(1000);

  sshield.begin();

  sshield.setOscillatorFrequency(27000000);
  sshield.setPWMFreq(50);

  Serial.println();
  Serial.println("========================================");
  Serial.println("     SISTEMA DE CALIBRACION MANUAL");
  Serial.println("========================================");

  mostrarAyuda();
}

// =====================================================
// LOOP
// =====================================================

void loop() {

  if (Serial.available()) {

    String comando = Serial.readStringUntil('\n');

    comando.trim();

    // -----------------------------------------
    // AYUDA
    // -----------------------------------------

    if (comando.equalsIgnoreCase("H")) {

      mostrarAyuda();
      return;
    }

    // -----------------------------------------
    // BUSCAR ESPACIO
    // -----------------------------------------

    int espacio = comando.indexOf(' ');

    if (espacio == -1) {

      Serial.println("Formato incorrecto.");
      Serial.println("Ejemplo: 1 300");

      return;
    }

    // -----------------------------------------
    // SEPARAR CANAL Y PWM
    // -----------------------------------------

    String textoCanal = comando.substring(0, espacio);
    String textoPWM   = comando.substring(espacio + 1);

    textoCanal.trim();
    textoPWM.trim();

    int canal = textoCanal.toInt();
    int pwm   = textoPWM.toInt();

    // -----------------------------------------
    // VALIDAR CANAL
    // -----------------------------------------

    if (canal < 0 || canal > 15) {

      Serial.println("ERROR: Canal debe estar entre 0 y 15.");

      return;
    }

    // -----------------------------------------
    // VALIDAR PWM
    // -----------------------------------------

    if (pwm < 0 || pwm > 4095) {

      Serial.println("ERROR: PWM debe estar entre 0 y 4095.");

      return;
    }

    // -----------------------------------------
    // MOVER
    // -----------------------------------------

    moverServo(canal, pwm);
  }
}