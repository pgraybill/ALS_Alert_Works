/////////////////////////////////////////////////////////////////////////////////
///////////////// ALS Alert System - Cloud Test Board (no LCD/EOG) /////////////
/////////////////////////////////////////////////////////////////////////////////
//
// Tests the phone app + Firebase path without the full hardware. It uses the same
// Wi-Fi setup, Firestore fields, events and heartbeat as ALS_Alert_System_V3, so
// the app and Cloud Functions can't tell the difference.
//
// Wiring (ESP32-C5 Dev Module):
//   GPIO4  -> push button -> GND   "Patient alarm" (starts an alarm)
//   GPIO5  -> push button -> GND   "Red button"    (stops the alarm at the device)
//   GPIO10 -> optional LED (with ~330 ohm resistor) or active buzzer -> GND
//   The on-board RGB LED also shows the state (see below).
//
// Serial Monitor: 115200 baud, Tools > USB CDC On Boot: Enabled.
// Type a letter and press Enter:
//   a = alarm (same as GPIO4)      s = stop (same as GPIO5)
//   l = left electrode loose       r = right electrode loose
//   i = status                     w = forget Wi-Fi (back to "ALS Alert" setup)
//
// RGB LED: blinking red = alarm, blinking yellow = electrode loose,
//          green = monitoring and online, blue = Wi-Fi setup mode,
//          purple = connecting / cloud problem.

#include <Arduino.h>
#include <EEPROM.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>

// ==================== PINS ====================
  const int AlarmButton = 4;   // To GND, internal pull-up
  const int StopButton  = 5;   // To GND, internal pull-up
  const int AlarmOut    = 10;  // Optional LED or buzzer

// ==================== CONFIGURATION (same as ALS_Alert_System_V3) ====================
  #define API_KEY "AIzaSyCyEekgfYi5o1y20-P_4Q2LVJA-QK-YCI0"
  #define FIREBASE_PROJECT_ID "alsalert"

  const uint32_t POLL_INTERVAL_MS       = 5000;
  const uint32_t HEARTBEAT_INTERVAL_MS  = 20000;
  const uint32_t AUTH_TIMEOUT_MS        = 30000;
  const uint32_t WIFI_RETRY_MS          = 15000;
  const uint32_t EVENT_RETRY_MS         = 5000;
  const uint32_t ELECTRODE_DEBOUNCE_MS  = 5000;
  const uint32_t CLIENT_TIMEOUT_MS      = 3000;

  // EEPROM layout is identical to the real firmware, so Wi-Fi setup done here
  // carries over when the real firmware is flashed.
  const int ssidMaxLen    = 32;
  const int passMaxLen    = 64;
  const int emailMaxLen   = 64;
  const int epassMaxLen   = 64;
  const int successMaxLen = 32;
  const int apiKeyMaxLen  = 48;
  const int projIdMaxLen  = 40;

  const int ssidAddr    = 0;
  const int passAddr    = ssidAddr + ssidMaxLen;
  const int emailAddr   = passAddr + passMaxLen;
  const int epassAddr   = emailAddr + emailMaxLen;
  const int successAddr = epassAddr + epassMaxLen;
  const int apiKeyAddr  = successAddr + successMaxLen;
  const int projIdAddr  = apiKeyAddr + apiKeyMaxLen;

// ==================== SHARED STATE ====================
  enum CloudEventType : uint8_t { EVT_ALARM, EVT_ELECTRODE_LEFT, EVT_ELECTRODE_RIGHT };
  enum CloudEventSource : uint8_t { SRC_PATIENT, SRC_REMOTE };
  struct CloudEvent {
    uint8_t  type;
    uint8_t  source;
    uint32_t id;
    uint32_t raisedAt;
  };
  QueueHandle_t xEventQueue = NULL;

  enum Mode : uint8_t { MONITORING, ALARM, ELECTRODE_LOOSE };
  volatile Mode mode = MONITORING;

  // Requests from the cloud task; only loop() changes mode.
  volatile bool remoteStopRequested = false;
  volatile bool remoteAlarmStarted  = false;

  enum CloudStatus : uint8_t { CLOUD_OFF, CLOUD_SETUP_AP, CLOUD_WIFI_WAIT, CLOUD_AUTH, CLOUD_AUTH_FAILED, CLOUD_ONLINE };
  volatile CloudStatus cloudStatus = CLOUD_OFF;

  // Simulated electrodes (Serial 'l' / 'r')
  bool leftLoose = false;
  bool rightLoose = false;
  unsigned long electrodeLooseSince = 0;
  bool electrodeEventSent = false;

