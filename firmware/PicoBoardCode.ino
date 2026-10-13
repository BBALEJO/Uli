#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

// ============================================================
//                  COMUNICACIÓN
// ============================================================

// PCA9685
arduino::MbedI2C Wire1(4, 5);
Adafruit_PWMServoDriver sshield = Adafruit_PWMServoDriver(0x40, Wire1);

// ESP32-S3
#define S3_SERIAL Serial1

// UART con ESP32-CAM
UART CAM_SERIAL(8, 9, NC, NC);

// ============================================================
//                  CANALES PCA9685
// ============================================================

#define BOCA       1
#define OREJA_DER  2
#define OREJA_IZQ  3
#define ROTAR      4
#define VERTICAL   9
#define PARPADOS   10
#define COSTADOS   12

// ============================================================
//              VALORES CALIBRADOS DE ULI
// ============================================================

// ---------------- BOCA ----------------

#define BOCA_CERRADA 260
#define BOCA_ABIERTA 100

// ---------------- OREJA IZQUIERDA ----------------

#define OREJA_IZQ_ADELANTE 250
#define OREJA_IZQ_ATRAS    380
#define OREJA_IZQ_NEUTRO   300

// ---------------- OREJA DERECHA ----------------

#define OREJA_DER_ADELANTE 230
#define OREJA_DER_ATRAS    100
#define OREJA_DER_NEUTRO   180

// ---------------- PARPADOS ----------------

#define PAR_CERRADO  80
#define PAR_ABIERTO  350
#define PAR_NEUTRO   300

// ---------------- COSTADOS ----------------

#define COST_IZQ   300
#define COST_DER   180
#define COST_NEUTRO 230

// ---------------- ROTACION ----------------

#define ROTAR_IZQ   100
#define ROTAR_DER   500
#define ROTAR_NEUTRO 380

// ---------------- VERTICAL ----------------

#define VERT_ARRIBA 150
#define VERT_ABAJO  300
#define VERT_NEUTRO 250

// ============================================================
//                    ESTADO DE SERVOS
// ============================================================

bool servosOk = false;

// Posiciones actuales
float rotarActual   = ROTAR_NEUTRO;
float verticalActual = VERT_NEUTRO;
float costadosActual = COST_NEUTRO;

// Objetivos
float rotarObjetivo    = ROTAR_NEUTRO;
float verticalObjetivo = VERT_NEUTRO;
float costadosObjetivo = COST_NEUTRO;

// ============================================================
//                 MOVIMIENTO SUAVE
// ============================================================

const unsigned long SERVO_TICK_MS = 20;

const float PASO_ROTAR    = 5.0;
const float PASO_VERTICAL = 4.0;
const float PASO_COSTADOS = 3.0;

unsigned long ultimoServoTick = 0;

// ============================================================
//                  SEGUIMIENTO FACIAL
// ============================================================

bool caraActiva = false;

int caraX = 50;
int caraY = 50;

unsigned long ultimaCara = 0;

const unsigned long CARA_TIMEOUT_MS = 1500;

// Si la cámara está montada en la cabeza,
// usamos seguimiento incremental.
#define CAMARA_EN_CABEZA true

#define INVERTIR_X false
#define INVERTIR_Y false

const float KP_ROTAR = 0.75;
const float KP_VERTICAL = 0.65;

const int ZONA_MUERTA_X = 5;
const int ZONA_MUERTA_Y = 5;

// ============================================================
//                 CONTROL MANUAL
// ============================================================

unsigned long manualHasta = 0;

const unsigned long MANUAL_HOLD_MS = 3000;

// ============================================================
//                  HABLAR
// ============================================================

bool hablando = false;

unsigned long ultimoHabla = 0;

bool faseHabla = false;

const unsigned long HABLA_INTERVALO = 500;

bool bocaExterna = false;

// ============================================================
//                    PARPADEO
// ============================================================

bool parpadeando = false;

unsigned long inicioParpadeo = 0;

