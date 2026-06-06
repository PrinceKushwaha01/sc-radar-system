#include <Servo.h>
#include <WiFiS3.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ========== CONFIGURATION ==========
#define VERSION "SCIENTI CREATION Radar 50cm"
#define MAX_DISTANCE 50  // Changed from 400 to 50cm
#define MIN_DISTANCE 2
#define SERVO_MIN 0
#define SERVO_MAX 180

// OLED Configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDR 0x3C

// WiFi Configuration - CHANGE THESE!
const char* ssid = "YOUR WIFI SSID";
const char* password = "YOUR WIFI PASSWORD";

// Server Configuration
WiFiServer server(80);
WiFiClient client;

// Hardware Pins
const int SERVO_PIN = 6;
const int TRIG_PIN = 9;
const int ECHO_PIN = 10;
const int LED_PIN = LED_BUILTIN;

// Components
Servo radarServo;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Radar Configuration
struct RadarConfig {
  int minAngle = 0;
  int maxAngle = 180;      // Full 180° coverage
  int stepSize = 2;
  int scanSpeed = 30;
  bool autoMode = true;
  bool alertsEnabled = true;
  float alertDistance = 20.0;  // Alert at 20cm
  bool oledEnabled = true;
  int oledRefreshRate = 500;  // ms
};

RadarConfig config;

// Radar State
struct RadarState {
  int currentAngle = 0;
  int sweepDirection = 1;
  bool isScanning = true;
  unsigned long lastScanTime = 0;
  unsigned long lastOledUpdate = 0;
  unsigned long startTime = 0;
  int scanCount = 0;
  int wifiStrength = 0;
  int objectsDetected = 0;
  String systemStatus = "Initializing";
  String ipAddress = "0.0.0.0";
  bool alertActive = false;
  float lastDistance = 0.0;
};

RadarState state;

// Object Structure
struct DetectedObject {
  int angle;
  float distance;
  unsigned long timestamp;
  byte confidence;
};

#define MAX_OBJECTS 10
DetectedObject objects[MAX_OBJECTS];
int objectCount = 0;

// Data Storage
String jsonBuffer = "";

// ========== SETUP ==========
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n" + String(VERSION));
  Serial.println("==================================");
  Serial.println("Configuration: 180° Scan, 50cm Range");
  Serial.println("OLED Display: 0.96-inch 128x64");
  Serial.println("==================================");
  
  // Initialize OLED
  initializeOLED();
  
  // Initialize GPIO
  pinMode(LED_PIN, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);
  
  // Initialize Servo
  radarServo.attach(SERVO_PIN);
  radarServo.write(0);
  delay(500);
  
  // Initialize WiFi
  initializeWiFi();
  
  // Start Web Server
  server.begin();
  state.ipAddress = WiFi.localIP().toString();
  
  // Display WiFi info on OLED
  displayWiFiInfo();
  delay(2000);
  
  // Calibration
  performCalibration();
  
  // Startup animation
  startupAnimation();
  
  state.startTime = millis();
  state.systemStatus = "Ready";
  
  // Initial OLED display
  updateOLED();
  
  Serial.println("\n=== RADAR SYSTEM READY ===");
  Serial.print("OLED Display: ");
  Serial.println(config.oledEnabled ? "Enabled" : "Disabled");
  Serial.print("Access via: http://");
  Serial.println(WiFi.localIP());
}

// ========== OLED INITIALIZATION ==========
void initializeOLED() {
  if(!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("SSD1306 allocation failed"));
    config.oledEnabled = false;
    return;
  }
  
  config.oledEnabled = true;
  
  // Clear display
  display.clearDisplay();
  
  // Display startup screen
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("SCIENTI CREATION"));
  display.setTextSize(2);
  display.setCursor(0, 16);
  display.println(F("RADAR"));
  display.setTextSize(1);
  display.setCursor(0, 40);
  display.println(F("50cm Range"));
  display.setCursor(0, 52);
  display.println(F("180° Scanning"));
  display.display();
  
  delay(2000);
  Serial.println("✓ OLED Initialized");
}

