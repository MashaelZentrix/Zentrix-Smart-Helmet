/*
========================================================
 ZENTRIX Smart Safety Helmet System
 Intelligent Worker Protection Prototype

 Developed for hazardous environments such as:
 Construction sites, industrial zones, and desert fields.

 This system integrates multiple sensors to detect:
 - Heat stress
 - Gas leaks
 - Loud sound / explosion
 - Worker fall
 - Emergency SOS request

 The system provides real-time alerts using:
 LCD display, warning LEDs, buzzer alarms, and cooling fan activation.

 Prototype developed using Arduino UNO as a proof of concept
 for a scalable smart safety platform.

 Project: ZENTRIX
========================================================
*/
/*
  ZENTRIX - Smart Safety Helmet Prototype
  ---------------------------------------
  Board: Arduino UNO

  Purpose:
  A smart safety helmet prototype that monitors:
  - Temperature
  - Gas
  - Loud sound
  - Fall / vibration
  - Manual SOS button

  Outputs:
  - LCD 16x2 I2C display
  - Main status LEDs (Green / Yellow / Red)
  - RGB emergency light
  - Buzzer
  - Relay-controlled cooling fan

  Notes:
  1) This code is written for demonstration and prototype validation.
  2) The vibration/fall sensor can behave differently depending on the module type.
     If the fall sensor triggers incorrectly, change:
       const int MOTION_ACTIVE_STATE = LOW;
     to:
       const int MOTION_ACTIVE_STATE = HIGH;

  Pin Mapping:
  - DHT11 temperature sensor        -> D9
  - Vibration / tilt sensor         -> D5
  - Relay module (fan)              -> D6
  - Emergency push button           -> D7
  - Buzzer                          -> D8
  - Green LED                       -> D2
  - Yellow LED                      -> D3
  - Red LED                         -> D4
  - RGB LED R/G/B                   -> D10 / D11 / D12
  - Gas sensor                      -> A0
  - Sound sensor                    -> A1
  - LCD I2C                         -> 0x27 (change to 0x3F if needed)
*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>

// ============================================================
// LCD
// ============================================================
LiquidCrystal_I2C lcd(0x27, 16, 2);   // Change to 0x3F if needed

// ============================================================
// Temperature Sensor
// ============================================================
#define DHTPIN 9
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// ============================================================
// Pin Definitions
// ============================================================
const int PIN_LED_GREEN   = 2;
const int PIN_LED_YELLOW  = 3;
const int PIN_LED_RED     = 4;

const int PIN_MOTION      = 5;
const int PIN_RELAY_FAN   = 6;
const int PIN_SOS_BUTTON  = 7;
const int PIN_BUZZER      = 8;

const int PIN_RGB_R       = 10;
const int PIN_RGB_G       = 11;
const int PIN_RGB_B       = 12;

const int PIN_GAS         = A0;
const int PIN_SOUND       = A1;

// ============================================================
// Configurable Thresholds
// ============================================================
const float TEMP_FAN_ON_C      = 32.0;
const float TEMP_FAN_OFF_C     = 29.0;

const int GAS_THRESHOLD        = 120;
const int SOUND_THRESHOLD      = 700;

// Motion sensor behavior:
// Most 2-pin vibration sensors with INPUT_PULLUP trigger LOW.
// If yours behaves opposite, change LOW to HIGH.
const int MOTION_ACTIVE_STATE  = LOW;

// ============================================================
// Timing Configuration
// ============================================================
const unsigned long STARTUP_IGNORE_TEMP_MS   = 5000;
const unsigned long STARTUP_IGNORE_MOTION_MS = 3000;

const unsigned long TEMP_READ_INTERVAL_MS    = 2000;
const unsigned long SAFE_PAGE_INTERVAL_MS    = 2000;

const unsigned long FALL_CONFIRM_MS          = 5000;
const unsigned long FALL_ALARM_MS            = 7000;
const unsigned long FALL_TRIGGER_DEBOUNCE_MS = 200;
const unsigned long FALL_COOLDOWN_MS         = 3000;

// ============================================================
// System State
// ============================================================
enum SystemState {
  STATE_SAFE,
  STATE_SOUND_ALERT,
  STATE_HEAT_ALERT,
  STATE_FALL_CHECK,
  STATE_FALL_ALERT,
  STATE_GAS_ALERT,
  STATE_SOS
};

SystemState currentState = STATE_SAFE;

// ============================================================
// Runtime Variables
// ============================================================
unsigned long startupTime = 0;
unsigned long lastTempReadTime = 0;
unsigned long lastSafePageTime = 0;
unsigned long lastLcdRefreshTime = 0;

float currentTemp = NAN;
bool fanState = false;
int safePage = 0;

// Fall logic
bool fallChecking = false;
bool fallAlarmActive = false;
unsigned long fallCheckStart = 0;
unsigned long fallAlarmStart = 0;
unsigned long motionActiveSince = 0;
unsigned long lastFallCycleEnd = 0;

// ============================================================
// Utility Functions
// ============================================================
void setMainLeds(bool greenOn, bool yellowOn, bool redOn) {
  digitalWrite(PIN_LED_GREEN,  greenOn  ? HIGH : LOW);
  digitalWrite(PIN_LED_YELLOW, yellowOn ? HIGH : LOW);
  digitalWrite(PIN_LED_RED,    redOn    ? HIGH : LOW);
}

void setRgb(bool redOn, bool greenOn, bool blueOn) {
  digitalWrite(PIN_RGB_R, redOn   ? HIGH : LOW);
  digitalWrite(PIN_RGB_G, greenOn ? HIGH : LOW);
  digitalWrite(PIN_RGB_B, blueOn  ? HIGH : LOW);
}

void setBuzzer(bool on) {
  digitalWrite(PIN_BUZZER, on ? HIGH : LOW);
}

void setFan(bool on) {
  digitalWrite(PIN_RELAY_FAN, on ? HIGH : LOW);
}

void stopAlerts() {
  setBuzzer(false);
  setRgb(false, false, false);
}

void showMessage(const String &line1, const String &line2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
}

bool isMotionActive() {
  return digitalRead(PIN_MOTION) == MOTION_ACTIVE_STATE;
}

bool isSosPressed() {
  return digitalRead(PIN_SOS_BUTTON) == LOW;  // INPUT_PULLUP
}

bool buzzerBlink(unsigned long periodMs) {
  return ((millis() / periodMs) % 2 == 0);
}

// ============================================================
// Temperature Handling
// ============================================================
void updateTemperature() {
  if (millis() - lastTempReadTime < TEMP_READ_INTERVAL_MS) return;
  lastTempReadTime = millis();

  float t = dht.readTemperature();
  if (!isnan(t)) {
    currentTemp = t;
  }
}

void updateFanLogic() {
  if (millis() - startupTime < STARTUP_IGNORE_TEMP_MS) {
    fanState = false;
    return;
  }

  if (!isnan(currentTemp)) {
    if (currentTemp >= TEMP_FAN_ON_C) {
      fanState = true;
    } else if (currentTemp <= TEMP_FAN_OFF_C) {
      fanState = false;
    }
  }
}

// ============================================================
// Fall Detection Logic
// ============================================================
void resetFallState() {
  fallChecking = false;
  fallAlarmActive = false;
  motionActiveSince = 0;
}

void updateFallLogic() {
  // Ignore motion sensor for a short time after startup
  if (millis() - startupTime < STARTUP_IGNORE_MOTION_MS) {
    resetFallState();
    return;
  }

  // Cooldown after a full fall cycle
  if (millis() - lastFallCycleEnd < FALL_COOLDOWN_MS) {
    resetFallState();
    return;
  }

  bool motionNow = isMotionActive();

  // Step 1: debounce initial trigger
  if (!fallChecking && !fallAlarmActive) {
    if (motionNow) {
      if (motionActiveSince == 0) {
        motionActiveSince = millis();
      } else if (millis() - motionActiveSince >= FALL_TRIGGER_DEBOUNCE_MS) {
        fallChecking = true;
        fallCheckStart = millis();
      }
    } else {
      motionActiveSince = 0;
    }
  }

  // Step 2: confirmation window
  if (fallChecking) {
    if (!motionNow) {
      // false trigger
      resetFallState();
      return;
    }

    if (millis() - fallCheckStart >= FALL_CONFIRM_MS) {
      fallChecking = false;
      fallAlarmActive = true;
      fallAlarmStart = millis();
    }
  }

  // Step 3: alarm window
  if (fallAlarmActive) {
    if (millis() - fallAlarmStart >= FALL_ALARM_MS) {
      fallAlarmActive = false;
      lastFallCycleEnd = millis();
    }
  }
}

// ============================================================
// LCD Pages
// ============================================================
void showSafePages() {
  if (millis() - lastSafePageTime >= SAFE_PAGE_INTERVAL_MS) {
    lastSafePageTime = millis();
    safePage++;
    if (safePage > 3) safePage = 0;
  }

  lcd.clear();

  switch (safePage) {
    case 0:
      lcd.setCursor(0, 0);
      lcd.print("SAFE BY ZENTRIX");
      lcd.setCursor(0, 1);
      lcd.print("System Secure");
      break;

    case 1:
      lcd.setCursor(0, 0);
      lcd.print("Temp: ");
      if (isnan(currentTemp)) {
        lcd.print("Error");
      } else {
        lcd.print(currentTemp, 1);
        lcd.print((char)223);
        lcd.print("C");
      }
      lcd.setCursor(0, 1);
      lcd.print("Cooling Standby");
      break;

    case 2:
      lcd.setCursor(0, 0);
      lcd.print("Gas Level:");
      lcd.setCursor(0, 1);
      lcd.print(analogRead(PIN_GAS));
      break;

    case 3:
      lcd.setCursor(0, 0);
      lcd.print("Sound:");
      lcd.print(analogRead(PIN_SOUND));
      lcd.setCursor(0, 1);
      lcd.print("ZENTRIX ACTIVE");
      break;
  }
}

// ============================================================
// State Evaluation
// Priority:
// 1. SOS
// 2. GAS
// 3. FALL ALERT / FALL CHECK
// 4. HEAT
// 5. SOUND
// 6. SAFE
// ============================================================
void evaluateState() {
  const int gasValue = analogRead(PIN_GAS);
  const int soundValue = analogRead(PIN_SOUND);

  if (isSosPressed()) {
    currentState = STATE_SOS;
    return;
  }

  if (gasValue > GAS_THRESHOLD) {
    currentState = STATE_GAS_ALERT;
    return;
  }

  if (fallAlarmActive) {
    currentState = STATE_FALL_ALERT;
    return;
  }

  if (fallChecking) {
    currentState = STATE_FALL_CHECK;
    return;
  }

  if (fanState) {
    currentState = STATE_HEAT_ALERT;
    return;
  }

  if (soundValue > SOUND_THRESHOLD) {
    currentState = STATE_SOUND_ALERT;
    return;
  }

  currentState = STATE_SAFE;
}

// ============================================================
// Output Rendering
// ============================================================
void renderState() {
  // Default reset
  setMainLeds(false, false, false);
  stopAlerts();

  switch (currentState) {
    case STATE_SOS:
      setFan(false);
      setMainLeds(false, false, true);
      setBuzzer(buzzerBlink(120));
      showMessage("SOS ALERT", "Worker NeedsHelp");
      break;

    case STATE_GAS_ALERT:
      setFan(false);
      setMainLeds(false, false, true);
      setRgb(false, false, true);     // Blue emergency light
      setBuzzer(buzzerBlink(300));
      showMessage("GAS DANGER!", "Evacuate Area");
      break;

    case STATE_FALL_CHECK:
      setFan(false);
      setMainLeds(false, true, false);
      showMessage("Impact Detected", "Checking Worker");
      break;

    case STATE_FALL_ALERT:
      setFan(false);
      setMainLeds(false, false, true);
      setBuzzer(buzzerBlink(250));
      showMessage("FALL ALERT!", "Worker Down");
      break;

    case STATE_HEAT_ALERT:
      setFan(true);
      setMainLeds(false, true, false);
      showMessage("HEAT WARNING", "Cooling Active");
      break;

    case STATE_SOUND_ALERT:
      setFan(false);
      setMainLeds(false, true, false);
      setBuzzer(buzzerBlink(250));
      showMessage("SOUND ALERT!", "Check Area");
      break;

    case STATE_SAFE:
    default:
      setFan(false);
      setMainLeds(true, false, false);

      if (millis() - startupTime < STARTUP_IGNORE_TEMP_MS) {
        showMessage("SAFE BY ZENTRIX", "Warming Up...");
      } else {
        showSafePages();
      }
      break;
  }
}

// ============================================================
// Setup
// ============================================================
void setup() {
  Serial.begin(9600);

  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_YELLOW, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);

  pinMode(PIN_MOTION, INPUT_PULLUP);
  pinMode(PIN_SOS_BUTTON, INPUT_PULLUP);

  pinMode(PIN_RELAY_FAN, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  pinMode(PIN_RGB_R, OUTPUT);
  pinMode(PIN_RGB_G, OUTPUT);
  pinMode(PIN_RGB_B, OUTPUT);

  dht.begin();

  lcd.init();
  lcd.backlight();

  startupTime = millis();

  showMessage("ZENTRIX SYSTEM", "Booting...");
  delay(900);

  setMainLeds(true, false, false);
  delay(180);
  setMainLeds(false, true, false);
  delay(180);
  setMainLeds(false, false, true);
  delay(180);
  setMainLeds(false, false, false);

  showMessage("Sensors Online", "System Ready");
  delay(900);

  setFan(false);
  setBuzzer(false);
  setRgb(false, false, false);
}

// ============================================================
// Main Loop
// ============================================================
void loop() {
  updateTemperature();
  updateFanLogic();
  updateFallLogic();
  evaluateState();
  renderState();

  // Debug output for development / committee review
  Serial.print("Temp: ");
  Serial.print(currentTemp);
  Serial.print(" | Gas: ");
  Serial.print(analogRead(PIN_GAS));
  Serial.print(" | Sound: ");
  Serial.print(analogRead(PIN_SOUND));
  Serial.print(" | Motion: ");
  Serial.print(isMotionActive() ? "ACTIVE" : "IDLE");
  Serial.print(" | SOS: ");
  Serial.print(isSosPressed() ? "ON" : "OFF");
  Serial.print(" | Fan: ");
  Serial.print(fanState ? "ON" : "OFF");
  Serial.print(" | FallCheck: ");
  Serial.print(fallChecking ? "YES" : "NO");
  Serial.print(" | FallAlarm: ");
  Serial.print(fallAlarmActive ? "YES" : "NO");
  Serial.print(" | State: ");
  Serial.println(currentState);
}