// ==================== FIREBASE & WI-FI ====================
  FirebaseData fbdo;
  FirebaseAuth auth;
  FirebaseConfig config;
  WiFiServer server(80);

  String uid = "";
  String path = "";
  String storedSSID = "";
  String storedPass = "";
  String storedEmail = "";
  String storedEpass = "";
  String storedSuccess = "";
  String firebaseApiKey = "";
  String firebaseProjectId = "";

  void writeEEPROM(int addr, String data, int maxLen);
  String readEEPROM(int addr, int length);
  void clearStoredCredentials();
  bool setFirestoreValue(String key, String value);
  bool writeServerTime(const char* fieldPath);
  bool writeHeartbeat();
  bool uploadEvent(const CloudEvent& evt);
  void queueCloudEvent(uint8_t type, uint8_t source);
  String getQueryParam(const String& query, const String& key);
  String urlDecode(const String& in);
  void handleWebProvisioning(WiFiClient& client, String request);
  void TaskWiFiCloud(void *pvParameters);

  const char* cloudStatusText() {
    switch (cloudStatus) {
      case CLOUD_SETUP_AP:    return "Wi-Fi setup mode (join \"ALS Alert\")";
      case CLOUD_WIFI_WAIT:   return "connecting to Wi-Fi";
      case CLOUD_AUTH:        return "signing in to Firebase";
      case CLOUD_AUTH_FAILED: return "Firebase sign-in FAILED (check account email/password)";
      case CLOUD_ONLINE:      return "online";
      default:                return "starting";
    }
  }

  const char* modeText() {
    switch (mode) {
      case ALARM:           return "ALARM";
      case ELECTRODE_LOOSE: return "ELECTRODE LOOSE";
      default:              return "monitoring";
    }
  }

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  unsigned long waitStart = millis();
  while (!Serial && millis() - waitStart < 3000) { delay(10); } // Give the Serial Monitor time to attach

  pinMode(AlarmButton, INPUT_PULLUP);
  pinMode(StopButton, INPUT_PULLUP);
  pinMode(AlarmOut, OUTPUT);
  digitalWrite(AlarmOut, LOW);

  EEPROM.begin(512);
  storedSSID    = readEEPROM(ssidAddr, ssidMaxLen);
  storedPass    = readEEPROM(passAddr, passMaxLen);
  storedEmail   = readEEPROM(emailAddr, emailMaxLen);
  storedEpass   = readEEPROM(epassAddr, epassMaxLen);
  storedSuccess = readEEPROM(successAddr, successMaxLen);
  firebaseApiKey    = readEEPROM(apiKeyAddr, apiKeyMaxLen);
  firebaseProjectId = readEEPROM(projIdAddr, projIdMaxLen);
  if (firebaseApiKey.length() == 0)    firebaseApiKey = API_KEY;
  if (firebaseProjectId.length() == 0) firebaseProjectId = FIREBASE_PROJECT_ID;

  Serial.println();
  Serial.println("==============================================");
  Serial.println(" ALS Alert cloud TEST BOARD");
  Serial.println(" GPIO4 button = alarm, GPIO5 button = stop");
  Serial.println(" Serial: a=alarm s=stop l/r=electrode loose i=status w=forget Wi-Fi");
  Serial.println("==============================================");
  if (storedSSID.length() > 0) {
    Serial.print("[wifi] Saved network: "); Serial.println(storedSSID);
    Serial.print("[cloud] Account: "); Serial.println(storedEmail);
  } else {
    Serial.println("[wifi] No saved Wi-Fi. Use the app's Wi-Fi setup.");
  }
  Serial.print("[cloud] Firebase project: "); Serial.println(firebaseProjectId);

  xEventQueue = xQueueCreate(16, sizeof(CloudEvent));
  xTaskCreatePinnedToCore(TaskWiFiCloud, "WiFiTask", 16384, NULL, 1, NULL, 0);
}

