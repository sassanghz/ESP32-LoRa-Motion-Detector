#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <ESP32Servo.h>
#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>

// ---------------- Pin Configuration ----------------
#define GREEN_LED 22
#define RED_LED   21
#define SERVO_PIN 13
#define EEPROM_SIZE 64
#define IR_SENSOR 36

Servo doorServo;

// ---------------- Wi-Fi Credentials ----------------
const char* ssid = "iPhone";
const char* password = "bingoooo";
String currentUser = "room1";  // Default user; can extend later for multi-user admin

WebServer server(80);

// ---------------- Access Control Variables ----------------
bool credentialsValid = false;
int failedAttempts = 0;
bool lockedOut = false;
unsigned long lockoutStart = 0;
bool accessGranted = false;      // true after successful login
unsigned long lockStartTime = 0; // for lockout timing
bool servoUnlocked = false;      // servo state


// ---------------- TTN OTAA Keys ----------------
static const u1_t PROGMEM DEVEUI[8] = { 0x7F, 0x3D, 0x07, 0xD0, 0x7E, 0xD5, 0xB3, 0x70 };
static const u1_t PROGMEM APPEUI[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static const u1_t PROGMEM APPKEY[16] = { 0x75, 0xA0, 0x8B, 0x8C, 0x13, 0x07, 0xCF, 0x4C, 0xFC, 0x60, 0x8C, 0x22, 0xF1, 0x27, 0x86, 0x78 };

void os_getArtEui(u1_t* buf){ memcpy_P(buf, APPEUI, 8); }
void os_getDevEui(u1_t* buf){ memcpy_P(buf, DEVEUI, 8); }
void os_getDevKey(u1_t* buf){ memcpy_P(buf, APPKEY, 16); }

const lmic_pinmap lmic_pins = {
  .nss = 18,
  .rxtx = LMIC_UNUSED_PIN,
  .rst = 23,
  .dio = {26, 33, 32}
};

static uint8_t mydata[4];
static osjob_t sendjob;
const unsigned TX_INTERVAL = 60;

// ---------------- EEPROM Functions ----------------
void writeDefaultCredentials() {
  String defaultUser = "room1";
  String defaultPass = "door123";

  for (int i = 0; i < defaultUser.length(); ++i)
    EEPROM.write(i, defaultUser[i]);
  EEPROM.write(defaultUser.length(), '\0');

  for (int i = 0; i < defaultPass.length(); ++i)
    EEPROM.write(32 + i, defaultPass[i]);
  EEPROM.write(32 + defaultPass.length(), '\0');

  EEPROM.commit();
  Serial.println("Default credentials stored in EEPROM");
}

String readUserFromEEPROM() {
  String user = "";
  for (int i = 0; i < 32; ++i) {
    char c = EEPROM.read(i);
    if (c == '\0') break;
    user += c;
  }
  return user;
}

String readPassFromEEPROM() {
  String pass = "";
  for (int i = 32; i < 64; ++i) {
    char c = EEPROM.read(i);
    if (c == '\0') break;
    pass += c;
  }
  return pass;
}

// ---------------- Access Control Logic ----------------
void grantAccess() {
  credentialsValid = true;
  accessGranted = true; // enable IR tracking
  lockedOut = false;
  failedAttempts = 0;

  Serial.println("Access Granted: Waiting for person detection...");

  // Indicate ready state
  digitalWrite(GREEN_LED, HIGH);
  digitalWrite(RED_LED, LOW);

  // Send LoRa message
  String message = "Granted|" + currentUser;
  uint8_t payload[50];
  message.getBytes(payload, sizeof(payload));
  LMIC_setTxData2(1, payload, strlen((char*)payload), 0);
  Serial.print("LoRa packet queued: ");
  Serial.println(message);
}

void denyAccess() {
  credentialsValid = false;
  accessGranted = false;
  lockedOut = true;
  lockStartTime = millis();

  digitalWrite(RED_LED, HIGH);
  digitalWrite(GREEN_LED, LOW);
  doorServo.write(0); // locked position

  Serial.println("Access Denied: System locked for 2 minutes.");

  // Send LoRa message
  String message = "Denied|" + currentUser;
  uint8_t payload[50];
  message.getBytes(payload, sizeof(payload));
  LMIC_setTxData2(1, payload, strlen((char*)payload), 0);
  Serial.print("LoRa packet queued: ");
  Serial.println(message);
}


// ---------------- Web Server Logic ----------------
void handleLogin() {
  if (lockedOut && (millis() - lockoutStart < 120000)) {
    String content = "<html><body><h2>System Locked</h2>"
                     "<p>Too many failed attempts.<br>Please wait 2 minutes before trying again.</p></body></html>";
    server.send(200, "text/html", content);
    return;
  } else if (lockedOut) {
    lockedOut = false;
    failedAttempts = 0;
  }

  String storedUser = readUserFromEEPROM();
  String storedPass = readPassFromEEPROM();

  if (server.hasArg("USERNAME") && server.hasArg("PASSWORD")) {
    if (server.arg("USERNAME") == storedUser && server.arg("PASSWORD") == storedPass) {
      grantAccess();
      failedAttempts = 0;
      server.sendHeader("Location", "/success");
      server.send(303);
      return;
    } else {
      failedAttempts++;
      denyAccess();
      if (failedAttempts >= 3) {
        lockedOut = true;
        lockoutStart = millis();
        Serial.println("Device locked for 2 minutes.");
      }
    }
  }

  String content = "<html><body><h2>Room Access Control</h2>"
                   "<form action='/login' method='POST'>"
                   "Username: <input type='text' name='USERNAME'><br>"
                   "Password: <input type='password' name='PASSWORD'><br>"
                   "<input type='submit' value='Open Door'>"
                   "</form></body></html>";
  server.send(200, "text/html", content);
}

void handleSuccess() {
  String content = "<html><body><h2>Access Granted</h2><p>The door has been opened successfully.</p></body></html>";
  server.send(200, "text/html", content);
}

void handleNotFound() {
  server.send(404, "text/plain", "404: Not Found");
}

// ---------------- LoRa Logic ----------------
void do_send(osjob_t* j) {
  if (LMIC.opmode & OP_TXRXPEND) {
    Serial.println(F("OP_TXRXPEND, not sending"));
  } else {
    String message = credentialsValid ? "Access Granted" : "Access Denied";
    uint8_t payload[50];
    message.getBytes(payload, sizeof(payload));
    LMIC_setTxData2(1, payload, strlen((char*)payload), 0);
    Serial.print(F("Packet queued: "));
    Serial.println(message);
  }
}

void onEvent(ev_t ev) {
  Serial.print(os_getTime());
  Serial.print(F(": "));
  switch (ev) {
    case EV_JOINING:   Serial.println(F("EV_JOINING")); break;
    case EV_JOINED:
      Serial.println(F("EV_JOINED"));
      LMIC_setLinkCheckMode(0);
      break;
    case EV_TXCOMPLETE:
      Serial.println(F("EV_TXCOMPLETE"));
      //os_setTimedCallback(&sendjob, os_getTime() + sec2osticks(TX_INTERVAL), do_send);
      break;
    default:
      Serial.print(F("Unknown event: "));
      Serial.println((unsigned)ev);
      break;
  }
}

// ---------------- LMIC Background Task ----------------
TaskHandle_t lmicTaskHandle;
void lmicTask(void *pvParameters) {
  Serial.println(F("Starting LMIC task..."));
  delay(1000);

  SPI.begin(5, 19, 27, 18);
  os_init_ex(&lmic_pins);
  LMIC_reset();
  LMIC_selectSubBand(1);
  LMIC_setClockError(MAX_CLOCK_ERROR / 100);

  do_send(&sendjob);
  while (true) {
    os_runloop_once();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ---------------- Setup ----------------
void setup() {
  Serial.begin(115200);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(IR_SENSOR, INPUT);
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED, LOW);

  doorServo.attach(SERVO_PIN);
  doorServo.write(0);

  EEPROM.begin(EEPROM_SIZE);
  //writeDefaultCredentials(); // remove/comment after first upload if you want to keep stored creds

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Web routes
  server.on("/", []() { server.sendHeader("Location", "/login"); server.send(303); });
  server.on("/login", handleLogin);
  server.on("/success", handleSuccess);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started.");

  // Start LoRa task
  xTaskCreatePinnedToCore(lmicTask, "lmicTask", 8192, NULL, 1, &lmicTaskHandle, 0);
}

// ---------------- Loop ----------------
void loop() {
  server.handleClient();

  // Handle lockout timer
  if (lockedOut && (millis() - lockStartTime >= 120000)) { // 2 minutes
    lockedOut = false;
    digitalWrite(RED_LED, LOW);
    Serial.println("System unlocked after 2 minutes.");
  }

  // Only monitor IR sensor if access was granted
  if (accessGranted && !lockedOut) {
    int sensorValue = analogRead(IR_SENSOR);
    int threshold = 2000; // adjust after calibration
    Serial.print("IR Sensor Value: ");
    Serial.println(sensorValue);

    if (sensorValue < threshold && !servoUnlocked) {
      // Object detected and door is locked — unlock it
      Serial.println("Person detected — unlocking door...");
      doorServo.write(90);
      servoUnlocked = true;
      digitalWrite(GREEN_LED, HIGH);
    } 
    else if (sensorValue > threshold && servoUnlocked) {
      // No person — lock the door again
      Serial.println("No person detected — locking door...");
      doorServo.write(0);
      servoUnlocked = false;
      digitalWrite(GREEN_LED, LOW);
    }
  }

  delay(200);
}