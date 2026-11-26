
/*
  ============================================================
  SISTEM INTELIGENT DE SUPRAVEGHERE CU ESP32-CAM AI THINKER
  ============================================================

  Functionalitati:
  - Transmisie video live prin server web
  - Control servomotoare PAN si TILT
  - Reglarea luminozitatii LED-ului flash
  - Detectarea miscarii cu senzor PIR
  - Trimiterea fotografiilor prin Telegram
  - Comenzi Telegram: /start, /photo, /flash, /status
  - Autentificare pentru interfata web

  Conexiuni:
  - Servo PAN: GPIO14
  - Servo TILT: GPIO15
  - Senzor PIR: GPIO13
  - LED Flash: GPIO4

  Credentialele sunt definite in secrets.h.
*/

// ============================================================
// BIBLIOTECI
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>

#include "esp_camera.h"
#include "esp_http_server.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_arduino_version.h"
#include "mbedtls/base64.h"

#include "secrets.h"

// ============================================================
// CONFIGURARE CAMERA AI THINKER
// ============================================================

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

// ============================================================
// CONFIGURARE PINI PERIFERICE
// ============================================================

#define PIN_SERVO_PAN       14
#define PIN_SERVO_TILT      15
#define PIN_PIR             13
#define PIN_FLASH            4

// ============================================================
// CONFIGURARE SERVOMOTOARE
// ============================================================

#define POZITIE_MINIMA       0
#define POZITIE_MAXIMA     180
#define PAS_SERVO           10

int pozitiePAN = 90;
int pozitieTILT = 90;

Servo servoPAN;
Servo servoTILT;

// ============================================================
// CONFIGURARE LED FLASH
// ============================================================

#define FRECVENTA_FLASH  5000
#define REZOLUTIE_FLASH  8
#define CANAL_FLASH      7

int luminozitateFlash = 0;

// ============================================================
// CONFIGURARE SENZOR PIR
// ============================================================

const unsigned long INTERVAL_ALARMA_PIR = 30000;

bool stareAnterioaraPIR = false;
unsigned long ultimaAlarmaPIR = 0;

// ============================================================
// CONFIGURARE TELEGRAM
// ============================================================

WiFiClientSecure clientTelegram;
UniversalTelegramBot bot(TOKEN_BOT_TELEGRAM, clientTelegram);

unsigned long ultimaVerificareTelegram = 0;
const unsigned long INTERVAL_TELEGRAM = 1500;

// ============================================================
// CONFIGURARE SERVER WEB
// ============================================================

httpd_handle_t serverCamera = NULL;
httpd_handle_t serverFlux = NULL;

#define LIMITA_DIMENSIUNE_CALE  32

static const char* DELIMITATOR_FLUX = "123456789000000000000987654321";

static const char* TIP_CONTINUT_FLUX =
  "multipart/x-mixed-replace;boundary=123456789000000000000987654321";

static const char* DELIMITATOR_CADRU =
  "\r\n--123456789000000000000987654321\r\n";

static const char* ANTET_CADRU =
  "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// ============================================================
// INTERFATA WEB
// ============================================================

static const char PAGINA_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="ro">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">

<title>Sistem ESP32-CAM</title>

<style>
body {
  font-family: Arial, sans-serif;
  text-align: center;
  background: #eef2f5;
  margin: 0;
  padding: 20px;
}

h1 {
  color: #26364a;
}

.container {
  max-width: 750px;
  margin: auto;
  background: white;
  padding: 20px;
  border-radius: 12px;
}

img {
  width: 100%;
  max-width: 640px;
  border-radius: 8px;
}

button {
  background: #2f4468;
  color: white;
  border: none;
  padding: 13px 20px;
  margin: 5px;
  border-radius: 6px;
  font-size: 16px;
  cursor: pointer;
}

button:active {
  background: #17263d;
}

input[type=range] {
  width: 80%;
  max-width: 400px;
}

.control {
  margin: 20px 0;
}

.valoare {
  font-weight: bold;
}
</style>
</head>

<body>

<div class="container">

<h1>Sistem de supraveghere ESP32-CAM</h1>

<h3>Transmisie video live</h3>

<img id="fluxVideo" src="">

<div class="control">

<h3>Control camera PAN / TILT</h3>

<div>
<button onclick="trimiteComanda('up')">SUS</button>
</div>

<div>
<button onclick="trimiteComanda('left')">STANGA</button>
<button onclick="trimiteComanda('right')">DREAPTA</button>
</div>

