/*******************************************************************************
 * SMART HOME SAFETY & ENVIRONMENTAL MONITORING SYSTEM (ATTRACTIVE DASHBOARD)
 * Hardware: ESP32 DevKit V1
 * Sensors: MQ-2 Gas (GPIO 33), Temp Sensor (GPIO 35), Ultrasonic (GPIO 5/18), IR (GPIO 19)
 * Actuators: Servo Motor (GPIO 13 - EXTERNALLY POWERED), Red LED (GPIO 4), Green LED (GPIO 2), Buzzer (GPIO 12)
 *
 * IMPORTANT: Power the servo from a separate 5V supply, NOT the ESP32's 5V pin,
 * to avoid brownout resets. Share GND between the external supply and ESP32.
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
// 2. HARDWARE PIN DEFINITIONS
// =============================================================================
#define MQ2_PIN        33   // ADC1_CH5
#define TEMP_PIN       35   // Analog input from Temperature sensor (LM35)
#define TRIG_PIN       5    // Ultrasonic HC-SR04 Trigger pin
#define ECHO_PIN       18   // Ultrasonic HC-SR04 Echo pin
#define IR_PIN         19   // IR module Digital Output pin
#define SERVO_PIN      13   // Servo Motor signal pin
#define GREEN_LED_PIN  2    // Green LED pin
#define RED_LED_PIN    4    // Red LED pin
#define BUZZER_PIN     12   // Buzzer signal pin

// Safety Thresholds
const int GAS_THRESHOLD = 800;          // Gas raw ADC threshold (0 - 4095)
const float TEMP_THRESHOLD = 45.0;      // Temperature threshold in °C
const int DISTANCE_THRESHOLD_CM = 15;   // Distance threshold in cm
const unsigned long GAS_WARMUP_MS = 20000; // MQ-2 warm-up time before trusting readings

// Global Objects & States
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
    tone(BUZZER_PIN, 1800); // Send 1.8kHz frequency tone
  } else {
    noTone(BUZZER_PIN);
  }
}

// =============================================================================
// 4. WEB SERVER HANDLERS
// =============================================================================

// Send JSON Data for smooth AJAX updating
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

// Toggle Gate position via Web Interface
void handleToggleGate() {
  manualGateOpen = !manualGateOpen;
  server.send(200, "text/plain", manualGateOpen ? "OPEN" : "CLOSED");
}

// Modern Web UI HTML/CSS
void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32 Smart Home Node</title>
  <link href="https://fonts.googleapis.com/css2?family=Inter:wght@300;400;600;700&display=swap" rel="stylesheet">
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: 'Inter', sans-serif; }
    body { background: #0f172a; color: #f8fafc; display: flex; flex-direction: column; align-items: center; min-height: 100vh; padding: 25px 15px; }
    
    header { text-align: center; margin-bottom: 25px; }
    header h1 { font-size: 1.8rem; font-weight: 700; color: #38bdf8; letter-spacing: -0.5px; }
    header p { font-size: 0.85rem; color: #94a3b8; margin-top: 4px; }

    .status-banner { width: 100%; max-width: 500px; padding: 15px 20px; border-radius: 16px; margin-bottom: 20px; display: flex; align-items: center; justify-content: space-between; background: #1e293b; border: 1px solid #334155; box-shadow: 0 10px 25px -5px rgba(0, 0, 0, 0.3); transition: all 0.3s ease; }
    .status-banner.safe { border-color: #10b981; background: rgba(16, 185, 129, 0.1); }
    .status-banner.alarm { border-color: #ef4444; background: rgba(239, 68, 68, 0.15); animation: pulse 1.5s infinite; }
    
    @keyframes pulse {
      0% { box-shadow: 0 0 0 0 rgba(239, 68, 68, 0.4); }
      70% { box-shadow: 0 0 0 12px rgba(239, 68, 68, 0); }
      100% { box-shadow: 0 0 0 0 rgba(239, 68, 68, 0); }
    }

    .status-title { font-size: 0.75rem; text-transform: uppercase; color: #94a3b8; font-weight: 600; letter-spacing: 0.5px; }
    .status-val { font-size: 1.1rem; font-weight: 700; margin-top: 2px; }
    .status-banner.safe .status-val { color: #34d399; }
    .status-banner.alarm .status-val { color: #f87171; }

    .grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 15px; width: 100%; max-width: 500px; }
    .card { background: #1e293b; border: 1px solid #334155; padding: 18px; border-radius: 16px; box-shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.2); position: relative; overflow: hidden; }
    .card h3 { font-size: 0.75rem; text-transform: uppercase; color: #94a3b8; letter-spacing: 0.5px; font-weight: 600; }
    .card .value { font-size: 1.8rem; font-weight: 700; margin: 10px 0 6px 0; color: #f8fafc; }
    .card .unit { font-size: 0.9rem; color: #64748b; font-weight: 400; }
    .card .subtext { font-size: 0.7rem; color: #64748b; margin-top: 2px; }
    
    .progress-bar { width: 100%; height: 6px; background: #334155; border-radius: 3px; overflow: hidden; margin-top: 8px; }
    .progress-fill { height: 100%; width: 0%; background: #38bdf8; transition: width 0.4s ease; }

    .control-panel { width: 100%; max-width: 500px; margin-top: 20px; background: #1e293b; border: 1px solid #334155; border-radius: 16px; padding: 20px; display: flex; align-items: center; justify-content: space-between; }
    .control-info h4 { font-size: 0.95rem; font-weight: 600; color: #f8fafc; }
    .control-info p { font-size: 0.75rem; color: #94a3b8; margin-top: 2px; }
    
    .btn { background: #0284c7; color: #ffffff; border: none; padding: 12px 20px; font-size: 0.9rem; font-weight: 600; border-radius: 10px; cursor: pointer; transition: all 0.2s ease; box-shadow: 0 4px 12px rgba(2, 132, 199, 0.3); }
    .btn:hover { background: #0369a1; }
    .btn:active { transform: scale(0.96); }

    .badge { display: inline-block; padding: 4px 8px; border-radius: 6px; font-size: 0.7rem; font-weight: 700; text-transform: uppercase; margin-top: 6px; }
    .badge-dark { background: rgba(99, 102, 241, 0.2); color: #818cf8; }
    .badge-bright { background: rgba(251, 191, 36, 0.2); color: #fbbf24; }
    .badge-warmup { background: rgba(148, 163, 184, 0.2); color: #94a3b8; }
  </style>
</head>
<body>

  <header>
    <h1>Smart Safety Dashboard</h1>
    <p>ESP32 IoT Environmental Monitor</p>
  </header>

  <div id="banner" class="status-banner safe">
    <div>
      <div class="status-title">System Health Status</div>
      <div id="statusText" class="status-val">SYSTEM SAFE</div>
    </div>
    <div id="gateBadge" class="badge badge-dark">GATE: CLOSED</div>
  </div>

  <div class="grid">
    <div class="card">
      <h3>Temperature</h3>
      <div class="value"><span id="temp">--</span><span class="unit"> °C</span></div>
      <div class="progress-bar"><div id="tempBar" class="progress-fill" style="background: #f43f5e;"></div></div>
    </div>

    <div class="card">
      <h3>Gas Sensor</h3>
      <div class="value"><span id="gas">--</span></div>
      <div id="gasStatus" class="subtext">Warming up...</div>
      <div class="progress-bar"><div id="gasBar" class="progress-fill" style="background: #fb923c;"></div></div>
    </div>

    <div class="card">
      <h3>Proximity</h3>
      <div class="value"><span id="dist">--</span><span class="unit"> cm</span></div>
      <div class="progress-bar"><div id="distBar" class="progress-fill" style="background: #38bdf8;"></div></div>
    </div>

    <div class="card">
      <h3>Ambient Light</h3>
      <div class="value" style="font-size: 1.2rem; margin-top: 15px;" id="lightText">--</div>
      <span id="lightBadge" class="badge badge-bright">DAYTIME</span>
    </div>
  </div>

  <div class="control-panel">
    <div class="control-info">
      <h4>Access Gate Control</h4>
      <p>Manual Servo Barrier override</p>
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
          
          // Update progress bars
          document.getElementById('tempBar').style.width = Math.min((data.temp / 80) * 100, 100) + '%';
          document.getElementById('gasBar').style.width = data.gasReady ? Math.min((data.gas / 4095) * 100, 100) + '%' : '0%';
          document.getElementById('distBar').style.width = Math.min((data.dist / 100) * 100, 100) + '%';

          // Light level update
          let lightText = document.getElementById('lightText');
          let lightBadge = document.getElementById('lightBadge');
          if (data.dark) {
            lightText.innerText = "Dark";
            lightBadge.innerText = "NIGHT TIME";
            lightBadge.className = "badge badge-dark";
          } else {
            lightText.innerText = "Bright";
            lightBadge.innerText = "DAY TIME";
            lightBadge.className = "badge badge-bright";
          }

          // Gate status update
          document.getElementById('gateBadge').innerText = data.gate ? "GATE: OPEN" : "GATE: CLOSED";

          // Hazard Banner update
          let banner = document.getElementById('banner');
          let statusText = document.getElementById('statusText');
          if (data.hazard) {
            banner.className = "status-banner alarm";
            statusText.innerText = "CRITICAL HAZARD DETECTED!";
          } else {
            banner.className = "status-banner safe";
            statusText.innerText = "SYSTEM SAFE & SECURE";
          }
        });
    }

    setInterval(updateData, 1000);

    function toggleGate() {
      fetch('/toggleGate');
    }
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

  // Configure Analog ADC Resolution and Attenuation for GPIO 33 & 35
  analogReadResolution(12);
  analogSetPinAttenuation(MQ2_PIN, ADC_11db);
  analogSetPinAttenuation(TEMP_PIN, ADC_11db);

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

  // Connect Wi-Fi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\n------------------------------------");
  Serial.println("Wi-Fi Connected!");
  Serial.print("Open Web Dashboard at: http://");
  Serial.println(WiFi.localIP());
  Serial.println("------------------------------------");

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/toggleGate", handleToggleGate);
  server.begin();

  gasStartTime = millis();
  Serial.println("MQ-2 warming up (20s) before hazard logic activates...");
}

// =============================================================================
// 6. MAIN LOOP
// =============================================================================
void loop() {
  server.handleClient();

  // Check if MQ-2 warm-up period has elapsed
  if (!gasReady && (millis() - gasStartTime >= GAS_WARMUP_MS)) {
    gasReady = true;
    Serial.println("MQ-2 ready — hazard logic now includes gas readings.");
  }

  // Read Sensors
  gasLevel = analogRead(MQ2_PIN);
  tempC = readTemperature();
  distanceCM = readDistanceCM();
  
  // IR active low detection
  isDark = (digitalRead(IR_PIN) == LOW);

  // Hazard State Evaluation (gas only counted once sensor has warmed up)
  isHazard = (tempC > TEMP_THRESHOLD) || (gasReady && gasLevel > GAS_THRESHOLD);

  // Servo Gate Automation
  if ((distanceCM < DISTANCE_THRESHOLD_CM || manualGateOpen) && !isHazard) {
    doorServo.write(90);
  } else {
    doorServo.write(0);
  }

  // Visual & Audio Indicator Logic
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