const unsigned long PARPADEO_MS = 160;

// ============================================================
//             MOVIMIENTOS AUTONOMOS
// ============================================================

bool autonomiaActiva = true;

unsigned long proximaAccion = 0;

unsigned long finAccion = 0;

bool accionAutonoma = false;

const unsigned long AUTONOMO_MIN = 1200;
const unsigned long AUTONOMO_MAX = 4200;

// ============================================================
//                  FUNCIONES GENERALES
// ============================================================

void setServoPulso(int canal, int pulso) {

  if (!servosOk)
    return;

  sshield.setPWM(canal, 0, pulso);
}


// ------------------------------------------------------------
// Limitar valores
// ------------------------------------------------------------

float limitar(float valor, float minimo, float maximo) {

  if (valor < minimo)
    return minimo;

  if (valor > maximo)
    return maximo;

  return valor;
}


// ============================================================
//                 POSICIONES DE ULI
// ============================================================

void bocaCerrada() {

  setServoPulso(BOCA, BOCA_CERRADA);
}


void bocaAbierta() {

  setServoPulso(BOCA, BOCA_ABIERTA);
}


// ------------------------------------------------------------
// Orejas neutras
// ------------------------------------------------------------

void orejasNeutras() {

  setServoPulso(OREJA_IZQ, OREJA_IZQ_NEUTRO);
  setServoPulso(OREJA_DER, OREJA_DER_NEUTRO);
}


// ------------------------------------------------------------
// Párpados neutros
// ------------------------------------------------------------

void parpadosNeutros() {

  setServoPulso(PARPADOS, PAR_NEUTRO);
}


// ------------------------------------------------------------
// Cabeza centrada
// ------------------------------------------------------------

void cabezaCentrada() {

  rotarObjetivo = ROTAR_NEUTRO;
  verticalObjetivo = VERT_NEUTRO;
  costadosObjetivo = COST_NEUTRO;
}


// ============================================================
//                  PARPADEO
// ============================================================

void iniciarParpadeo() {

  if (hablando)
    return;

  if (parpadeando)
    return;

  parpadeando = true;

  inicioParpadeo = millis();

  setServoPulso(PARPADOS, PAR_CERRADO);
}


void actualizarParpadeo() {

  if (!parpadeando)
    return;

  if (millis() - inicioParpadeo >= PARPADEO_MS) {

    parpadeando = false;

    setServoPulso(PARPADOS, PAR_NEUTRO);
  }
}


// ============================================================
//                 HABLAR
// ============================================================

void iniciarHablar() {

  hablando = true;

  autonomiaActiva = false;

  // Congelar cabeza inmediatamente
  rotarObjetivo = rotarActual;
  verticalObjetivo = verticalActual;
  costadosObjetivo = costadosActual;

  ultimoHabla = millis();

  faseHabla = false;

  parpadeando = false;

  // Estado inicial
  bocaAbierta();
  setServoPulso(PARPADOS, PAR_ABIERTO);

  Serial.println("HABLAR -> prioridad activa");
}


void actualizarHablar() {

  if (!hablando)
    return;

  // Mantener cabeza congelada
  rotarObjetivo = rotarActual;
  verticalObjetivo = verticalActual;
  costadosObjetivo = costadosActual;

  // Si el sistema externo controla la boca,
  // no interferimos.
  if (!bocaExterna) {

    if (millis() - ultimoHabla >= HABLA_INTERVALO) {

      ultimoHabla = millis();

      faseHabla = !faseHabla;

      if (faseHabla) {

        bocaAbierta();
        setServoPulso(PARPADOS, PAR_ABIERTO);

        setServoPulso(
          OREJA_IZQ,
          OREJA_IZQ_ADELANTE
        );

        setServoPulso(
          OREJA_DER,
          OREJA_DER_ATRAS
        );

      } else {

        bocaCerrada();
        setServoPulso(PARPADOS, PAR_NEUTRO);

        setServoPulso(
          OREJA_IZQ,
          OREJA_IZQ_ATRAS
        );

        setServoPulso(
          OREJA_DER,
          OREJA_DER_ADELANTE
        );
      }
    }
  }
}