<div>
<button onclick="trimiteComanda('down')">JOS</button>
</div>

</div>

<div class="control">

<h3>Control iluminare LED</h3>

<p>Luminozitate:
<span class="valoare" id="valoareFlash">0</span>
</p>

<input
type="range"
min="0"
max="255"
value="0"
id="sliderFlash"
oninput="modificaFlash(this.value)"
>

</div>

</div>

<script>

window.onload = function() {
  document.getElementById("fluxVideo").src =
    "http://" + window.location.hostname + ":81/stream";
};

function trimiteComanda(comanda) {
  fetch("/action?go=" + comanda);
}

function modificaFlash(valoare) {
  document.getElementById("valoareFlash").innerText = valoare;
  fetch("/flash?value=" + valoare);
}

</script>

</body>
</html>
)rawliteral";

// ============================================================
// CONTROL LED FLASH - COMPATIBILITATE ESP32 CORE 2.x / 3.x
// ============================================================

void initializeazaFlash() {

#if ESP_ARDUINO_VERSION_MAJOR >= 3

  ledcAttach(PIN_FLASH, FRECVENTA_FLASH, REZOLUTIE_FLASH);

#else

  ledcSetup(CANAL_FLASH, FRECVENTA_FLASH, REZOLUTIE_FLASH);
  ledcAttachPin(PIN_FLASH, CANAL_FLASH);

#endif

  seteazaFlash(0);
}

void seteazaFlash(int valoare) {

  valoare = constrain(valoare, 0, 255);
  luminozitateFlash = valoare;

#if ESP_ARDUINO_VERSION_MAJOR >= 3

  ledcWrite(PIN_FLASH, valoare);

#else

  ledcWrite(CANAL_FLASH, valoare);

#endif
}

// ============================================================
// CONTROL SERVOMOTOARE
// ============================================================

void initializeazaServouri() {

  servoPAN.setPeriodHertz(50);
  servoTILT.setPeriodHertz(50);

  servoPAN.attach(PIN_SERVO_PAN, 500, 2400);
  servoTILT.attach(PIN_SERVO_TILT, 500, 2400);

  pozitiePAN = 90;
  pozitieTILT = 90;

  servoPAN.write(pozitiePAN);
  servoTILT.write(pozitieTILT);
}

void modificaPAN(int pas) {

  pozitiePAN = constrain(
    pozitiePAN + pas,
    POZITIE_MINIMA,
    POZITIE_MAXIMA
  );

  servoPAN.write(pozitiePAN);
}

void modificaTILT(int pas) {

  pozitieTILT = constrain(
    pozitieTILT + pas,
    POZITIE_MINIMA,
    POZITIE_MAXIMA
  );

  servoTILT.write(pozitieTILT);
}

// ============================================================
// AUTENTIFICARE SERVER WEB
// ============================================================

bool verificaAutentificare(httpd_req_t *cerere) {

  char antetAutentificare[256];

  if (
    httpd_req_get_hdr_value_str(
      cerere,
      "Authorization",
      antetAutentificare,
      sizeof(antetAutentificare)
    ) != ESP_OK
  ) {

    httpd_resp_set_status(cerere, "401 Unauthorized");

    httpd_resp_set_hdr(
      cerere,
      "WWW-Authenticate",
      "Basic realm=\"ESP32-CAM\""
    );

    httpd_resp_send(
      cerere,
      "Autentificare necesara",
      HTTPD_RESP_USE_STRLEN
    );

    return false;
  }

  if (strncmp(antetAutentificare, "Basic ", 6) != 0) {
    return false;
  }

  unsigned char decodat[192];
  size_t lungimeDecodata = 0;

  int rezultat = mbedtls_base64_decode(
    decodat,
    sizeof(decodat) - 1,
    &lungimeDecodata,
    (const unsigned char*)(antetAutentificare + 6),
    strlen(antetAutentificare + 6)
  );

  if (rezultat != 0) {

    httpd_resp_set_status(cerere, "401 Unauthorized");
    httpd_resp_send(
      cerere,
      "Credentiale invalide",
      HTTPD_RESP_USE_STRLEN
    );

    return false;
  }

  decodat[lungimeDecodata] = '\0';

  String credentialePrimite = String((char*)decodat);

  String credentialeCorecte =
    String(NUME_UTILIZATOR_WEB) + ":" + String(PAROLA_WEB);

  if (credentialePrimite != credentialeCorecte) {

    httpd_resp_set_status(cerere, "401 Unauthorized");

    httpd_resp_set_hdr(
      cerere,
      "WWW-Authenticate",
      "Basic realm=\"ESP32-CAM\""
    );

    httpd_resp_send(
      cerere,
      "Utilizator sau parola incorecta",
      HTTPD_RESP_USE_STRLEN
    );

    return false;
  }

  return true;
}