// ========== OLED DISPLAY FUNCTIONS ==========
void updateOLED() {
  if (!config.oledEnabled || millis() - state.lastOledUpdate < config.oledRefreshRate) {
    return;
  }
  
  state.lastOledUpdate = millis();
  
  display.clearDisplay();
  
  // Display mode
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  // Line 1: System Status
  display.setCursor(0, 0);
  if (state.alertActive) {
   
    display.print("!ALERT! ");
    display.setTextColor(SSD1306_WHITE);
  } else {
    display.print(state.isScanning ? "SCANNING" : "PAUSED ");
  }
  
  // Line 2: Angle and Distance
  display.setCursor(0, 12);
  display.print("ANG:");
  display.print(state.currentAngle);
  display.print("°");
  
  display.setCursor(70, 12);
  display.print("DST:");
  if (state.lastDistance > 0) {
    display.print(state.lastDistance, 1);
  } else {
    display.print("---");
  }
  display.print("cm");
  
  // Line 3: Objects and Scan Count
  display.setCursor(0, 24);
  display.print("OBJ:");
  display.print(objectCount);
  
  display.setCursor(70, 24);
  display.print("SCN:");
  display.print(state.scanCount);
  
  // Line 4: WiFi Signal
  display.setCursor(0, 36);
  display.print("WiFi:");
  if (state.wifiStrength > -67) {
    display.print("EXC");  // Excellent
  } else if (state.wifiStrength > -70) {
    display.print("GOOD"); // Good
  } else if (state.wifiStrength > -80) {
    display.print("FAIR"); // Fair
  } else {
    display.print("POOR"); // Poor
  }
  
  // Line 5: IP Address (scroll if needed)
  display.setCursor(0, 48);
  display.print("IP:");
  
  // Display IP (truncate if too long)
  String shortIP = state.ipAddress;
  if (shortIP.length() > 9) {
    shortIP = shortIP.substring(0, 9) + "...";
  }
  display.print(shortIP);
  
  // Draw distance bar at bottom (0-50cm)
  drawDistanceBar();
  
  display.display();
}

void drawDistanceBar() {
  int barWidth = 128;
  int barHeight = 8;
  int barY = 56;
  
  // Draw background

  
  if (state.lastDistance > 0) {
    // Calculate bar length (max 50cm)
    int barLength = map(constrain(state.lastDistance, 0, MAX_DISTANCE), 0, MAX_DISTANCE, 0, barWidth - 2);
    
    // Color coding based on distance
    if (state.lastDistance < 10) {
      
    } else if (state.lastDistance < 20) {
      display.fillRect(1, barY + 1, barLength, barHeight - 2, 1); // Light gray for medium
    } else {
      display.fillRect(1, barY + 1, barLength, barHeight - 2, 1); // Dark gray for far
    }
    
    // Add distance marker
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(barLength + 3, barY - 1);
    display.print(int(state.lastDistance));
  }
}

void displayWiFiInfo() {
  if (!config.oledEnabled) return;
  
  display.clearDisplay();
  
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  display.setCursor(0, 0);
  display.println("WiFi Connecting...");
  
  display.setCursor(0, 16);
  display.print("SSID:");
  if (String(ssid).length() > 14) {
    display.println(String(ssid).substring(0, 14) + "...");
  } else {
    display.println(ssid);
  }
  
  display.setCursor(0, 32);
  display.println("Signal:");
  display.setCursor(0, 42);
  display.print(state.wifiStrength);
  display.print(" dBm");
  
  display.display();
}

void displayAlertOnOLED(int angle, float distance) {
  if (!config.oledEnabled) return;
  
  // Flash the display for alert
  for (int i = 0; i < 3; i++) {
    display.invertDisplay(true);
    delay(200);
    display.invertDisplay(false);
    delay(200);
  }
  
  // Show alert message
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  display.setCursor(10, 0);
  display.println("OBJECT DETECTED!");
  
  display.setTextSize(2);
  display.setCursor(20, 20);
  display.print(distance, 0);
  display.setTextSize(1);
  display.println("cm");
  
  display.setTextSize(1);
  display.setCursor(20, 40);
  display.print("Angle: ");
  display.print(angle);
  display.print("°");
  
  display.display();
  delay(1000);
  
  // Return to normal display
  updateOLED();
}

// ========== WiFi INITIALIZATION ==========
void initializeWiFi() {
  if (config.oledEnabled) {
    displayWiFiInfo();
  }
  
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  
  int status = WiFi.begin(ssid, password);
  int attempts = 0;
  
  while (status != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    status = WiFi.status();
    attempts++;
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
  }
  
  if (status == WL_CONNECTED) {
    Serial.println("\n✓ WiFi Connected!");
    state.wifiStrength = WiFi.RSSI();
    state.systemStatus = "Connected";
  } else {
    Serial.println("\n✗ WiFi Connection Failed!");
    state.systemStatus = "Offline";
  }
  
  digitalWrite(LED_PIN, LOW);
}