// ============================================================
//                 MOVIMIENTO DE SERVOS
// ============================================================

float moverSuave(float actual, float objetivo, float paso) {

  if (actual < objetivo) {

    actual += paso;

    if (actual > objetivo)
      actual = objetivo;

  } else if (actual > objetivo) {

    actual -= paso;

    if (actual < objetivo)
      actual = objetivo;
  }

  return actual;
}


void actualizarServos() {

  if (millis() - ultimoServoTick < SERVO_TICK_MS)
    return;

  ultimoServoTick = millis();

  // Durante HABLAR la cabeza queda congelada
  if (hablando)
    return;

  rotarActual = moverSuave(
    rotarActual,
    rotarObjetivo,
    PASO_ROTAR
  );

  verticalActual = moverSuave(
    verticalActual,
    verticalObjetivo,
    PASO_VERTICAL
  );

  costadosActual = moverSuave(
    costadosActual,
    costadosObjetivo,
    PASO_COSTADOS
  );

  setServoPulso(
    ROTAR,
    (int)rotarActual
  );

  setServoPulso(
    VERTICAL,
    (int)verticalActual
  );

  setServoPulso(
    COSTADOS,
    (int)costadosActual
  );
}


// ============================================================
//                SEGUIMIENTO FACIAL
// ============================================================

void aplicarSeguimiento(int x, int y) {

  if (hablando)
    return;

  x = constrain(x, 0, 100);
  y = constrain(y, 0, 100);

  if (INVERTIR_X)
    x = 100 - x;

  if (INVERTIR_Y)
    y = 100 - y;

  caraX = x;
  caraY = y;

  caraActiva = true;

  ultimaCara = millis();

  // ----------------------------------------------------------
  // ROTACION
  // ----------------------------------------------------------

  int errorX = x - 50;

  if (abs(errorX) > ZONA_MUERTA_X) {

    float movimientoX = errorX * KP_ROTAR;

    rotarObjetivo += movimientoX;
  }

  // ----------------------------------------------------------
  // VERTICAL
  // ----------------------------------------------------------

  int errorY = y - 50;

  if (abs(errorY) > ZONA_MUERTA_Y) {

    float movimientoY = errorY * KP_VERTICAL;

    verticalObjetivo += movimientoY;
  }

  // Límites físicos calibrados
  rotarObjetivo = limitar(
    rotarObjetivo,
    ROTAR_IZQ,
    ROTAR_DER
  );

  verticalObjetivo = limitar(
    verticalObjetivo,
    VERT_ARRIBA,
    VERT_ABAJO
  );
}


// ============================================================
//                PROCESAR CÁMARA
// ============================================================

void procesarCamara() {

  static String buffer = "";

  while (CAM_SERIAL.available()) {

    char c = CAM_SERIAL.read();

    if (c == '\n') {

      buffer.trim();

      if (buffer.startsWith("FACE")) {

        int x = 50;
        int y = 50;

        int separador = buffer.indexOf(' ');

        if (separador >= 0) {

          String datos =
            buffer.substring(separador + 1);

          int espacio = datos.indexOf(' ');

          if (espacio >= 0) {

            x = datos.substring(0, espacio).toInt();
            y = datos.substring(espacio + 1).toInt();

            aplicarSeguimiento(x, y);
          }
        }
      }

      else if (buffer == "NOFACE") {

        // No hacemos nada inmediatamente.
        // La pérdida se determina mediante timeout.
      }

      buffer = "";
    }

    else {

      buffer += c;

      if (buffer.length() > 50)
        buffer = "";
    }
  }

  // ----------------------------------------------------------
  // Determinar si realmente se perdió la cara
  // ----------------------------------------------------------

  if (
    caraActiva &&
    millis() - ultimaCara > CARA_TIMEOUT_MS
  ) {

    caraActiva = false;
  }
}