// ==================== BUTTONS, SERIAL COMMANDS, ALARM OUTPUT ====================
  // Returns true once per press (falling edge, 30 ms debounce)
  bool pressed(int pin, bool& lastState, unsigned long& lastChange) {
    bool state = digitalRead(pin);
    if (state != lastState && millis() - lastChange > 30) {
      lastChange = millis();
      lastState = state;
      return state == LOW;
    }
    return false;
  }

  void startAlarm(uint8_t source) {
    if (mode == ALARM) return;
    mode = ALARM;
    queueCloudEvent(EVT_ALARM, source);
    Serial.println();
    Serial.println(source == SRC_REMOTE ? "*** ALARM started from the APP (test alarm) ***"
                                        : "*** PATIENT ALARM triggered ***");
    Serial.println("    Press the stop button (GPIO5), type s, or tap Stop Alarm in the app.");
  }

  void stopAlarm(const char* how) {
    if (mode == MONITORING) {
      Serial.println("[device] Nothing to stop, already monitoring.");
      return;
    }
    mode = MONITORING;
    leftLoose = false;
    rightLoose = false;
    digitalWrite(AlarmOut, LOW);
    Serial.print("[device] Alarm stopped "); Serial.print(how); Serial.println(". Back to monitoring.");
  }

  void startElectrodeAlert() {
    if (mode != MONITORING) return;
    mode = ELECTRODE_LOOSE;
    electrodeLooseSince = millis();
    electrodeEventSent = false;
    Serial.print("*** ");
    Serial.print(leftLoose ? "LEFT" : "RIGHT");
    Serial.println(" ELECTRODE LOOSE (simulated) ***");
    Serial.println("    The phone is notified if it stays loose for 5 s. Press stop to reattach.");
  }

  void printStatus() {
    Serial.println("---------------- status ----------------");
    Serial.print(" Device:  "); Serial.println(modeText());
    Serial.print(" Cloud:   "); Serial.println(cloudStatusText());
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print(" Wi-Fi:   "); Serial.print(WiFi.SSID()); Serial.print("  IP "); Serial.println(WiFi.localIP());
    } else if (storedSSID.length() == 0) {
      Serial.println(" Wi-Fi:   setup mode, network \"ALS Alert\" at 192.168.4.1");
    } else {
      Serial.print(" Wi-Fi:   not connected (retrying "); Serial.print(storedSSID); Serial.println(")");
    }
    Serial.print(" UID:     "); Serial.println(uid.length() ? uid : String("(not signed in)"));
    Serial.print(" Queued events waiting to upload: "); Serial.println(uxQueueMessagesWaiting(xEventQueue));
    Serial.println("----------------------------------------");
  }

  void updateIndicators() {
    static unsigned long lastBlink = 0;
    static bool blinkOn = false;
    static CloudStatus shown = CLOUD_OFF;
    static Mode shownMode = ALARM;
    unsigned long period = mode == ALARM ? 250 : 1000;

    if (mode != MONITORING) {
      shownMode = mode;  // Forces a redraw when monitoring resumes

      if (millis() - lastBlink >= period) {
        lastBlink = millis();
        blinkOn = !blinkOn;
        digitalWrite(AlarmOut, blinkOn);
        if (mode == ALARM) rgbLedWrite(RGB_BUILTIN, blinkOn ? 64 : 0, 0, 0);
        else               rgbLedWrite(RGB_BUILTIN, blinkOn ? 64 : 0, blinkOn ? 40 : 0, 0);
      }
      return;
    }

    if (shown == cloudStatus && shownMode == mode) return;
    shown = cloudStatus;
    shownMode = mode;
    digitalWrite(AlarmOut, LOW);
    if (cloudStatus == CLOUD_ONLINE)        rgbLedWrite(RGB_BUILTIN, 0, 32, 0);
    else if (cloudStatus == CLOUD_SETUP_AP) rgbLedWrite(RGB_BUILTIN, 0, 0, 48);
    else                                    rgbLedWrite(RGB_BUILTIN, 24, 0, 32);
  }