// ============================================================
// PAGINA PRINCIPALA
// ============================================================

static esp_err_t indexHandler(httpd_req_t *cerere) {

  if (!verificaAutentificare(cerere)) {
    return ESP_OK;
  }

  httpd_resp_set_type(cerere, "text/html");

  return httpd_resp_send(
    cerere,
    PAGINA_HTML,
    HTTPD_RESP_USE_STRLEN
  );
}

// ============================================================
// FLUX VIDEO LIVE
// ============================================================

static esp_err_t fluxHandler(httpd_req_t *cerere) {

  if (!verificaAutentificare(cerere)) {
    return ESP_OK;
  }

  esp_err_t rezultat = ESP_OK;

  camera_fb_t *cadru = NULL;

  char antetCadru[96];

  httpd_resp_set_type(cerere, TIP_CONTINUT_FLUX);

  while (true) {

    cadru = esp_camera_fb_get();

    if (!cadru) {

      Serial.println("Eroare la capturarea imaginii.");

      rezultat = ESP_FAIL;
      break;
    }

    size_t lungimeAntet = snprintf(
      antetCadru,
      sizeof(antetCadru),
      ANTET_CADRU,
      cadru->len
    );

    rezultat = httpd_resp_send_chunk(
      cerere,
      DELIMITATOR_CADRU,
      strlen(DELIMITATOR_CADRU)
    );

    if (rezultat == ESP_OK) {

      rezultat = httpd_resp_send_chunk(
        cerere,
        antetCadru,
        lungimeAntet
      );
    }

    if (rezultat == ESP_OK) {

      rezultat = httpd_resp_send_chunk(
        cerere,
        (const char*)cadru->buf,
        cadru->len
      );
    }

    esp_camera_fb_return(cadru);
    cadru = NULL;

    if (rezultat != ESP_OK) {
      break;
    }

    vTaskDelay(pdMS_TO_TICKS(30));
  }

  return rezultat;
}

// ============================================================
// CONTROL SERVOURI DIN INTERFATA WEB
// ============================================================

static esp_err_t actionHandler(httpd_req_t *cerere) {

  if (!verificaAutentificare(cerere)) {
    return ESP_OK;
  }

  char interogare[100];
  char comanda[24];

  if (
    httpd_req_get_url_query_str(
      cerere,
      interogare,
      sizeof(interogare)
    ) != ESP_OK
  ) {
    httpd_resp_send_400(cerere);
    return ESP_FAIL;
  }

  if (
    httpd_query_key_value(
      interogare,
      "go",
      comanda,
      sizeof(comanda)
    ) != ESP_OK
  ) {
    httpd_resp_send_400(cerere);
    return ESP_FAIL;
  }

  if (strcmp(comanda, "up") == 0) {

    modificaTILT(PAS_SERVO);

  } else if (strcmp(comanda, "down") == 0) {

    modificaTILT(-PAS_SERVO);

  } else if (strcmp(comanda, "left") == 0) {

    modificaPAN(PAS_SERVO);

  } else if (strcmp(comanda, "right") == 0) {

    modificaPAN(-PAS_SERVO);

  } else {

    httpd_resp_send_400(cerere);
    return ESP_FAIL;
  }

  httpd_resp_set_hdr(
    cerere,
    "Cache-Control",
    "no-store"
  );

  return httpd_resp_send(
    cerere,
    "OK",
    HTTPD_RESP_USE_STRLEN
  );
}

// ============================================================
// CONTROL LUMINOZITATE FLASH DIN INTERFATA WEB
// ============================================================

static esp_err_t flashHandler(httpd_req_t *cerere) {

  if (!verificaAutentificare(cerere)) {
    return ESP_OK;
  }

  char interogare[64];
  char valoareText[16];

  if (
    httpd_req_get_url_query_str(
      cerere,
      interogare,
      sizeof(interogare)
    ) != ESP_OK
  ) {
    httpd_resp_send_400(cerere);
    return ESP_FAIL;
  }

  if (
    httpd_query_key_value(
      interogare,
      "value",
      valoareText,
      sizeof(valoareText)
    ) != ESP_OK
  ) {
    httpd_resp_send_400(cerere);
    return ESP_FAIL;
  }

  seteazaFlash(atoi(valoareText));

  return httpd_resp_send(
    cerere,
    "OK",
    HTTPD_RESP_USE_STRLEN
  );
}