// ============================================================
//               MOVIMIENTOS AUTÓNOMOS
// ============================================================

void programarSiguienteAccion() {

  unsigned long espera =
    random(AUTONOMO_MIN, AUTONOMO_MAX + 1);

  proximaAccion = millis() + espera;
}


// ------------------------------------------------------------
// Acción aleatoria
// ------------------------------------------------------------

void ejecutarAccionAutonoma() {

  if (hablando)
    return;

  if (caraActiva)
    return;

  if (millis() < proximaAccion)
    return;

  if (accionAutonoma)
    return;

  int accion = random(0, 100);

  // ----------------------------------------------------------
  // 0-34: PARPADEAR
  // ----------------------------------------------------------

  if (accion < 35) {

    iniciarParpadeo();

    finAccion = millis() + 250;
  }

  // ----------------------------------------------------------
  // 35-49: OREJAS
  // ----------------------------------------------------------

  else if (accion < 50) {

    if (random(0, 2) == 0) {

      setServoPulso(
        OREJA_IZQ,
        OREJA_IZQ_ADELANTE
      );

      setServoPulso(
        OREJA_DER,
        OREJA_DER_ATRAS
      );

    } else {

      setServoPulso(
        OREJA_IZQ,
        OREJA_IZQ_ATRAS
      );

      setServoPulso(
        OREJA_DER,
        OREJA_DER_ADELANTE
      );
    }

    finAccion = millis() + random(500, 1100);
  }

  // ----------------------------------------------------------
  // 50-69: GIRAR CABEZA
  // ----------------------------------------------------------

  else if (accion < 70) {

    int direccion = random(0, 2);

    if (direccion == 0) {

      // Movimiento pequeño hacia izquierda
      rotarObjetivo =
        ROTAR_NEUTRO -
        random(45, 100);

    } else {

      // Movimiento pequeño hacia derecha
      rotarObjetivo =
        ROTAR_NEUTRO +
        random(45, 100);
    }

    rotarObjetivo = limitar(
      rotarObjetivo,
      ROTAR_IZQ,
      ROTAR_DER
    );

    finAccion = millis() + random(700, 1500);
  }

  // ----------------------------------------------------------
  // 70-82: MIRAR ARRIBA / ABAJO
  // ----------------------------------------------------------

  else if (accion < 83) {

    int direccion = random(0, 2);

    if (direccion == 0) {

      verticalObjetivo =
        VERT_NEUTRO - random(25, 55);

    } else {

      verticalObjetivo =
        VERT_NEUTRO + random(20, 45);
    }

    verticalObjetivo = limitar(
      verticalObjetivo,
      VERT_ARRIBA,
      VERT_ABAJO
    );

    finAccion = millis() + random(600, 1300);
  }

  // ----------------------------------------------------------
  // 83-92: INCLINAR COSTADOS
  // ----------------------------------------------------------

  else if (accion < 93) {

    int direccion = random(0, 2);

    if (direccion == 0) {

      costadosObjetivo =
        COST_NEUTRO + random(15, 30);

    } else {

      costadosObjetivo =
        COST_NEUTRO - random(15, 30);
    }

    costadosObjetivo = limitar(
      costadosObjetivo,
      COST_DER,
      COST_IZQ
    );

    finAccion = millis() + random(700, 1400);
  }

  // ----------------------------------------------------------
  // 93-99: MOVIMIENTO COMBINADO
  // ----------------------------------------------------------

  else {

    rotarObjetivo =
      ROTAR_NEUTRO + random(-70, 71);

    verticalObjetivo =
      VERT_NEUTRO + random(-35, 36);

    costadosObjetivo =
      COST_NEUTRO + random(-25, 26);

    rotarObjetivo = limitar(
      rotarObjetivo,
      ROTAR_IZQ,
      ROTAR_DER
    );

    verticalObjetivo = limitar(
      verticalObjetivo,
      VERT_ARRIBA,
      VERT_ABAJO
    );

    costadosObjetivo = limitar(
      costadosObjetivo,
      COST_DER,
      COST_IZQ
    );

    finAccion = millis() + random(700, 1500);
  }

  accionAutonoma = true;
}