void loop() {
  static bool alarmLast = HIGH, stopLast = HIGH;
  static unsigned long alarmChange = 0, stopChange = 0;
  static CloudStatus lastCloud = CLOUD_OFF;

  if (pressed(AlarmButton, alarmLast, alarmChange)) startAlarm(SRC_PATIENT);
  if (pressed(StopButton, stopLast, stopChange))    stopAlarm("at the device (GPIO5)");

  while (Serial.available()) {
    char c = tolower(Serial.read());
    if (c == 'a') startAlarm(SRC_PATIENT);
    else if (c == 's') stopAlarm("at the device (Serial)");
    else if (c == 'l') { leftLoose = true;  startElectrodeAlert(); }
    else if (c == 'r') { rightLoose = true; startElectrodeAlert(); }
    else if (c == 'i') printStatus();
    else if (c == 'w') {
      Serial.println("[wifi] Forgetting Wi-Fi and restarting into setup mode...");
      clearStoredCredentials();
      delay(500);
      ESP.restart();
    }
  }

  // Requests from the app (via the cloud task)
  if (remoteAlarmStarted) {
    remoteAlarmStarted = false;
    startAlarm(SRC_REMOTE);
  }
  if (remoteStopRequested) {
    remoteStopRequested = false;
    stopAlarm("from the APP");
  }

  // Electrode push only if still loose after the debounce time (one per event)
  if (mode == ELECTRODE_LOOSE && !electrodeEventSent && millis() - electrodeLooseSince >= ELECTRODE_DEBOUNCE_MS) {
    queueCloudEvent(leftLoose ? EVT_ELECTRODE_LEFT : EVT_ELECTRODE_RIGHT, SRC_PATIENT);
    electrodeEventSent = true;
    Serial.println("[device] Electrode still loose after 5 s, notifying the phone.");
  }

  if (cloudStatus != lastCloud) {
    lastCloud = cloudStatus;
    Serial.print("[cloud] Status: "); Serial.println(cloudStatusText());
  }

  updateIndicators();
  delay(10);
}