// ============================================================
// PORNIRE SERVER WEB SI SERVER VIDEO
// ============================================================

void pornesteServerWeb() {

  httpd_config_t configurare = HTTPD_DEFAULT_CONFIG();

  configurare.server_port = 80;
  configurare.ctrl_port = 32768;

  httpd_uri_t rutaPrincipala = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = indexHandler,
    .user_ctx = NULL
  };

  httpd_uri_t rutaActiune = {
    .uri = "/action",
    .method = HTTP_GET,
    .handler = actionHandler,
    .user_ctx = NULL
  };

  httpd_uri_t rutaFlash = {
    .uri = "/flash",
    .method = HTTP_GET,
    .handler = flashHandler,
    .user_ctx = NULL
  };

  if (httpd_start(&serverCamera, &configurare) == ESP_OK) {

    httpd_register_uri_handler(serverCamera, &rutaPrincipala);
    httpd_register_uri_handler(serverCamera, &rutaActiune);
    httpd_register_uri_handler(serverCamera, &rutaFlash);

    Serial.println("Server web pornit pe portul 80.");
  }

  httpd_config_t configurareFlux = HTTPD_DEFAULT_CONFIG();

  configurareFlux.server_port = 81;
  configurareFlux.ctrl_port = 32769;

  httpd_uri_t rutaFlux = {
    .uri = "/stream",
    .method = HTTP_GET,
    .handler = fluxHandler,
    .user_ctx = NULL
  };

  if (httpd_start(&serverFlux, &configurareFlux) == ESP_OK) {

    httpd_register_uri_handler(serverFlux, &rutaFlux);

    Serial.println("Server video pornit pe portul 81.");
  }
}

// ============================================================
// INITIALIZARE CAMERA
// ============================================================

bool initializeazaCamera() {

  camera_config_t configurare;

  configurare.ledc_channel = LEDC_CHANNEL_0;
  configurare.ledc_timer = LEDC_TIMER_0;

  configurare.pin_d0 = Y2_GPIO_NUM;
  configurare.pin_d1 = Y3_GPIO_NUM;
  configurare.pin_d2 = Y4_GPIO_NUM;
  configurare.pin_d3 = Y5_GPIO_NUM;
  configurare.pin_d4 = Y6_GPIO_NUM;
  configurare.pin_d5 = Y7_GPIO_NUM;
  configurare.pin_d6 = Y8_GPIO_NUM;
  configurare.pin_d7 = Y9_GPIO_NUM;

  configurare.pin_xclk = XCLK_GPIO_NUM;
  configurare.pin_pclk = PCLK_GPIO_NUM;
  configurare.pin_vsync = VSYNC_GPIO_NUM;
  configurare.pin_href = HREF_GPIO_NUM;

#if ESP_ARDUINO_VERSION_MAJOR >= 3

  configurare.pin_sccb_sda = SIOD_GPIO_NUM;
  configurare.pin_sccb_scl = SIOC_GPIO_NUM;

#else

  configurare.pin_sscb_sda = SIOD_GPIO_NUM;
  configurare.pin_sscb_scl = SIOC_GPIO_NUM;

#endif

  configurare.pin_pwdn = PWDN_GPIO_NUM;
  configurare.pin_reset = RESET_GPIO_NUM;

  configurare.xclk_freq_hz = 20000000;
  configurare.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {

    configurare.frame_size = FRAMESIZE_VGA;
    configurare.jpeg_quality = 12;
    configurare.fb_count = 2;

  } else {

    configurare.frame_size = FRAMESIZE_QVGA;
    configurare.jpeg_quality = 15;
    configurare.fb_count = 1;
  }

  esp_err_t rezultat = esp_camera_init(&configurare);

  if (rezultat != ESP_OK) {

    Serial.printf(
      "Eroare initializare camera: 0x%x\n",
      rezultat
    );

    return false;
  }

  Serial.println("Camera initializata cu succes.");

  return true;
}

// ============================================================
// CONECTARE WI-FI
// ============================================================

bool conectareWiFi() {

  WiFi.mode(WIFI_STA);

  WiFi.begin(NUME_RETEA_WIFI, PAROLA_WIFI);

  Serial.print("Conectare la reteaua Wi-Fi");

  unsigned long inceput = millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - inceput < 30000
  ) {

    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() != WL_CONNECTED) {

    Serial.println("\nConectarea Wi-Fi a esuat.");

    return false;
  }

  Serial.println("\nConectare Wi-Fi reusita.");

  Serial.print("Adresa IP: ");
  Serial.println(WiFi.localIP());

  return true;
}