// ========== CALIBRATION ==========
void performCalibration() {
  if (config.oledEnabled) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Calibrating...");
    display.setCursor(0, 16);
    display.println("0° -> 180°");
    display.display();
  }
  
  Serial.println("\nStarting 180° Calibration...");
  state.systemStatus = "Calibrating";
  
  // Move through full 180° range
  for (int pos = 0; pos <= 180; pos += 30) {
    radarServo.write(pos);
    
    if (config.oledEnabled) {
      display.setCursor(0, 32);
      display.print("Position: ");
      display.print(pos);
      display.print("°   ");
      display.display();
    }
    
    delay(300);
  }
  
  radarServo.write(90);
  
  if (config.oledEnabled) {
    display.clearDisplay();
    display.setCursor(0, 24);
    display.println("Calibration");
    display.setCursor(0, 40);
    display.println("Complete!");
    display.display();
    delay(1000);
  }
  
  Serial.println("✓ 180° Calibration Complete");
  state.systemStatus = "Calibration Complete";
}

// ========== STARTUP ANIMATION ==========
void startupAnimation() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
    delay(100);
  }
  
  // OLED startup animation
  if (config.oledEnabled) {
    display.clearDisplay();
    for (int x = 0; x < SCREEN_WIDTH; x += 4) {
     
      display.display();
      delay(10);
    }
  }
}

// ========== MAIN LOOP ==========
void loop() {
  unsigned long currentTime = millis();
  
  // Handle Radar Scanning
  if (state.isScanning && (currentTime - state.lastScanTime >= config.scanSpeed)) {
    updateRadarPosition();
    state.lastScanTime = currentTime;
  }
  
  // Update OLED Display
  updateOLED();
  
  // Handle Web Clients
  handleWebClients();
  
  // Update Status LED
  updateStatusLED();
  
  // Update WiFi strength periodically
  if (currentTime % 10000 < 50) {
    state.wifiStrength = WiFi.RSSI();
  }
}

// ========== RADAR FUNCTIONS ==========
void updateRadarPosition() {
  // Continuous 180° sweep
  state.currentAngle += config.stepSize * state.sweepDirection;
  
  // Boundary check for 180°
  if (state.currentAngle >= config.maxAngle) {
    state.currentAngle = config.maxAngle;
    state.sweepDirection = -1;
    state.scanCount++;
  } else if (state.currentAngle <= config.minAngle) {
    state.currentAngle = config.minAngle;
    state.sweepDirection = 1;
    state.scanCount++;
  }
  
  // Move servo
  radarServo.write(state.currentAngle);
  
  // Measure distance (max 50cm)
  float distance = measureDistance();
  state.lastDistance = distance;
  
  // Process object detection (only up to 50cm)
  if (distance > MIN_DISTANCE && distance <= MAX_DISTANCE) {
    handleObjectDetection(state.currentAngle, distance);
    
    // Check for alerts (within 20cm)
    if (config.alertsEnabled && distance < config.alertDistance) {
      state.alertActive = true;
      triggerAlert(state.currentAngle, distance);
    } else {
      state.alertActive = false;
    }
  } else {
    state.alertActive = false;
  }
  
  // Prepare JSON data
  prepareJSONData(distance);
}

float measureDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  
  // Reduced timeout for 50cm max range
  long duration = pulseIn(ECHO_PIN, HIGH, 5800); // 50cm ≈ 5800µs
  
  if (duration == 0) return 0.0;
  
  float distance = duration * 0.0343 / 2.0;
  
  // Limit to 50cm
  if (distance > MAX_DISTANCE) return 0.0;
  if (distance < MIN_DISTANCE) return 0.0;
  
  return distance;
}

void handleObjectDetection(int angle, float distance) {
  bool objectUpdated = false;
  
  // Try to update existing object
  for (int i = 0; i < MAX_OBJECTS; i++) {
    if (objects[i].confidence > 0) {
      if (abs(objects[i].angle - angle) < 15) {
        objects[i].distance = distance;
        objects[i].angle = angle;
        objects[i].timestamp = millis();
        objects[i].confidence = min(100, objects[i].confidence + 10);
        objectUpdated = true;
        break;
      }
    }
  }
  
  // Add new object
  if (!objectUpdated) {
    for (int i = 0; i < MAX_OBJECTS; i++) {
      if (objects[i].confidence == 0) {
        objects[i].angle = angle;
        objects[i].distance = distance;
        objects[i].timestamp = millis();
        objects[i].confidence = 60;
        objectCount++;
        state.objectsDetected++;
        break;
      }
    }
  }
  
  // Decay confidence
  for (int i = 0; i < MAX_OBJECTS; i++) {
    if (objects[i].confidence > 0) {
      if (millis() - objects[i].timestamp > 3000) {
        objects[i].confidence -= 2;
        if (objects[i].confidence <= 0) {
          objects[i].confidence = 0;
          objectCount--;
        }
      }
    }
  }
}

