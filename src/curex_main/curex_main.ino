
#include <DHT.h>
#include <ESP8266WiFi.h>     
#include <ESP8266WebServer.h> 

const char* ssid     = "Redmi9T";
const char* password = "123456789";


#define DHT_PIN        2      
#define DHT_TYPE       DHT11
#define LM35_PIN       A0     
#define FSR_PIN        A1     
#define FLEX_PIN       A2     
#define PELTIER_HEAT   5      
#define PELTIER_COOL   6      
#define STATUS_LED     13     

// Therapy thresholds
const float HEAT_THRESHOLD_LOW  = 36.0;  // Below this → activate heat
const float HEAT_TARGET         = 40.0;  // Heat therapy target 
const float COOL_THRESHOLD_HIGH = 37.5;  // Above this → activate cooling
const float COOL_TARGET         = 17.5;  // Cold therapy target 
const int   SWELLING_THRESHOLD  = 600;   // FSR reading above this = swelling detected
const int   FLEX_THRESHOLD      = 500;   // Flex reading above this = stiffness detected


DHT dht(DHT_PIN, DHT_TYPE);
ESP8266WebServer server(80);

float ambientTemp    = 0;
float ambientHumid   = 0;
float bodyTemp       = 0;
int   swellingVal    = 0;
int   flexVal        = 0;
String therapyMode   = "IDLE";


void setup() {
  Serial.begin(115200);

  pinMode(PELTIER_HEAT, OUTPUT);
  pinMode(PELTIER_COOL, OUTPUT);
  pinMode(STATUS_LED,   OUTPUT);

  analogWrite(PELTIER_HEAT, 0);
  analogWrite(PELTIER_COOL, 0);

  dht.begin();

  Serial.println("CUREX booting...");
  connectWiFi();
  setupRoutes();
  server.begin();
  Serial.println("Server started at: http://" + WiFi.localIP().toString());
}


void loop() {
  readSensors();
  runTherapyLogic();
  server.handleClient();
  delay(500);
}

void connectWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected! IP: " + WiFi.localIP().toString());
    digitalWrite(STATUS_LED, HIGH);
  } else {
    Serial.println("\nWiFi failed — running in offline mode.");
  }
}

void readSensors() {
  // DHT11 — ambient temperature & humidity
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if (!isnan(h) && !isnan(t)) {
    ambientTemp  = t;
    ambientHumid = h;
  }

  // LM35 — body temperature (10mV/°C, 5V reference)
  int lm35Raw = analogRead(LM35_PIN);
  bodyTemp = (lm35Raw * 5.0 / 1023.0) * 100.0;

  // FSR — swelling / pressure
  swellingVal = analogRead(FSR_PIN);

  // Flex sensor — finger stiffness
  flexVal = analogRead(FLEX_PIN);

  Serial.printf("[Sensors] Body: %.1f°C | Ambient: %.1f°C | Humidity: %.1f%% | Swelling: %d | Flex: %d\n",
                bodyTemp, ambientTemp, ambientHumid, swellingVal, flexVal);
}


void runTherapyLogic() {
  bool highSwelling = swellingVal > SWELLING_THRESHOLD;
  bool highStiffness = flexVal > FLEX_THRESHOLD;

  if (highSwelling) {
    // Swelling detected → cold therapy
    activateCooling(200);
    therapyMode = "COOLING";
    Serial.println("[Therapy] Swelling detected → COOLING");

  } else if (bodyTemp < HEAT_THRESHOLD_LOW || highStiffness) {
    // Low temp or stiffness → heat therapy
    activateHeating(200);
    therapyMode = "HEATING";
    Serial.println("[Therapy] Stiffness/low temp → HEATING");

  } else if (bodyTemp > COOL_THRESHOLD_HIGH) {
    // Fever-like state → cooling
    activateCooling(150);
    therapyMode = "COOLING";
    Serial.println("[Therapy] High body temp → COOLING");

  } else {
    // All normal
    stopTherapy();
    therapyMode = "IDLE";
  }
}

void activateHeating(int pwmValue) {
  analogWrite(PELTIER_COOL, 0);
  analogWrite(PELTIER_HEAT, pwmValue);
}

void activateCooling(int pwmValue) {
  analogWrite(PELTIER_HEAT, 0);
  analogWrite(PELTIER_COOL, pwmValue);
}

void stopTherapy() {
  analogWrite(PELTIER_HEAT, 0);
  analogWrite(PELTIER_COOL, 0);
}


void setupRoutes() {

  server.on("/data", HTTP_GET, []() {
    String json = "{";
    json += "\"bodyTemp\":"    + String(bodyTemp, 1)    + ",";
    json += "\"ambientTemp\":" + String(ambientTemp, 1) + ",";
    json += "\"humidity\":"    + String(ambientHumid, 1)+ ",";
    json += "\"swelling\":"    + String(swellingVal)    + ",";
    json += "\"flex\":"        + String(flexVal)        + ",";
    json += "\"therapy\":\""   + therapyMode            + "\"";
    json += "}";
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
  });

  // Health check
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/plain", "CUREX online. Visit /data for sensor readings.");
  });
}
