/*******************************************************************************
 * SMART HOME SAFETY & ENVIRONMENTAL MONITORING SYSTEM (ESP32-S3)
 * Hardware: ESP32-S3 Dev Module
 * Sensors: MQ-2 Gas (GPIO 4), Temp Sensor (GPIO 5), Ultrasonic (GPIO 6/7), IR (GPIO 15)
 * Actuators: Servo Motor (GPIO 16 - EXTERNALLY POWERED), Red LED (GPIO 18), Green LED (GPIO 8), Buzzer (GPIO 17)
 *******************************************************************************/

#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>

// =============================================================================
// 1. WI-FI CREDENTIALS
// =============================================================================
const char* WIFI_SSID = "Oneplus Nord CE4 5G";
const char* WIFI_PASS = "9gpjhvji";

// =============================================================================
// 2. HARDWARE PIN DEFINITIONS (ESP32-S3)
// =============================================================================
#define MQ2_PIN        4    // ADC1_CH3
#define TEMP_PIN       5    // ADC1_CH4 (Analog input from LM35 Temp)
#define TRIG_PIN       6    // Ultrasonic HC-SR04 Trigger pin
#define ECHO_PIN       7    // Ultrasonic HC-SR04 Echo pin
#define IR_PIN         15   // IR module Digital Output pin
#define SERVO_PIN      16   // Servo Motor signal PWM pin
#define BUZZER_PIN     17   // Buzzer signal pin
#define RED_LED_PIN    18   // Red LED pin
#define GREEN_LED_PIN  8    // Green LED pin

// Safety Thresholds
const int GAS_THRESHOLD = 800;             // Gas raw ADC threshold (0 - 4095)
const float TEMP_THRESHOLD = 45.0;         // Temperature threshold in °C
const int DISTANCE_THRESHOLD_CM = 15;      // Distance threshold in cm
const unsigned long GAS_WARMUP_MS = 20000; // MQ-2 warm-up time (20s)

// Global Objects & Variables
Servo doorServo;
WebServer server(80);

int gasLevel = 0;
float tempC = 0.0;
float distanceCM = 0.0;
bool isDark = false;
bool isHazard = false;
bool manualGateOpen = false;
bool gasReady = false;

unsigned long gasStartTime = 0;
unsigned long lastIpPrintTime = 0; // Timer for periodic IP printing

// =============================================================================
// 3. HELPER FUNCTIONS
// =============================================================================
float readDistanceCM() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duration == 0) return 400.0;
  return (duration * 0.0343) / 2.0;
}

float readTemperature() {
  int rawAnalog = analogRead(TEMP_PIN);
  float voltage = (rawAnalog / 4095.0) * 3.3;
  return voltage * 100.0; // LM35 outputs 10mV/°C
}

void triggerBuzzer(bool enable) {
  if (enable) {
    tone(BUZZER_PIN, 1800);
  } else {
    noTone(BUZZER_PIN);
  }
}