// ============================================================
// TRIMITERE FOTOGRAFIE TELEGRAM
// ============================================================

bool trimiteFotografieTelegram() {

  camera_fb_t *cadru = esp_camera_fb_get();

  if (!cadru) {

    Serial.println("Nu s-a putut captura fotografia.");

    return false;
  }

  WiFiClientSecure clientFoto;

  // Conexiune TLS fara verificarea certificatului.
  // Pentru utilizare expusa pe internet se recomanda
  // configurarea verificarii certificatului CA.

  clientFoto.setInsecure();

  if (!clientFoto.connect("api.telegram.org", 443)) {

    Serial.println("Conectarea la Telegram a esuat.");

    esp_camera_fb_return(cadru);

    return false;
  }

  String delimitator = "----ESP32CAMBoundary";

  String antet =
    "--" + delimitator + "\r\n"
    "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n" +
    String(ID_CHAT_TELEGRAM) + "\r\n" +
    "--" + delimitator + "\r\n"
    "Content-Disposition: form-data; name=\"photo\"; filename=\"camera.jpg\"\r\n"
    "Content-Type: image/jpeg\r\n\r\n";

  String finalCerere =
    "\r\n--" + delimitator + "--\r\n";

  size_t lungimeTotala =
    antet.length() + cadru->len + finalCerere.length();

  clientFoto.println(
    "POST /bot" + String(TOKEN_BOT_TELEGRAM) +
    "/sendPhoto HTTP/1.1"
  );

  clientFoto.println("Host: api.telegram.org");
  clientFoto.println("Connection: close");

  clientFoto.println(
    "Content-Type: multipart/form-data; boundary=" + delimitator
  );

  clientFoto.println(
    "Content-Length: " + String(lungimeTotala)
  );

  clientFoto.println();

  clientFoto.print(antet);

  size_t pozitie = 0;

  while (pozitie < cadru->len) {

    size_t dimensiuneBloc = min(
      (size_t)1024,
      cadru->len - pozitie
    );

    size_t scrisi = clientFoto.write(
      cadru->buf + pozitie,
      dimensiuneBloc
    );

    if (scrisi == 0) {
      break;
    }

    pozitie += scrisi;
  }

  clientFoto.print(finalCerere);

  esp_camera_fb_return(cadru);

  bool succes = false;

  unsigned long inceput = millis();

  while (
    clientFoto.connected() &&
    millis() - inceput < 12000
  ) {

    if (clientFoto.available()) {

      String linie = clientFoto.readStringUntil('\n');

      if (linie.startsWith("HTTP/1.1 200") ||
          linie.startsWith("HTTP/1.0 200")) {

        succes = true;
      }

      if (linie.length() == 1) {
        break;
      }

    } else {

      delay(10);
    }
  }

  clientFoto.stop();

  if (succes) {
    Serial.println("Fotografie trimisa prin Telegram.");
  } else {
    Serial.println("Trimiterea fotografiei nu a fost confirmata.");
  }

  return succes;
}

// ============================================================
// TRATARE COMENZI TELEGRAM
// ============================================================