// ------------------------------------------------------------
// Finalizar acción autónoma
// ------------------------------------------------------------

void actualizarAccionAutonoma() {

  if (!accionAutonoma)
    return;

  if (millis() < finAccion)
    return;

  // Regresar suavemente a una posición natural
  rotarObjetivo = ROTAR_NEUTRO;
  verticalObjetivo = VERT_NEUTRO;
  costadosObjetivo = COST_NEUTRO;

  orejasNeutras();

  accionAutonoma = false;

  programarSiguienteAccion();
}


// ============================================================
//              PROCESAMIENTO DE COMANDOS
// ============================================================

void procesarComando(String cmd) {

  cmd.trim();

  if (cmd.length() == 0)
    return;

  Serial.print("CMD: ");
  Serial.println(cmd);

  // ==========================================================
  // HABLAR
  // ==========================================================

  if (cmd == "HABLAR") {

    iniciarHablar();

    return;
  }


  // ==========================================================
  // QUIETO
  // ==========================================================

  if (cmd == "QUIETO") {

    hablando = false;

    bocaExterna = false;

    autonomiaActiva = true;

    bocaCerrada();

    orejasNeutras();

    parpadosNeutros();

    cabezaCentrada();

    accionAutonoma = false;

    programarSiguienteAccion();

    return;
  }


  // ==========================================================
  // BOCA
  // ==========================================================

  if (cmd.startsWith("BOCA")) {

    int valor = cmd.substring(4).toInt();

    valor = constrain(
      valor,
      BOCA_ABIERTA,
      BOCA_CERRADA
    );

    setServoPulso(
      BOCA,
      valor
    );

    bocaExterna = true;

    return;
  }


  // ==========================================================
  // ROTAR IZQUIERDA
  // ==========================================================

  if (cmd == "ROTAR_IZQUIERDA") {

    manualHasta =
      millis() + MANUAL_HOLD_MS;

    rotarObjetivo = ROTAR_IZQ;

    return;
  }


  // ==========================================================
  // ROTAR DERECHA
  // ==========================================================

  if (cmd == "ROTAR_DERECHA") {

    manualHasta =
      millis() + MANUAL_HOLD_MS;

    rotarObjetivo = ROTAR_DER;

    return;
  }


  // ==========================================================
  // SUBIR CABEZA
  // ==========================================================

  if (cmd == "SUBIR_CABEZA") {

    manualHasta =
      millis() + MANUAL_HOLD_MS;

    verticalObjetivo = VERT_ARRIBA;

    return;
  }


  // ==========================================================
  // BAJAR CABEZA
  // ==========================================================

  if (cmd == "BAJAR_CABEZA") {

    manualHasta =
      millis() + MANUAL_HOLD_MS;

    verticalObjetivo = VERT_ABAJO;

    return;
  }


  // ==========================================================
  // COSTADO IZQUIERDA
  // ==========================================================

  if (cmd == "COSTADO_IZQUIERDA") {

    manualHasta =
      millis() + MANUAL_HOLD_MS;

    costadosObjetivo = COST_IZQ;

    return;
  }


  // ==========================================================
  // COSTADO DERECHA
  // ==========================================================

  if (cmd == "COSTADO_DERECHA") {

    manualHasta =
      millis() + MANUAL_HOLD_MS;

    costadosObjetivo = COST_DER;

    return;
  }


  // ==========================================================
  // CENTRAR
  // ==========================================================

  if (cmd == "CENTRAR_CABEZA") {

    manualHasta =
      millis() + MANUAL_HOLD_MS;

    cabezaCentrada();

    return;
  }


  // ==========================================================
  // PARPADEAR
  // ==========================================================

  if (cmd == "PARPADEAR") {

    iniciarParpadeo();

    return;
  }


  // ==========================================================
  // OREJAS ADELANTE
  // ==========================================================

  if (cmd == "OREJAS_ADELANTE") {

    setServoPulso(
      OREJA_IZQ,
      OREJA_IZQ_ADELANTE
    );

    setServoPulso(
      OREJA_DER,
      OREJA_DER_ADELANTE
    );

    return;
  }


  // ==========================================================
  // OREJAS ATRÁS
  // ==========================================================

  if (cmd == "OREJAS_ATRAS") {

    setServoPulso(
      OREJA_IZQ,
      OREJA_IZQ_ATRAS
    );

    setServoPulso(
      OREJA_DER,
      OREJA_DER_ATRAS
    );

    return;
  }
}