void triggerAlert(int angle, float distance) {
  static unsigned long lastAlertTime = 0;
  
  if (millis() - lastAlertTime > 1000) {
    Serial.print("⚠️ ALERT! Object at ");
    Serial.print(distance);
    Serial.print("cm, angle ");
    Serial.print(angle);
    Serial.println("°");
    
    // Visual alert
    digitalWrite(LED_PIN, HIGH);
    
    // OLED alert
    if (config.oledEnabled) {
      displayAlertOnOLED(angle, distance);
    }
    
    delay(100);
    digitalWrite(LED_PIN, LOW);
    
    lastAlertTime = millis();
  }
}

void prepareJSONData(float distance) {
  jsonBuffer = "{";
  jsonBuffer += "\"angle\":" + String(state.currentAngle);
  jsonBuffer += ",\"distance\":" + String(distance, 1);
  jsonBuffer += ",\"scan\":" + String(state.scanCount);
  jsonBuffer += ",\"objects\":" + String(objectCount);
  jsonBuffer += ",\"wifi\":" + String(state.wifiStrength);
  jsonBuffer += ",\"status\":\"" + state.systemStatus + "\"";
  jsonBuffer += ",\"maxRange\":" + String(MAX_DISTANCE);
  jsonBuffer += ",\"time\":" + String(millis() - state.startTime);
  
  
  // Add objects array
  jsonBuffer += ",\"objectData\":[";
  bool firstObject = true;
  for (int i = 0; i < MAX_OBJECTS; i++) {
    if (objects[i].confidence > 20) {
      if (!firstObject) jsonBuffer += ",";
      jsonBuffer += "{\"a\":" + String(objects[i].angle);
      jsonBuffer += ",\"d\":" + String(objects[i].distance, 1);
      jsonBuffer += ",\"c\":" + String(objects[i].confidence);
      jsonBuffer += "}";
      firstObject = false;
    }
  }
  jsonBuffer += "]";
  jsonBuffer += "}";
}

void updateStatusLED() {
  static unsigned long lastBlink = 0;
  
  if (state.alertActive) {
    // Fast blink for alert
    if (millis() - lastBlink > 200) {
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      lastBlink = millis();
    }
  } else if (state.isScanning) {
    // Slow blink for scanning
    if (millis() - lastBlink > 1000) {
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      lastBlink = millis();
    }
  } else {
    // Solid for paused
    digitalWrite(LED_PIN, LOW);
  }
}

// ========== WEB SERVER ==========
void handleWebClients() {
  client = server.available();
  
  if (client) {
    String request = "";
    String currentLine = "";
    
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        request += c;
        
        if (c == '\n') {
          if (currentLine.length() == 0) {
            serveRequest(request);
            break;
          } else {
            currentLine = "";
          }
        } else if (c != '\r') {
          currentLine += c;
        }
      }
    }
    
    delay(1);
    client.stop();
  }
}

void serveRequest(String request) {
  if (request.indexOf("GET / ") >= 0 || request.indexOf("GET /index") >= 0) {
    sendIndexPage();
  } else if (request.indexOf("GET /data") >= 0) {
    sendJSONData();
  } else if (request.indexOf("GET /status") >= 0) {
    sendStatus();
  } else if (request.indexOf("GET /control") >= 0) {
    handleControlRequest(request);
  } else if (request.indexOf("GET /oled") >= 0) {
    handleOLEDControl(request);
  } else {
    send404();
  }
}

void handleOLEDControl(String request) {
  if (request.indexOf("toggle") >= 0) {
    config.oledEnabled = !config.oledEnabled;
    if (config.oledEnabled) {
      updateOLED();
    }
  }
  
  sendJSONResponse("{\"oled\":\"" + String(config.oledEnabled ? "enabled" : "disabled") + "\"}");
}

void sendJSONResponse(String json) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.println();
  client.println(json);
}