void proceseazaComenziTelegram(int numarMesaje) {

  for (int i = 0; i < numarMesaje; i++) {

    String idChat = bot.messages[i].chat_id;
    String mesaj = bot.messages[i].text;

    // Acceptam numai comenzile din chat-ul configurat.
    if (idChat != String(ID_CHAT_TELEGRAM)) {
      continue;
    }

    // Eliminam sufixul @nume_bot, daca exista.
    int pozitieAt = mesaj.indexOf('@');

    if (pozitieAt >= 0) {
      mesaj = mesaj.substring(0, pozitieAt);
    }

    Serial.println("Comanda Telegram: " + mesaj);

    if (mesaj == "/start") {

      String raspuns =
        "Sistemul ESP32-CAM este activ.\n\n"
        "Comenzi disponibile:\n"
        "/photo - Captura fotografie\n"
        "/flash - Pornire/Oprire flash\n"
        "/flash 0-255 - Reglare luminozitate\n"
        "/status - Starea sistemului";

      bot.sendMessage(idChat, raspuns, "");

    } else if (mesaj == "/photo") {

      bot.sendMessage(
        idChat,
        "Se captureaza fotografia...",
        ""
      );

      bool rezultat = trimiteFotografieTelegram();

      if (!rezultat) {
        bot.sendMessage(
          idChat,
          "Fotografia nu a putut fi trimisa.",
          ""
        );
      }

    } else if (mesaj.startsWith("/flash")) {

      int spatiu = mesaj.indexOf(' ');

      if (spatiu > 0) {

        int valoare = mesaj.substring(spatiu + 1).toInt();

        seteazaFlash(valoare);

      } else {

        if (luminozitateFlash > 0) {
          seteazaFlash(0);
        } else {
          seteazaFlash(255);
        }
      }

      bot.sendMessage(
        idChat,
        "Luminozitate flash: " +
        String(luminozitateFlash) +
        "/255",
        ""
      );

    } else if (mesaj == "/status") {

      String stareWiFi =
        WiFi.status() == WL_CONNECTED ? "Conectat" : "Deconectat";

      String raspuns =
        "Stare sistem ESP32-CAM\n\n"
        "Wi-Fi: " + stareWiFi + "\n" +
        "IP: " + WiFi.localIP().toString() + "\n" +
        "PAN: " + String(pozitiePAN) + " grade\n" +
        "TILT: " + String(pozitieTILT) + " grade\n" +
        "Flash: " + String(luminozitateFlash) + "/255\n" +
        "PIR: " +
        (digitalRead(PIN_PIR) == HIGH ? "Miscare" : "Inactiv");

      bot.sendMessage(idChat, raspuns, "");
    }
  }
}

// ============================================================
// VERIFICARE MESAJE TELEGRAM
// ============================================================

void verificaTelegram() {

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (millis() - ultimaVerificareTelegram < INTERVAL_TELEGRAM) {
    return;
  }

  ultimaVerificareTelegram = millis();

  int numarMesaje = bot.getUpdates(
    bot.last_message_received + 1
  );

  while (numarMesaje) {

    proceseazaComenziTelegram(numarMesaje);

    numarMesaje = bot.getUpdates(
      bot.last_message_received + 1
    );
  }
}

// ============================================================
// DETECTARE MISCARE PIR
// ============================================================

void verificaSenzorPIR() {

  bool miscareDetectata =
    digitalRead(PIN_PIR) == HIGH;

  if (
    miscareDetectata &&
    !stareAnterioaraPIR &&
    millis() - ultimaAlarmaPIR >= INTERVAL_ALARMA_PIR
  ) {

    ultimaAlarmaPIR = millis();

    Serial.println("Miscare detectata!");

    bot.sendMessage(
      ID_CHAT_TELEGRAM,
      "Alerta! A fost detectata miscare.",
      ""
    );

    trimiteFotografieTelegram();
  }

  stareAnterioaraPIR = miscareDetectata;
}

// ============================================================
// INITIALIZARE SISTEM
// ============================================================

void setup() {

  Serial.begin(115200);
  Serial.setDebugOutput(false);

  delay(500);

  Serial.println();
  Serial.println("Pornire sistem ESP32-CAM...");

  // Dezactivarea detectorului brownout poate ascunde
  // problemele de alimentare. Foloseste o sursa corecta
  // pentru placa si servomotoare.

  // WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  pinMode(PIN_PIR, INPUT);

  // Initializare camera.
  if (!initializeazaCamera()) {
    Serial.println("Camera nu a putut fi initializata.");
    return;
  }

  // Initializare servouri si flash.
  initializeazaServouri();
  initializeazaFlash();

  // Conectare Wi-Fi.
  if (!conectareWiFi()) {
    Serial.println("Verifica datele Wi-Fi din secrets.h.");
    return;
  }

  // Configurare conexiune Telegram.
  // setInsecure nu verifica certificatul serverului.
  clientTelegram.setInsecure();

  // Pornire server web si flux video.
  pornesteServerWeb();

  Serial.println("--------------------------------");
  Serial.println("Sistem pornit cu succes.");
  Serial.print("Interfata web: http://");
  Serial.println(WiFi.localIP());
  Serial.println("Comenzi Telegram: /start /photo /flash /status");
  Serial.println("--------------------------------");
}

// ============================================================
// BUCLE PRINCIPALA
// ============================================================

void loop() {

  if (WiFi.status() != WL_CONNECTED) {

    Serial.println("Conexiune Wi-Fi pierduta. Reconectare...");

    WiFi.reconnect();

    delay(500);
    return;
  }

  verificaSenzorPIR();

  verificaTelegram();

  delay(20);
}