// ============================================================
//             PROCESAR SERIAL S3
// ============================================================

void procesarS3() {

  static String buffer = "";

  while (S3_SERIAL.available()) {

    char c = S3_SERIAL.read();

    if (c == '\n') {

      buffer.trim();

      procesarComando(buffer);

      buffer = "";
    }

    else {

      buffer += c;

      if (buffer.length() > 80)
        buffer = "";
    }
  }
}


// ============================================================
//                       SETUP
// ============================================================

void setup() {

  Serial.begin(115200);

  delay(1000);

  Serial.println();
  Serial.println("================================");
  Serial.println("       ULI - RASPBERRY PICO");
  Serial.println("================================");

  // ----------------------------------------------------------
  // UART S3
  // ----------------------------------------------------------

  S3_SERIAL.setTX(0);
  S3_SERIAL.setRX(1);

  S3_SERIAL.begin(115200);

  // ----------------------------------------------------------
  // UART CAMARA
  // ----------------------------------------------------------

  CAM_SERIAL.begin(115200);

  // ----------------------------------------------------------
  // PCA9685
  // ----------------------------------------------------------

  Wire1.begin();

  sshield.begin();

  sshield.setOscillatorFrequency(27000000);

  sshield.setPWMFreq(50);

  delay(500);

  servosOk = true;

  // ----------------------------------------------------------
  // Posiciones iniciales
  // ----------------------------------------------------------

  setServoPulso(
    BOCA,
    BOCA_CERRADA
  );

  setServoPulso(
    OREJA_IZQ,
    OREJA_IZQ_NEUTRO
  );

  setServoPulso(
    OREJA_DER,
    OREJA_DER_NEUTRO
  );

  setServoPulso(
    PARPADOS,
    PAR_NEUTRO
  );

  setServoPulso(
    ROTAR,
    ROTAR_NEUTRO
  );

  setServoPulso(
    VERTICAL,
    VERT_NEUTRO
  );

  setServoPulso(
    COSTADOS,
    COST_NEUTRO
  );

  // ----------------------------------------------------------
  // Semilla aleatoria
  // ----------------------------------------------------------

  randomSeed(micros());

  programarSiguienteAccion();

  Serial.println("Servos inicializados.");
  Serial.println("Seguimiento facial X + Y activo.");
  Serial.println("Autonomia activa.");
}


// ============================================================
//                        LOOP
// ============================================================

void loop() {

  // Comunicaciones
  procesarS3();

  procesarCamara();


  // ----------------------------------------------------------
  // HABLAR tiene prioridad absoluta
  // ----------------------------------------------------------

  if (hablando) {

    actualizarHablar();

  }

  else {

    // --------------------------------------------------------
    // Si hay cara -> seguimiento
    // Si no hay cara -> autonomia
    // --------------------------------------------------------

    if (caraActiva) {

      // La cara tiene prioridad sobre autonomía
      accionAutonoma = false;

    }

    else if (
      autonomiaActiva &&
      millis() >= manualHasta
    ) {

      ejecutarAccionAutonoma();

      actualizarAccionAutonoma();
    }
  }


  // Actualizaciones
  actualizarParpadeo();

  actualizarServos();

  delay(1);
}