// ========== ADD THESE FUNCTIONS IF NOT PRESENT ==========
void sendIndexPage() { 
  
  // ... (keep your existing sendIndexPage code) ...
  // Add OLED control button to your HTML
   client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  
  client.println("<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'><title>SCIENTI CREATION - 50cm Radar</title>");
  client.println("<style>");
  client.println("body{font-family:Arial,sans-serif;background:linear-gradient(135deg,#0a1929,#1a2f3e);color:white;margin:0;padding:20px;min-height:100vh;}");
  client.println(".header{text-align:center;padding:20px;margin-bottom:30px;border-bottom:3px solid #00ffcc;}");
  client.println(".brand{font-size:2.5rem;color:#00ffcc;text-shadow:0 0 15px rgba(0,255,204,0.5);margin-bottom:10px;}");
  client.println(".container{max-width:1400px;margin:0 auto;display:grid;grid-template-columns:2fr 1fr;gap:20px;}");
  client.println("@media (max-width:1200px){.container{grid-template-columns:1fr;}}");
  client.println(".card{background:rgba(20,40,60,0.6);border-radius:15px;padding:20px;border:1px solid rgba(0,200,255,0.3);box-shadow:0 10px 30px rgba(0,0,0,0.3);}");
  client.println(".card-title{color:#00ffcc;font-size:1.5rem;margin-bottom:20px;display:flex;align-items:center;gap:10px;}");
  client.println(".radar-container{position:relative;}");
  client.println("canvas{background:rgba(0,10,20,0.8);border-radius:10px;border:2px solid #0088ff;width:100%;height:500px;}");
  client.println(".watermark{position:absolute;top:20px;left:20px;color:rgba(0,255,204,0.3);font-size:1.8rem;font-weight:bold;pointer-events:none;}");
  client.println(".data-grid{display:grid;grid-template-columns:repeat(2,1fr);gap:15px;margin-top:20px;}");
  client.println(".data-item{background:rgba(0,40,80,0.5);padding:15px;border-radius:10px;border-left:4px solid #00ffcc;}");
  client.println(".data-label{color:#88ddff;font-size:0.9rem;margin-bottom:5px;}");
  client.println(".data-value{color:#00ffcc;font-size:1.8rem;font-weight:bold;}");
  client.println(".controls{display:flex;flex-wrap:wrap;gap:10px;margin-top:20px;}");
  client.println(".btn{background:linear-gradient(135deg,#0088ff,#0066cc);color:white;border:none;padding:12px 20px;border-radius:8px;cursor:pointer;font-size:1rem;flex:1;min-width:120px;transition:all 0.3s;}");
  client.println(".btn:hover{transform:scale(1.05);box-shadow:0 0 20px rgba(0,136,255,0.6);}");
  client.println(".btn-primary{background:linear-gradient(135deg,#00ffcc,#00cc99);}");
  client.println(".btn-danger{background:linear-gradient(135deg,#ff3366,#cc0033);}");
  client.println(".footer{margin-top:30px;text-align:center;color:#88aaff;padding:20px;border-top:1px solid rgba(0,150,255,0.3);}");
  client.println(".alert{background:rgba(255,50,50,0.2);border:1px solid #ff3366;padding:10px;border-radius:8px;margin:10px 0;animation:pulse 2s infinite;}");
  client.println("@keyframes pulse{0%,100%{opacity:1;}50%{opacity:0.7;}}");
  client.println(".range-indicator{height:10px;background:linear-gradient(90deg,#00ff00,#ffff00,#ff0000);border-radius:5px;margin-top:5px;}");
  client.println("</style>");
  client.println("</head><body>");
  
  client.println("<div class='header'>");
  client.println("<h1 class='brand'>SCIENTI CREATION RADAR</h1>");
  client.println("<p>180° Scanning | 50cm Range Detection | Real-time Monitoring</p>");
  client.print("<p>IP: ");
  client.print(WiFi.localIP());
  client.println(" | Status: <span id='sysStatus'>Ready</span></p>");
  client.println("</div>");
  
  client.println("<div class='container'>");
  
  // Left Panel - Radar Display
  client.println("<div class='card'>");
  client.println("<div class='card-title'><span style='font-size:1.2em;'>📡</span> Radar Visualization (0-50cm)</div>");
  client.println("<div class='radar-container'>");
  client.println("<canvas id='radarCanvas' width='800' height='500'></canvas>");
  client.println("<div class='watermark'>SCIENTI CREATION</div>");
  client.println("</div>");
  
  client.println("<div class='controls'>");
  client.println("<button class='btn btn-primary' onclick=\"sendCommand('start')\">▶ Start Scan</button>");
  client.println("<button class='btn' onclick=\"sendCommand('stop')\">⏸ Pause</button>");
  client.println("<button class='btn' onclick=\"sendCommand('calibrate')\">⚙ Calibrate</button>");
  client.println("<button class='btn btn-danger' onclick=\"sendCommand('reset')\">🔄 Reset</button>");
  client.println("</div>");
  client.println("</div>");
  
  // Right Panel - Data Display
  client.println("<div class='card'>");
  client.println("<div class='card-title'><span style='font-size:1.2em;'>📊</span> Real-time Data</div>");
  
  client.println("<div id='alertBox' class='alert' style='display:none;'>");
  client.println("<strong>⚠ CLOSE OBJECT DETECTED!</strong>");
  client.println("<div id='alertDetails'></div>");
  client.println("</div>");
  
  client.println("<div class='data-grid'>");
  client.println("<div class='data-item'>");
  client.println("<div class='data-label'>Current Angle</div>");
  client.println("<div class='data-value' id='angle'>0</div>");
  client.println("<span style='color:#88ffaa;'>°</span>");
  client.println("</div>");
  
  client.println("<div class='data-item'>");
  client.println("<div class='data-label'>Distance</div>");
  client.println("<div class='data-value' id='distance'>0</div>");
  client.println("<span style='color:#88ffaa;'>cm</span>");
  client.println("<div class='range-indicator' id='rangeIndicator'></div>");
  client.println("</div>");
  
  client.println("<div class='data-item'>");
  client.println("<div class='data-label'>Objects Detected</div>");
  client.println("<div class='data-value' id='objects'>0</div>");
  client.println("</div>");
  
  client.println("<div class='data-item'>");
  client.println("<div class='data-label'>Scan Count</div>");
  client.println("<div class='data-value' id='scanCount'>0</div>");
  client.println("</div>");
  
  client.println("<div class='data-item'>");
  client.println("<div class='data-label'>WiFi Strength</div>");
  client.println("<div class='data-value' id='wifi'>0</div>");
  client.println("<span style='color:#88ffaa;'>dBm</span>");
  client.println("</div>");
  
  client.println("<div class='data-item'>");
  client.println("<div class='data-label'>Uptime</div>");
  client.println("<div class='data-value' id='uptime'>00:00:00</div>");
  client.println("</div>");
  client.println("</div>");
  
  client.println("<h3 style='margin-top:20px;color:#00ffcc;'>Detected Objects (Max 50cm):</h3>");
  client.println("<div id='objectList' style='max-height:200px;overflow-y:auto;margin-top:10px;'>");
  client.println("<div style='color:#888;text-align:center;padding:10px;'>No objects detected</div>");
  client.println("</div>");
  client.println("</div>");
  client.println("</div>");
  
  client.println("<div class='footer'>");
  client.println("<p>© 2024 SCIENTI CREATION - 180° Radar System with 50cm Range</p>");
  client.println("<p>Arduino R4 WiFi | Wireless Object Detection | Real-time Monitoring</p>");
  client.println("</div>");
  
  // JavaScript
  client.println("<script>");
  client.println("const canvas = document.getElementById('radarCanvas');");
  client.println("const ctx = canvas.getContext('2d');");
  client.println("const centerX = canvas.width / 2;");
  client.println("const centerY = canvas.height / 2;");
  client.println("const radarRadius = 200;");
  client.println("let currentAngle = 0;");
  client.println("let maxRange = 50; // 50cm maximum");
  
  // Draw radar base function
  client.println("function drawRadarBase() {");
  client.println("ctx.clearRect(0, 0, canvas.width, canvas.height);");
  client.println("// Draw concentric circles for 50cm range");
  client.println("for (let i = 1; i <= 5; i++) {");
  client.println("const radius = (radarRadius / 5) * i;");
  client.println("ctx.beginPath();");
  client.println("ctx.arc(centerX, centerY, radius, 0, Math.PI);");
  client.println("ctx.strokeStyle = 'rgba(0, 255, 200, 0.3)';");
  client.println("ctx.lineWidth = 1;");
  client.println("ctx.stroke();");
  client.println("// Distance labels (0-50cm)");
  client.println("ctx.fillStyle = '#80ffd4';");
  client.println("ctx.font = '12px Arial';");
  client.println("const distance = i * 10; // 10, 20, 30, 40, 50cm");
  client.println("ctx.fillText(distance + 'cm', centerX + radius - 15, centerY + 5);");
  client.println("}");
  
  client.println("// Draw angle lines (every 30°)");
  client.println("for (let angle = -90; angle <= 90; angle += 30) {");
  client.println("const rad = (angle * Math.PI) / 180;");
  client.println("ctx.beginPath();");
  client.println("ctx.moveTo(centerX, centerY);");
  client.println("ctx.lineTo(centerX + radarRadius * Math.cos(rad), centerY - radarRadius * Math.sin(rad));");
  client.println("ctx.strokeStyle = 'rgba(0, 200, 255, 0.2)';");
  client.println("ctx.lineWidth = 1;");
  client.println("ctx.stroke();");
  client.println("// Angle labels");
  client.println("const labelX = centerX + (radarRadius + 20) * Math.cos(rad);");
  client.println("const labelY = centerY - (radarRadius + 20) * Math.sin(rad);");
  client.println("ctx.fillStyle = '#80ffff';");
  client.println("ctx.font = '14px Arial';");
  client.println("ctx.fillText(angle + '°', labelX - 10, labelY + 5);");
  client.println("}");
  
  client.println("// Draw center point");
  client.println("ctx.beginPath();");
  client.println("ctx.arc(centerX, centerY, 5, 0, Math.PI * 2);");
  client.println("ctx.fillStyle = '#00ffcc';");
  client.println("ctx.fill();");
  client.println("}");
  
  // Draw sweep line function
  client.println("function drawSweepLine(angle) {");
  client.println("const rad = ((angle - 90) * Math.PI) / 180;");
  client.println("ctx.beginPath();");
  client.println("ctx.moveTo(centerX, centerY);");
  client.println("ctx.lineTo(centerX + radarRadius * Math.cos(rad), centerY + radarRadius * Math.sin(rad));");
  client.println("const gradient = ctx.createLinearGradient(centerX, centerY, centerX + radarRadius * Math.cos(rad), centerY + radarRadius * Math.sin(rad));");
  client.println("gradient.addColorStop(0, 'rgba(0, 255, 0, 0.8)');");
  client.println("gradient.addColorStop(1, 'rgba(0, 255, 0, 0.2)');");
  client.println("ctx.strokeStyle = gradient;");
  client.println("ctx.lineWidth = 2;");
  client.println("ctx.stroke();");
  client.println("}");
  
  // Draw objects function
  client.println("function drawObjects(objects) {");
  client.println("if (!objects) return;");
  client.println("objects.forEach(obj => {");
  client.println("const angleRad = ((obj.a - 90) * Math.PI) / 180;");
  client.println("const distanceRatio = Math.min(obj.d / maxRange, 1);");
  client.println("const objRadius = radarRadius * distanceRatio;");
  client.println("const x = centerX + objRadius * Math.cos(angleRad);");
  client.println("const y = centerY + objRadius * Math.sin(angleRad);");
  
  client.println("// Draw object with color based on distance");
  client.println("ctx.beginPath();");
  client.println("ctx.arc(x, y, 10, 0, Math.PI * 2);");
  client.println("if (obj.d < 20) {");
  client.println("ctx.fillStyle = 'rgba(255, 50, 50, 0.9)'; // Red for close objects");
  client.println("} else if (obj.d < 35) {");
  client.println("ctx.fillStyle = 'rgba(255, 150, 50, 0.8)'; // Orange for medium");
  client.println("} else {");
  client.println("ctx.fillStyle = 'rgba(50, 255, 100, 0.7)'; // Green for far");
  client.println("}");
  client.println("ctx.fill();");
  
  client.println("// Draw distance label");
  client.println("ctx.fillStyle = '#ffffff';");
  client.println("ctx.font = 'bold 11px Arial';");
  client.println("ctx.fillText(obj.d.toFixed(0) + 'cm', x + 12, y - 12);");
  client.println("});");
  client.println("}");
  
  // Update range indicator
  client.println("function updateRangeIndicator(distance) {");
  client.println("const indicator = document.getElementById('rangeIndicator');");
  client.println("const percentage = Math.min((distance / maxRange) * 100, 100);");
  client.println("indicator.style.width = percentage + '%';");
  client.println("}");
  
  // Update object list
  client.println("function updateObjectList(objects) {");
  client.println("const list = document.getElementById('objectList');");
  client.println("if (!objects || objects.length === 0) {");
  client.println("list.innerHTML = '<div style=\"color:#888;text-align:center;padding:10px;\">No objects detected</div>';");
  client.println("return;");
  client.println("}");
  client.println("list.innerHTML = '';");
  client.println("objects.forEach(obj => {");
  client.println("const div = document.createElement('div');");
  client.println("div.style.padding = '10px';");
  client.println("div.style.margin = '5px 0';");
  client.println("div.style.background = obj.d < 20 ? 'rgba(255,50,50,0.3)' : 'rgba(255,150,50,0.3)';");
  client.println("div.style.borderRadius = '5px';");
  client.println("div.style.borderLeft = obj.d < 20 ? '4px solid #ff3366' : '4px solid #ffaa00';");
  client.println("div.innerHTML = `<strong>${obj.a}°</strong> | ${obj.d.toFixed(1)}cm | Confidence: ${obj.c}%`;");
  client.println("list.appendChild(div);");
  client.println("});");
  client.println("}");
  
  // Check for alerts
  client.println("function checkAlert(objects) {");
  client.println("const alertBox = document.getElementById('alertBox');");
  client.println("const alertDetails = document.getElementById('alertDetails');");
  client.println("let hasAlert = false;");
  client.println("let alertText = '';");
  
  client.println("if (objects) {");
  client.println("objects.forEach(obj => {");
  client.println("if (obj.d < 20) {");
  client.println("hasAlert = true;");
  client.println("alertText += `Object at ${obj.a}°, ${obj.d.toFixed(1)}cm<br>`;");
  client.println("}");
  client.println("});");
  client.println("}");
  
  client.println("if (hasAlert) {");
  client.println("alertBox.style.display = 'block';");
  client.println("alertDetails.innerHTML = alertText;");
  client.println("} else {");
  client.println("alertBox.style.display = 'none';");
  client.println("}");
  client.println("}");
  
  // Format time
  client.println("function formatTime(ms) {");
  client.println("const seconds = Math.floor((ms / 1000) % 60);");
  client.println("const minutes = Math.floor((ms / (1000 * 60)) % 60);");
  client.println("const hours = Math.floor((ms / (1000 * 60 * 60)) % 24);");
  client.println("return `${hours.toString().padStart(2, '0')}:${minutes.toString().padStart(2, '0')}:${seconds.toString().padStart(2, '0')}`;");
  client.println("}");
  
  // Fetch data from Arduino
  client.println("async function fetchData() {");
  client.println("try {");
  client.println("const response = await fetch('/data');");
  client.println("const data = await response.json();");
  
  client.println("// Update display elements");
  client.println("document.getElementById('angle').textContent = data.angle;");
  client.println("document.getElementById('distance').textContent = data.distance.toFixed(1);");
  client.println("document.getElementById('objects').textContent = data.objects;");
  client.println("document.getElementById('scanCount').textContent = data.scan;");
  client.println("document.getElementById('wifi').textContent = data.wifi;");
  client.println("document.getElementById('sysStatus').textContent = data.status;");
  client.println("document.getElementById('uptime').textContent = formatTime(data.time);");
  
  client.println("// Update range indicator");
  client.println("updateRangeIndicator(data.distance);");
  
  client.println("// Update radar display");
  client.println("drawRadarBase();");
  client.println("drawSweepLine(data.angle);");
  client.println("if (data.objectData) {");
  client.println("drawObjects(data.objectData);");
  client.println("updateObjectList(data.objectData);");
  client.println("checkAlert(data.objectData);");
  client.println("}");
  
  client.println("} catch (error) {");
  client.println("console.log('Connection error:', error);");
  client.println("document.getElementById('sysStatus').textContent = 'Disconnected';");
  client.println("}");
  client.println("}");
  
  // Send commands to Arduino
  client.println("async function sendCommand(cmd) {");
  client.println("await fetch('/control?cmd=' + cmd);");
  client.println("}");
  
  // Initialize radar
  client.println("drawRadarBase();");
  
  // Start polling
  client.println("setInterval(fetchData, 100);");
  client.println("fetchData();");
  
  client.println("</script>");
  client.println("</body></html>");
}

void sendJSONData() {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.println();
  client.println(jsonBuffer);
}

void sendStatus() {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/plain");
  client.println("Connection: close");
  client.println();
  client.println("System Status: " + state.systemStatus);
  client.println("OLED: " + String(config.oledEnabled ? "Enabled" : "Disabled"));
  client.println("IP: " + state.ipAddress);
  client.println("WiFi: " + String(state.wifiStrength) + " dBm");
  client.println("Uptime: " + String((millis() - state.startTime) / 1000) + "s");
}

void handleControlRequest(String request) {
  if (request.indexOf("start") >= 0) {
    state.isScanning = true;
  } else if (request.indexOf("stop") >= 0) {
    state.isScanning = false;
  } else if (request.indexOf("calibrate") >= 0) {
    performCalibration();
  } else if (request.indexOf("reset") >= 0) {
    objectCount = 0;
    state.objectsDetected = 0;
    state.scanCount = 0;
    for (int i = 0; i < MAX_OBJECTS; i++) {
      objects[i].confidence = 0;
    }
  }
  
  sendJSONResponse("{\"status\":\"ok\"}");
}

void send404() {
  client.println("HTTP/1.1 404 Not Found");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  client.println("<h1>404 Not Found</h1>");
}