// =============================================================================
// 4. WEB SERVER HANDLERS
// =============================================================================
void handleData() {
  String json = "{";
  json += "\"temp\":" + String(tempC, 1) + ",";
  json += "\"gas\":" + String(gasLevel) + ",";
  json += "\"gasReady\":" + String(gasReady ? "true" : "false") + ",";
  json += "\"dist\":" + String(distanceCM, 1) + ",";
  json += "\"dark\":" + String(isDark ? "true" : "false") + ",";
  json += "\"hazard\":" + String(isHazard ? "true" : "false") + ",";
  json += "\"gate\":" + String((distanceCM < DISTANCE_THRESHOLD_CM || manualGateOpen) ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}

void handleToggleGate() {
  manualGateOpen = !manualGateOpen;
  server.send(200, "text/plain", manualGateOpen ? "OPEN" : "CLOSED");
}

void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32-S3 Smart Home Node</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: sans-serif; }
    body { background: #0f172a; color: #f8fafc; display: flex; flex-direction: column; align-items: center; min-height: 100vh; padding: 25px 15px; }
    header { text-align: center; margin-bottom: 25px; }
    header h1 { font-size: 1.8rem; color: #38bdf8; }
    .status-banner { width: 100%; max-width: 500px; padding: 15px; border-radius: 12px; margin-bottom: 20px; display: flex; justify-content: space-between; background: #1e293b; }
    .status-banner.safe { border: 1px solid #10b981; }
    .status-banner.alarm { border: 1px solid #ef4444; background: rgba(239,68,68,0.2); }
    .grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 15px; width: 100%; max-width: 500px; }
    .card { background: #1e293b; padding: 18px; border-radius: 12px; border: 1px solid #334155; }
    .card h3 { font-size: 0.75rem; text-transform: uppercase; color: #94a3b8; }
    .card .value { font-size: 1.8rem; font-weight: bold; margin-top: 5px; }
    .btn { background: #0284c7; color: white; border: none; padding: 12px 20px; border-radius: 8px; cursor: pointer; font-weight: bold; }
  </style>
</head>
<body>
  <header>
    <h1>Smart Safety Dashboard</h1>
    <p>ESP32-S3 IoT Environmental Monitor</p>
    <p style="margin-top:8px; color:#38bdf8; font-weight:bold;">
      Dashboard: http://192.168.4.1
    </p>
  </header>

  <div id="banner" class="status-banner safe">
    <div>
      <div>SYSTEM STATUS</div>
      <div id="statusText" style="font-weight:bold;">SYSTEM SAFE</div>
    </div>
    <div id="gateBadge">GATE: CLOSED</div>
  </div>

  <div class="grid">
    <div class="card">
      <h3>Temperature</h3>
      <div class="value"><span id="temp">--</span> °C</div>
    </div>
    <div class="card">
      <h3>Gas Sensor</h3>
      <div class="value"><span id="gas">--</span></div>
      <div id="gasStatus" style="font-size:0.7rem; color:#94a3b8;">Warming up...</div>
    </div>
    <div class="card">
      <h3>Proximity</h3>
      <div class="value"><span id="dist">--</span> cm</div>
    </div>
    <div class="card">
      <h3>Ambient Light</h3>
      <div class="value" id="lightText">--</div>
    </div>
  </div>

  <div style="width:100%; max-width:500px; margin-top:20px; background:#1e293b; padding:20px; border-radius:12px; display:flex; justify-content:space-between; align-items:center;">
    <div>
      <h4>Access Gate Control</h4>
      <p style="font-size:0.75rem; color:#94a3b8;">Manual Servo Override</p>
    </div>
    <button class="btn" onclick="toggleGate()">Toggle Gate</button>
  </div>

  <script>
    function updateData() {
      fetch('/data')
        .then(res => res.json())
        .then(data => {
          document.getElementById('temp').innerText = data.temp;
          document.getElementById('gas').innerText = data.gasReady ? data.gas : '--';
          document.getElementById('gasStatus').innerText = data.gasReady ? 'Live reading' : 'Warming up...';
          document.getElementById('dist').innerText = data.dist;
          document.getElementById('lightText').innerText = data.dark ? "Dark" : "Bright";
          document.getElementById('gateBadge').innerText = data.gate ? "GATE: OPEN" : "GATE: CLOSED";

          let banner = document.getElementById('banner');
          let statusText = document.getElementById('statusText');
          if (data.hazard) {
            banner.className = "status-banner alarm";
            statusText.innerText = "HAZARD DETECTED!";
          } else {
            banner.className = "status-banner safe";
            statusText.innerText = "SYSTEM SAFE";
          }
        });
    }
    setInterval(updateData, 1000);
    function toggleGate() { fetch('/toggleGate'); }
  </script>
</body>
</html>
)rawliteral";
  server.send(200, "text/html", html);
}

// =============================================================================
// 5. SETUP FUNCTION
// =============================================================================
void setup() {
  Serial.begin(115200);

  // Wait up to 5 seconds for USB CDC Serial Monitor to open
  unsigned long startWait = millis();
  while (!Serial && (millis() - startWait < 5000)) {
    delay(10);
  }
  delay(1000);

  Serial.println("\n\n====================================");
  Serial.println("  ESP32-S3 SYSTEM INITIALIZING...   ");
  Serial.println("====================================");

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(IR_PIN, INPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  // Servo Setup
  ESP32PWM::allocateTimer(0);
  doorServo.setPeriodHertz(50);
  doorServo.attach(SERVO_PIN, 500, 2400);
  doorServo.write(0);

  // =============================================================================
  // WI-FI + GUARANTEED DASHBOARD ACCESS
  // =============================================================================
  // The ESP32-S3 creates its own Access Point AND tries your normal Wi-Fi.
  // Therefore an IP address is always available even if the phone hotspot fails.
  // AP dashboard: http://192.168.4.1

  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);

  // Start ESP32-S3 Access Point
  IPAddress apIP(192, 168, 4, 1);
  IPAddress apGateway(192, 168, 4, 1);
  IPAddress apSubnet(255, 255, 255, 0);

  WiFi.softAPConfig(apIP, apGateway, apSubnet);

  const char* AP_SSID = "ESP32-S3-SmartHome";
  const char* AP_PASS = "12345678";

  bool apStarted = WiFi.softAP(AP_SSID, AP_PASS);

  Serial.println();
  Serial.println("==========================================");
  Serial.println("       ESP32-S3 SMART HOME SERVER");
  Serial.println("==========================================");

  if (apStarted) {
    Serial.println("ESP32 ACCESS POINT: STARTED");
    Serial.print("AP SSID: ");
    Serial.println(AP_SSID);
    Serial.print("AP PASSWORD: ");
    Serial.println(AP_PASS);
    Serial.print("DASHBOARD IP: http://");
    Serial.println(WiFi.softAPIP());
    Serial.println("------------------------------------------");
  } else {
    Serial.println("ERROR: ESP32 ACCESS POINT FAILED!");
  }

  // Also try connecting to the configured Wi-Fi/hotspot
  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long wifiStart = millis();
  const unsigned long WIFI_TIMEOUT = 15000;

  while (WiFi.status() != WL_CONNECTED &&
         millis() - wifiStart < WIFI_TIMEOUT) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("NORMAL WI-FI: CONNECTED");
    Serial.print("NORMAL WI-FI IP: http://");
    Serial.println(WiFi.localIP());
    Serial.print("RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    Serial.println("NORMAL WI-FI: NOT CONNECTED");
    Serial.println("Use the ESP32-S3 Access Point instead.");
  }

  // Web Server Routes
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/toggleGate", handleToggleGate);
  server.begin();

  Serial.println("------------------------------------------");
  Serial.println("WEB SERVER: STARTED");
  Serial.print("ALWAYS AVAILABLE DASHBOARD: http://");
  Serial.println(WiFi.softAPIP());

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("LAN DASHBOARD: http://");
    Serial.println(WiFi.localIP());
  }

  Serial.println("==========================================");
  Serial.println();

  gasStartTime = millis();
  Serial.println("MQ-2 warming up (20s) before hazard logic activates...");
}

// =============================================================================
// 6. MAIN LOOP
// =============================================================================
void loop() {
  server.handleClient();

  if (!gasReady && (millis() - gasStartTime >= GAS_WARMUP_MS)) {
    gasReady = true;
    Serial.println("MQ-2 ready — hazard logic active.");
  }

  // PRINT DASHBOARD IP EVERY 5 SECONDS
  // AP IP is always available, even if normal Wi-Fi is disconnected.
  if (millis() - lastIpPrintTime > 5000) {
    lastIpPrintTime = millis();

    Serial.print("DASHBOARD AP: http://");
    Serial.println(WiFi.softAPIP());

    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("DASHBOARD LAN: http://");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("Wi-Fi status: disconnected (AP dashboard still active)");
      WiFi.reconnect();
    }
  }

  gasLevel = analogRead(MQ2_PIN);
  tempC = readTemperature();
  distanceCM = readDistanceCM();
  
  isDark = (digitalRead(IR_PIN) == LOW);

  isHazard = (tempC > TEMP_THRESHOLD) || (gasReady && gasLevel > GAS_THRESHOLD);

  if ((distanceCM < DISTANCE_THRESHOLD_CM || manualGateOpen) && !isHazard) {
    doorServo.write(90);
  } else {
    doorServo.write(0);
  }

  if (isHazard) {
    digitalWrite(RED_LED_PIN, HIGH);
    digitalWrite(GREEN_LED_PIN, LOW);
    triggerBuzzer(true);
  } else {
    digitalWrite(RED_LED_PIN, LOW);
    triggerBuzzer(false);

    if (isDark) {
      digitalWrite(GREEN_LED_PIN, HIGH);
    } else {
      digitalWrite(GREEN_LED_PIN, LOW);
    }
  }

  delay(150);
}