// ==================== App/Cloud-Service Interface (as in ALS_Alert_System_V3) ====================
void TaskWiFiCloud(void *pvParameters) {
  bool staMode = storedSSID.length() > 0;
  bool firebaseStarted = false;
  bool connectionReported = false;
  unsigned long authStart = 0;
  unsigned long lastWifiAttempt = 0;
  unsigned long lastPoll = 0;
  unsigned long lastHeartbeat = 0;
  unsigned long lastEventAttempt = 0;

  if (staMode) {
    Serial.print("[wifi] Connecting to "); Serial.println(storedSSID);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(storedSSID.c_str(), storedPass.c_str());
    lastWifiAttempt = millis();
    cloudStatus = CLOUD_WIFI_WAIT;

    int wifiCount = 0;
    while (WiFi.status() != WL_CONNECTED && wifiCount < 30) {
      vTaskDelay(pdMS_TO_TICKS(500));
      wifiCount++;
    }

    // Credentials from the app that never worked: go back to setup mode.
    // After one success, keep retrying instead (router may still be booting).
    if (WiFi.status() != WL_CONNECTED && storedSuccess != "success") {
      Serial.println("[wifi] Could not join the new network. Wrong name/password? Going back to setup mode.");
      clearStoredCredentials();
      vTaskDelay(pdMS_TO_TICKS(500));
      ESP.restart();
    }
    server.begin();
  } else {
    // Report phones joining/leaving the setup network, to debug Wi-Fi setup
    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
      if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
        Serial.println("[wifi] A phone JOINED \"ALS Alert\"");
      } else if (event == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) {
        Serial.println("[wifi] A phone LEFT \"ALS Alert\"");
      }
    });
    WiFi.mode(WIFI_AP);
    WiFi.softAP("ALS Alert", "");
    server.begin();
    cloudStatus = CLOUD_SETUP_AP;
    Serial.println("[wifi] Setup network \"ALS Alert\" started at 192.168.4.1");
    Serial.println("[wifi] On the phone: join \"ALS Alert\", then use the app's Wi-Fi setup.");
  }

  for (;;) {
    // 1. Wi-Fi setup requests from the app
    WiFiClient client = server.available();
    if (client) {
      unsigned long clientStart = millis();
      while (client.connected() && !client.available() && millis() - clientStart < CLIENT_TIMEOUT_MS) {
        vTaskDelay(1);
      }
      if (client.available()) {
        String request = client.readStringUntil('\r');
        client.flush();
        // Log the request without the query (it contains passwords)
        int q = request.indexOf('?');
        Serial.print("[wifi] Request received: ");
        Serial.println(q == -1 ? request : request.substring(0, q) + " (with settings)");
        handleWebProvisioning(client, request);
      } else {
        Serial.println("[wifi] A client connected but sent nothing");
        client.stop();
      }
    }

    if (staMode) {
      // 2. Keep Wi-Fi up
      if (WiFi.status() != WL_CONNECTED) {
        if (cloudStatus != CLOUD_WIFI_WAIT) Serial.println("[wifi] Wi-Fi lost, retrying...");
        cloudStatus = CLOUD_WIFI_WAIT;
        if (millis() - lastWifiAttempt >= WIFI_RETRY_MS) {
          lastWifiAttempt = millis();
          WiFi.disconnect();
          WiFi.begin(storedSSID.c_str(), storedPass.c_str());
        }
      } else {
        // 3. Start Firebase once
        if (!firebaseStarted) {
          Serial.print("[wifi] Connected. IP "); Serial.println(WiFi.localIP());

          config.api_key = firebaseApiKey.c_str();
          auth.user.email = storedEmail.c_str();
          auth.user.password = storedEpass.c_str();
          config.token_status_callback = tokenStatusCallback;

          Firebase.begin(&config, &auth);
          Firebase.reconnectWiFi(true);
          firebaseStarted = true;
          authStart = millis();
          cloudStatus = CLOUD_AUTH;
        }

        // 4. Sign-in, with a timeout instead of waiting forever
        if (!Firebase.ready()) {
          if (cloudStatus == CLOUD_ONLINE) {
            cloudStatus = CLOUD_AUTH;
            authStart = millis();
          }
          if (cloudStatus == CLOUD_AUTH && millis() - authStart >= AUTH_TIMEOUT_MS) {
            cloudStatus = CLOUD_AUTH_FAILED;
          }
        } else {
          if (uid.length() == 0) {
            uid = auth.token.uid.c_str();
            path = "ALSAlert/" + uid;
            Serial.print("[cloud] User UID: "); Serial.println(uid);
          }
          cloudStatus = CLOUD_ONLINE;

          // 5. Connection info once per boot
          if (!connectionReported) {
            FirebaseJson content;
            content.set("fields/connected/stringValue", "1");
            content.set("fields/ipaddress/stringValue", WiFi.localIP().toString());
            content.set("fields/lastConnectiontemp/stringValue", "now");
            // Commands written before this device was set up (e.g. the resetAll the app sends
            // at login) are not for it: clear them so they can't wipe or alarm a fresh device.
            content.set("fields/resetAll/stringValue", "0");
            content.set("fields/startAlarm/stringValue", "0");
            content.set("fields/stopAlarm/stringValue", "0");
            content.set("fields/startCal/stringValue", "0");

            if (Firebase.Firestore.patchDocument(&fbdo, firebaseProjectId.c_str(), "", path.c_str(), content.raw(), "connected,lastConnectiontemp,ipaddress,resetAll,startAlarm,stopAlarm,startCal")) {
              connectionReported = true;
              if (storedSuccess != "success") {
                writeEEPROM(successAddr, "success", successMaxLen);
                storedSuccess = "success";
              }
              writeServerTime("lastConnection");
              writeHeartbeat();
              lastHeartbeat = millis();
              Serial.println("[cloud] Connection info posted to Firestore. Ready.");
            } else {
              Serial.print("[cloud] Connection post failed: "); Serial.println(fbdo.errorReason());
            }
          }

          // 6. Upload queued events (kept until accepted)
          CloudEvent evt;
          if (connectionReported && millis() - lastEventAttempt >= EVENT_RETRY_MS
              && xQueuePeek(xEventQueue, &evt, 0) == pdTRUE) {
            if (uploadEvent(evt)) {
              xQueueReceive(xEventQueue, &evt, 0);
              lastEventAttempt = 0;
            } else {
              lastEventAttempt = millis();
            }
          }

          // 7. Heartbeat
          if (connectionReported && millis() - lastHeartbeat >= HEARTBEAT_INTERVAL_MS) {
            lastHeartbeat = millis();
            if (writeHeartbeat()) Serial.println("[cloud] Heartbeat sent (lastSeen)");
          }

          // 8. App commands
          if (connectionReported && millis() - lastPoll >= POLL_INTERVAL_MS) {
            lastPoll = millis();

            if (Firebase.Firestore.getDocument(&fbdo, firebaseProjectId.c_str(), "", path.c_str(), "connected,startAlarm,stopAlarm,startCal,resetAll")) {
              FirebaseJson payload;
              payload.setJsonData(fbdo.payload().c_str());
              FirebaseJsonData jsonData;

              payload.get(jsonData, "fields/connected/stringValue", true);
              if (jsonData.stringValue == "3") {
                Serial.println("[app] Wi-Fi test from the app, answering");
                setFirestoreValue("lastConnectiontemp", "now");
                writeServerTime("lastConnection");
                vTaskDelay(pdMS_TO_TICKS(1000));
                setFirestoreValue("connected", "4");
              }

              jsonData.clear();
              payload.get(jsonData, "fields/startAlarm/stringValue", true);
              if (jsonData.stringValue == "1") {
                Serial.println("[app] Start Alarm received");
                setFirestoreValue("startAlarm", "0");
                remoteAlarmStarted = true;
              }

              jsonData.clear();
              payload.get(jsonData, "fields/stopAlarm/stringValue", true);
              if (jsonData.stringValue == "1") {
                Serial.println("[app] Stop Alarm received");
                setFirestoreValue("stopAlarm", "0");
                if (mode != MONITORING) remoteStopRequested = true;
              }

              jsonData.clear();
              payload.get(jsonData, "fields/startCal/stringValue", true);
              if (jsonData.stringValue == "1") {
                Serial.println("[app] Start Calibration received (the real board would calibrate now)");
                setFirestoreValue("startCal", "0");
              }

              jsonData.clear();
              payload.get(jsonData, "fields/resetAll/stringValue", true);
              if (jsonData.stringValue == "1") {
                Serial.println("[app] Reset All received: forgetting Wi-Fi and restarting");
                setFirestoreValue("resetAll", "0");
                clearStoredCredentials();
                ESP.restart();
              }
            } else {
              Serial.print("[cloud] Poll failed: "); Serial.println(fbdo.errorReason());
            }
          }
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// ==================== EEPROM & HELPER FUNCTIONS (as in ALS_Alert_System_V3) ====================
  void writeEEPROM(int addr, String data, int maxLen) {
    int len = min((int)data.length(), maxLen - 1);
    for (int i = 0; i < len; ++i) {
      EEPROM.write(addr + i, data[i]);
    }
    EEPROM.write(addr + len, '\0');
    EEPROM.commit();
  }

  String readEEPROM(int addr, int length) {
    char data[length + 1];
    int i = 0;
    for (; i < length; ++i) {
      uint8_t b = EEPROM.read(addr + i);
      if (b == '\0' || b == 0xFF) break;
      data[i] = (char)b;
    }
    data[i] = '\0';
    return String(data);
  }

  void clearStoredCredentials() {
    char empty[1] = {0};
    EEPROM.put(ssidAddr, empty);
    EEPROM.put(passAddr, empty);
    EEPROM.put(emailAddr, empty);
    EEPROM.put(epassAddr, empty);
    EEPROM.put(successAddr, empty);
    EEPROM.put(apiKeyAddr, empty);
    EEPROM.put(projIdAddr, empty);
    EEPROM.commit();
    Serial.println("[wifi] Stored Wi-Fi credentials cleared.");
  }

  bool setFirestoreValue(String key, String value) {
    FirebaseJson content;
    content.set("fields/" + key + "/stringValue", value);
    if (Firebase.Firestore.patchDocument(&fbdo, firebaseProjectId.c_str(), "", path.c_str(), content.raw(), key)) {
      return true;
    }
    Serial.print("[cloud] Write of "); Serial.print(key); Serial.print(" failed: "); Serial.println(fbdo.errorReason());
    return false;
  }

  bool writeServerTime(const char* fieldPath) {
    std::vector<struct firebase_firestore_document_write_t> writes;
    struct firebase_firestore_document_write_t transform_write;
    transform_write.type = firebase_firestore_document_write_type_transform;
    transform_write.document_transform.transform_document_path = path.c_str();

    struct firebase_firestore_document_write_field_transforms_t field_transforms;
    field_transforms.fieldPath = fieldPath;
    field_transforms.transform_type = firebase_firestore_transform_type_set_to_server_value;
    field_transforms.transform_content = "REQUEST_TIME";
    transform_write.document_transform.field_transforms.push_back(field_transforms);
    writes.push_back(transform_write);

    if (Firebase.Firestore.commitDocument(&fbdo, firebaseProjectId.c_str(), "", writes, "")) {
      return true;
    }
    Serial.print("[cloud] Write of "); Serial.print(fieldPath); Serial.print(" failed: "); Serial.println(fbdo.errorReason());
    return false;
  }

  bool writeHeartbeat() {
    return writeServerTime("lastSeen");
  }

  bool uploadEvent(const CloudEvent& evt) {
    const char* typeStr = evt.type == EVT_ALARM ? "alarm"
                        : evt.type == EVT_ELECTRODE_LEFT ? "electrode_left" : "electrode_right";
    const char* sourceStr = evt.source == SRC_REMOTE ? "remote" : "patient";
    char idStr[20];
    snprintf(idStr, sizeof(idStr), "%08lx%08lx", (unsigned long)evt.raisedAt, (unsigned long)evt.id);
    String eventPath = path + "/events/" + idStr;

    FirebaseJson content;
    content.set("fields/type/stringValue", typeStr);
    content.set("fields/source/stringValue", sourceStr);
    content.set("fields/delayMs/integerValue", String((unsigned long)(millis() - evt.raisedAt)));

    std::vector<struct firebase_firestore_document_write_t> writes;
    struct firebase_firestore_document_write_t update_write;
    update_write.type = firebase_firestore_document_write_type_update;
    update_write.update_document_content = content.raw();
    update_write.update_document_path = eventPath.c_str();
    update_write.update_transforms.fieldPath = "at";
    update_write.update_transforms.transform_type = firebase_firestore_transform_type_set_to_server_value;
    update_write.update_transforms.transform_content = "REQUEST_TIME";
    writes.push_back(update_write);

    if (Firebase.Firestore.commitDocument(&fbdo, firebaseProjectId.c_str(), "", writes, "")) {
      Serial.print("[cloud] Event uploaded: "); Serial.print(typeStr);
      Serial.println(" -> the phone should get a notification");
      return true;
    }
    Serial.print("[cloud] Event upload failed (will retry): "); Serial.println(fbdo.errorReason());
    return false;
  }

  void queueCloudEvent(uint8_t type, uint8_t source) {
    CloudEvent evt;
    evt.type = type;
    evt.source = source;
    evt.id = esp_random();
    evt.raisedAt = millis();
    if (xQueueSend(xEventQueue, &evt, 0) != pdTRUE) {
      Serial.println("[cloud] Event queue full, event dropped");
    } else if (cloudStatus != CLOUD_ONLINE) {
      Serial.println("[cloud] Not online yet: event queued and will upload when connected");
    }
  }

  String getQueryParam(const String& query, const String& key) {
    String pattern = "?" + key + "=";
    int start = query.indexOf(pattern);
    if (start == -1) {
      pattern = "&" + key + "=";
      start = query.indexOf(pattern);
    }
    if (start == -1) return "";
    start += pattern.length();
    int end = query.indexOf('&', start);
    if (end == -1) end = query.length();
    return query.substring(start, end);
  }

  String urlDecode(const String& in) {
    String out;
    out.reserve(in.length());
    for (unsigned int i = 0; i < in.length(); ++i) {
      char c = in[i];
      if (c == '%' && i + 2 < in.length() && isxdigit(in[i + 1]) && isxdigit(in[i + 2])) {
        char hex[3] = { in[i + 1], in[i + 2], '\0' };
        out += (char)strtol(hex, NULL, 16);
        i += 2;
      } else {
        out += c;
      }
    }
    return out;
  }

  void handleWebProvisioning(WiFiClient& client, String request) {
    int queryStart = request.indexOf('?');
    int queryEnd   = request.lastIndexOf(" HTTP/");
    String query = (queryStart != -1 && queryEnd > queryStart) ? request.substring(queryStart, queryEnd) : "";

    String ssidStr   = urlDecode(getQueryParam(query, "ssid"));
    String passStr   = urlDecode(getQueryParam(query, "password"));
    String emailStr  = urlDecode(getQueryParam(query, "email"));
    String epassStr  = urlDecode(getQueryParam(query, "epassword"));
    String apiKeyStr = urlDecode(getQueryParam(query, "apikey"));
    String projIdStr = urlDecode(getQueryParam(query, "projectid"));

    if (ssidStr.length() > 0 && query.indexOf("password=") != -1) {
      if (ssidStr.length() >= ssidMaxLen || passStr.length() >= passMaxLen ||
          emailStr.length() >= emailMaxLen || epassStr.length() >= epassMaxLen ||
          apiKeyStr.length() >= apiKeyMaxLen || projIdStr.length() >= projIdMaxLen) {
        Serial.println("[wifi] Setup request rejected: a value is too long");
        client.println("HTTP/1.1 400 Bad Request\r\nContent-Type: text/html\r\n\r\n<!HTML><html><body><h1>A value is too long</h1></body></html>");
        client.stop();
        return;
      }

      Serial.print("[wifi] Setup received from the app: network \""); Serial.print(ssidStr);
      Serial.print("\", account "); Serial.println(emailStr);

      writeEEPROM(ssidAddr, ssidStr, ssidMaxLen);
      writeEEPROM(passAddr, passStr, passMaxLen);
      writeEEPROM(emailAddr, emailStr, emailMaxLen);
      writeEEPROM(epassAddr, epassStr, epassMaxLen);
      writeEEPROM(successAddr, "", successMaxLen);
      writeEEPROM(apiKeyAddr, apiKeyStr, apiKeyMaxLen);
      writeEEPROM(projIdAddr, projIdStr, projIdMaxLen);

      client.println("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!HTML><html><body><h1>Credentials Saved! Restarting...</h1></body></html>");
      client.stop();
      Serial.println("[wifi] Saved. Restarting to join the network...");
      vTaskDelay(pdMS_TO_TICKS(1000));
      ESP.restart();
    } else {
      client.println("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!HTML><html><body><h1>ALS Alert Provisioning</h1><p>Send SSID/Pass via form.</p></body></html>");
      client.stop();
    }
  }
