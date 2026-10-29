/////////////////////////////////////////////////////////////////////////////////
///////////////////////// ALS Alert System Team FreeRTOS Code //////////////////////////
/////////////////////////////////////////////////////////////////////////////////

//Libraries
  #include <Arduino.h>
  #include <Wire.h>               // I2C Communication
  #include <SSD1803A_I2C.h>       // Library for DOGS164-A LCD Display
  #include <Bounce2.h>            // Button Debouncing
  #include <EEPROM.h>             // EEPROM Storage for Wi-Fi/Firebase Credentials
  #include <WiFi.h>               // ESP32 Wi-Fi Stack
  #include <Firebase_ESP_Client.h>// Firebase Client
  #include <addons/TokenHelper.h> // Token status callbacks for Firebase

// ==================== TROUBLESHOOTING & CONFIGURATION ====================
  // Toggle this to true to show IP address on LCD in Wi-Fi Setup, or false to hide
    #define SHOW_IP_ADDRESS true

  /* Default Firebase API Key and Project ID */
  // Used only when the app did not send its own (apikey= / projectid= during Wi-Fi setup)
    #define API_KEY "AIzaSyCyEekgfYi5o1y20-P_4Q2LVJA-QK-YCI0"
    #define FIREBASE_PROJECT_ID "alsalert"

  // Cloud timing (see design decisions in the session log)
    const uint32_t POLL_INTERVAL_MS       = 5000;   // Check Firestore for app commands
    const uint32_t HEARTBEAT_INTERVAL_MS  = 20000;  // Write lastSeen so the server can detect offline
    const uint32_t AUTH_TIMEOUT_MS        = 30000;  // Report "Cloud auth fail" after this long
    const uint32_t WIFI_RETRY_MS          = 15000;  // Retry Wi-Fi this often while disconnected
    const uint32_t EVENT_RETRY_MS         = 5000;   // Retry a failed event upload this often
    const uint32_t ELECTRODE_DEBOUNCE_MS  = 5000;   // Electrode must stay loose this long before a push
    const uint32_t CLIENT_TIMEOUT_MS      = 3000;   // Drop provisioning clients that send nothing

  // EEPROM Layout Configuration (512 Bytes total)
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
    const int projIdAddr  = apiKeyAddr + apiKeyMaxLen;   // Ends at byte 344

// ==================== PIN DECLARATIONS ====================
  const int SDA_ESP32    = 2; 
  const int SCL_ESP32    = 3; 
  const int Reset_LCD    = 0;
  const int Scroll       = 1; 
  const int EOG_Pin      = 6;
  const int Select       = 8;
  const int Alarm        = 10;
  const int Reset_Button = 26;
  const int Cal_LED      = 23;
  const int CompPin      = 27; // Comparator Pin for electrode contact
  const int Battery      = 4;
  const int Wall         = 5;
  const int CompPin2     = 14; // Comparator Pin for electrode contact

// ==================== SAMPLING CONSTANTS ====================
  const uint8_t  SAMPLING_FREQ_HZ = 50;
  const uint16_t SAMPLE_PERIOD_MS = 1000 / SAMPLING_FREQ_HZ; // 20 ms

// ==================== FREERTOS & THREAD-SAFE HANDLES ====================
  // Events waiting to be uploaded to Firestore. Kept until the upload succeeds,
  // so alarms raised while offline are sent once the connection returns.
  enum CloudEventType : uint8_t { EVT_ALARM, EVT_ELECTRODE_LEFT, EVT_ELECTRODE_RIGHT };
  enum CloudEventSource : uint8_t { SRC_PATIENT, SRC_REMOTE };
  struct CloudEvent {
    uint8_t  type;
    uint8_t  source;
    uint32_t id;        // Random id chosen on the device, so a retried upload never duplicates
    uint32_t raisedAt;  // millis() when the event happened
  };
  QueueHandle_t xEventQueue = NULL;

  // Global trigger variable shared between tasks
  // 0=none, 1=alarm trigger, 2=left electrode loose, 3=right electrode loose, 4=cal fail
  volatile uint8_t Trigger = 0;
  volatile bool runSampling = false;

  // Requests from the Wi-Fi task to the UI task. Only the UI task changes MenuState.
  volatile bool remoteStopRequested = false;  // App pressed "Stop Alarm"
  volatile bool remoteAlarmStarted  = false;  // App pressed "Start Alarm" (test)

  // Cloud connection status, shown in the Wi-Fi Setup menu
  enum CloudStatus : uint8_t { CLOUD_OFF, CLOUD_WIFI_WAIT, CLOUD_AUTH, CLOUD_AUTH_FAILED, CLOUD_ONLINE };
  volatile CloudStatus cloudStatus = CLOUD_OFF;

// ==================== EOG ALGORITHM & CALIBRATION VARIABLES ====================
 
  //Parameters (Updated to best AND configuration as of 10/6/2026)
    float Upper_Thresh = 2700;
    float Lower_Thresh = 500;
    uint8_t NOEM = 5;
    uint16_t MET = 6250; // Move Expiration time in mS
    uint16_t SLET = 1500;  // Single Look Expiration Time in mS
    float PO = 0.4643;   

    uint16_t nFFT = 512;
    float Max_Freq = 2;
    float Min_Freq = 0.3775;
    float LDAT =   0.2;
    uint8_t LDAD = 13; 

  uint16_t Cal_Thresh = 10000; // Duration of active calibration
  uint8_t RA = 10;

  unsigned long Time_Samp = 0;
  unsigned long Time_Look = 0;

  float Sig = 0;
  float Sig_Prev = 0;
  uint16_t Sig_Rol = 0;
  uint16_t Sig_Rol_Prev = 0;

  uint8_t Move[7] = {0};
  unsigned long Time_Move[7] = {0};
  bool Up_Look_Allow = 1;
  bool Down_Look_Allow = 1;
  uint8_t Look = 0;
  uint8_t ii = 0;
  bool MoveAdded = 0;

  unsigned long Time_Cal = 0;
  unsigned long Time_Cal_Alt = 0;
  unsigned long Time_Cal_Alt_Mini = 0;
  uint8_t Cal_Count = 0;
  unsigned long BeepTime = 0;
  uint16_t BeepThresh = 500;
  uint16_t BeepThresh2 = 1000;
  bool Toggle = 0;
  float Avg_T = 0;
  float Avg_F = 0;
  unsigned long samped = 0;
  float pre_val = 1500;
  unsigned long first_samp = 0;
  unsigned long last_samp = 0;
  int16_t Up_Adjust = 0;
  int16_t Down_Adjust = 0;
  bool first = 1;
  float PtoPM = 0;
  float PtoPA = 0;
  bool Done = 0;
  uint8_t Cal = 0;

  float Slope = 0;
  float Slope_Inst = 0;
  float Slope_Prev = 0;
  uint16_t Midpoint = 0;

  float Count_Max = 0;
  float Count_Min = 0;
  uint8_t Printed = 0;

  bool ElectrodeLoose = 0;
  bool ElectrodeLoose2 = 0;

  bool stateJustChanged = 1;

  //Variables for pattern recognition
      uint16_t UpperFreq = 1.5;
      uint16_t LowerFreq = 0.85;

      uint16_t UpperPtoP = 2750;
      uint16_t LowerPtoP = 1500;

      uint8_t Breadth = 0;
      uint8_t Freq = 0;

// ==================== LCD & UI HARDWARE ====================
  int W_power, Wp_prev, B_power, Bp_prev;
  int Blink = 0;
  const int Blink_dly = 400;
  unsigned long B_Alt_delay;

  Bounce scrollButton = Bounce();
  Bounce selButton    = Bounce();
  Bounce resetButton  = Bounce();

  const uint8_t LCD_Address = 0x3C;
  SSD1803A_I2C lcd(LCD_Address);

  // Menu FSM States
  enum Menu {
    Main_Menu,
    Run_Device,
    Battery_Menu,
    Calibration_Menu,
    Wifi_Setup,
    Wifi_Reset_Confirm,
    Alert_State
  };
  Menu MenuState = Main_Menu;

  enum Sel { opt1, opt2, opt3, opt4 };
  Sel SelState = opt1;

// ==================== FIREBASE & WI-FI GLOBAL OBJECTS ====================
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

  // Forward Declarations of Helper Functions
  void writeEEPROM(int addr, String data, int maxLen);
  String readEEPROM(int addr, int length);
  void clearStoredCredentials();
  bool setFirestoreValue(String key, String value);
  bool writeHeartbeat();
  bool uploadEvent(const CloudEvent& evt);
  void queueCloudEvent(uint8_t type, uint8_t source);
  String getQueryParam(const String& query, const String& key);
  String urlDecode(const String& in);
  void handleWebProvisioning(WiFiClient& client, String request);

  // Task Function Prototypes
  void TaskUI_FSM(void *pvParameters);
  void TaskBiosignal(void *pvParameters);
  void TaskWiFiCloud(void *pvParameters);

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);

  // Initialize Pin Modes
    pinMode(EOG_Pin, INPUT);
    pinMode(Wall, INPUT);     
    pinMode(Battery, INPUT);  
    pinMode(Cal_LED, OUTPUT);
    pinMode(Alarm, OUTPUT);
    pinMode(Scroll, INPUT);   
    pinMode(Select, INPUT);   
    pinMode(Reset_Button, INPUT_PULLDOWN); 
    pinMode(CompPin, INPUT);   
    pinMode(CompPin2, INPUT);   

  // Initialize EEPROM
    EEPROM.begin(512);

  // Read Saved Wi-Fi Credentials from EEPROM
    storedSSID    = readEEPROM(ssidAddr, 32);
    storedPass    = readEEPROM(passAddr, 64);
    storedEmail   = readEEPROM(emailAddr, 64);
    storedEpass   = readEEPROM(epassAddr, 64);
    storedSuccess = readEEPROM(successAddr, 32);
    firebaseApiKey    = readEEPROM(apiKeyAddr, apiKeyMaxLen);
    firebaseProjectId = readEEPROM(projIdAddr, projIdMaxLen);
    if (firebaseApiKey.length() == 0)    firebaseApiKey = API_KEY;
    if (firebaseProjectId.length() == 0) firebaseProjectId = FIREBASE_PROJECT_ID;

  // Wire (I2C) & Button Debounce Initialization
    Wire.begin(SDA_ESP32, SCL_ESP32);
    scrollButton.attach(Scroll);       scrollButton.interval(20);
    selButton.attach(Select);          selButton.interval(20);
    resetButton.attach(Reset_Button);  resetButton.interval(20);

    B_Alt_delay = millis();
    W_power = 1; B_power = 1; Wp_prev = 1; Bp_prev = 1;

  // LCD Setup
    lcd.begin(DOGS164, Reset_LCD); 
    lcd.display(CONTRAST, 54);
    lcd.display(SET_ROM_A);
    lcd.cls();
    lcd.display(VIEW_BOTTOM);
    lcd.display(LINES_3_2); 
    lcd.locate(2,1);                  
    lcd.print("   Device  ON");  
    delay(1000);

  // Render Initial Main Menu Screen (All 4 Options)
    lcd.cls();
    lcd.display(LINES_4);
    lcd.locate(1,1); lcd.print("Calibrate    ***");
    lcd.locate(2,1); lcd.print("Battery Menu    ");
    lcd.locate(3,1); lcd.print("Run Device      ");
    lcd.locate(4,1); lcd.print("Wifi Setup      ");

  // Create Event Queue for the Wi-Fi Task (holds alarms until uploaded)
    xEventQueue = xQueueCreate(16, sizeof(CloudEvent));

  // Create FreeRTOS Tasks on Core 0
  // WiFiTask needs 16 KB: TLS handshakes in Firebase_ESP_Client overflow 8 KB
    xTaskCreatePinnedToCore(TaskBiosignal, "BiosignalTask", 4096,  NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(TaskUI_FSM,    "UITask",        4096,  NULL, 2, NULL, 0);
    xTaskCreatePinnedToCore(TaskWiFiCloud, "WiFiTask",      16384, NULL, 1, NULL, 0);
}

void loop() {
  // FreeRTOS manages execution. Delete the default loop task.
  vTaskDelete(NULL);
}

// ==================== TASK 1: UI & Finite STATE MACHINE ====================
void TaskUI_FSM(void *pvParameters) {
  for (;;) {
    scrollButton.update();  
    selButton.update();
    resetButton.update(); 

    Wp_prev = W_power;               
    Bp_prev = B_power;               
    W_power = digitalRead(Wall);
    B_power = digitalRead(Battery);

    // A remote "Start Alarm" from the app fires from any menu, not only Run Device
    if (Trigger == 1 && MenuState != Alert_State) {
      runSampling = false;
      MenuState = Alert_State;
    }

    switch (MenuState) {
      case Main_Menu:
        runSampling = false;
        if(stateJustChanged == 1){
        lcd.cls();
        lcd.display(LINES_4);
        stateJustChanged = 0;
        }

        switch (SelState) {
          case opt1:  // Calibrate
            lcd.locate(1,1); lcd.print("Calibrate    ***");
            lcd.locate(2,1); lcd.print("Battery Menu    ");
            lcd.locate(3,1); lcd.print("Run Device      ");
            lcd.locate(4,1); lcd.print("Wifi Setup      ");

            if (scrollButton.fell()) {
              SelState = opt2;
            } else if (selButton.rose()) {
              MenuState = Calibration_Menu;
              SelState = opt1;
              stateJustChanged = 1;
              lcd.cls(); 
            }
            break; 

          case opt2: // Battery Menu
            lcd.locate(1,1); lcd.print("Calibrate       ");
            lcd.locate(2,1); lcd.print("Battery Menu ***");
            lcd.locate(3,1); lcd.print("Run Device      ");
            lcd.locate(4,1); lcd.print("Wifi Setup      ");

            if (scrollButton.fell()) {
              SelState = opt3;
            } else if (selButton.rose()) {
              MenuState = Battery_Menu;
              SelState = opt1;
              stateJustChanged = 1;
              lcd.cls();
            }
            break; 

          case opt3: // Run Device
            lcd.locate(1,1); lcd.print("Calibrate       ");
            lcd.locate(2,1); lcd.print("Battery Menu    ");
            lcd.locate(3,1); lcd.print("Run Device   ***");
            lcd.locate(4,1); lcd.print("Wifi Setup      ");

            if (scrollButton.fell()) {
              SelState = opt4;
            } else if (selButton.rose()) {
              MenuState = Run_Device;
              SelState = opt1; 
              stateJustChanged = 1;
            }
            break; 

          case opt4: // Wifi Setup
            lcd.locate(1,1); lcd.print("Calibrate       ");
            lcd.locate(2,1); lcd.print("Battery Menu    ");
            lcd.locate(3,1); lcd.print("Run Device      ");
            lcd.locate(4,1); lcd.print("Wifi Setup   ***");

            if (scrollButton.fell()) {
              SelState = opt1; // Wrap around back to opt1
            } else if (selButton.rose()) {
              MenuState = Wifi_Setup;
              SelState = opt1;
              stateJustChanged = 1;
              lcd.cls();
            }
            break;
        }
        break; 

      case Wifi_Setup:
        runSampling = false;
        lcd.display(LINES_4);
        
        // Display status based on whether Wi-Fi is connected or in SoftAP mode
          if (WiFi.status() == WL_CONNECTED) {
            lcd.locate(1,1); lcd.print("WiFi Connected  ");
            if (cloudStatus == CLOUD_AUTH_FAILED) {
              lcd.locate(2,1); lcd.print("Cloud auth fail ");
            } else if (cloudStatus != CLOUD_ONLINE) {
              lcd.locate(2,1); lcd.print("Cloud connecting");
            } else if (SHOW_IP_ADDRESS) {
              lcd.locate(2,1); lcd.print(WiFi.localIP().toString().c_str());
            } else {
              lcd.locate(2,1); lcd.print("Status: Online  ");
            }
          } else if (storedSSID.length() > 0) {
            lcd.locate(1,1); lcd.print("WiFi Lost       ");
            lcd.locate(2,1); lcd.print("Retrying...     ");
          } else {
            lcd.locate(1,1); lcd.print("AP: ALS Alert   ");
            lcd.locate(2,1); lcd.print("192.168.4.1     ");
          }

        // Sub-options in Wi-Fi Menu
          if (SelState == opt1) {
            lcd.locate(3,1); lcd.print("Back to Main ***");
            lcd.locate(4,1); lcd.print("Reset WiFi      ");
            if (scrollButton.fell()) { SelState = opt2; }
            else if (selButton.rose()) {
              MenuState = Main_Menu;
              SelState = opt1;
            }
          } else if (SelState == opt2) {
            lcd.locate(3,1); lcd.print("Back to Main    ");
            lcd.locate(4,1); lcd.print("Reset WiFi   ***");
            if (scrollButton.fell()) { SelState = opt1; }
            else if (selButton.rose()) {
              MenuState = Wifi_Reset_Confirm;
              lcd.cls();
            }
          }
        break;

      case Wifi_Reset_Confirm:
        lcd.display(LINES_4);
        lcd.locate(1,1); lcd.print("Reset WiFi?");
        lcd.locate(2,1); lcd.print("Sel=YES Scroll=NO");
        lcd.locate(3,1); lcd.print("Clears all saved");
        lcd.locate(4,1); lcd.print("credentials!");

        if (selButton.rose()) {
          lcd.cls();
          lcd.locate(2,1); lcd.print(" Resetting WiFi ");
          clearStoredCredentials();
          vTaskDelay(pdMS_TO_TICKS(1000));
          ESP.restart(); // Reboot into SoftAP Provisioning Mode
        } else if (scrollButton.fell()) {
          MenuState = Wifi_Setup;
          SelState = opt1;
          lcd.cls();
        }
        break;

      case Calibration_Menu:
        runSampling = false;
        Time_Cal_Alt = millis();
        Cal_Count = 0;
        Done = 0;
        digitalWrite(Cal_LED, HIGH);

        while (Done == 0) {
          Cal_Count++;
          if (Cal_Count == 1) {
            Time_Cal_Alt_Mini = millis();
            lcd.display(LINES_3_3);
            lcd.locate(1,1); lcd.print(" CALIBRATING IN");
            lcd.locate(3,1); lcd.print("       3");
            Serial.println("3");
            while (millis() - Time_Cal_Alt_Mini <= 1000) { vTaskDelay(1); }
          } else if (Cal_Count == 2) {
            Time_Cal_Alt_Mini = millis();
            lcd.display(LINES_3_3);
            lcd.locate(3,1); lcd.print("       2");
            Serial.println("2");
            while (millis() - Time_Cal_Alt_Mini <= 1000) { vTaskDelay(1); }
          } else if (Cal_Count == 3) {
            Time_Cal_Alt_Mini = millis();
            lcd.display(LINES_3_3);
            lcd.locate(3,1); lcd.print("       1");
            Serial.println("1");
            while (millis() - Time_Cal_Alt_Mini <= 1000) { vTaskDelay(1); }
          } else if (Cal_Count == 4) {
            Time_Cal_Alt_Mini = millis();
            lcd.display(LINES_3_3);
            lcd.locate(3,1); lcd.print("       GO!");
            Serial.println("GO!");
            while (millis() - Time_Cal_Alt_Mini <= 1000) { vTaskDelay(1); }
            Done = 1;
          }
        }

        Count_Max = 0; Count_Min = 0;
        Serial.print(Lower_Thresh); Serial.print("\t"); Serial.println(Upper_Thresh);

        Printed = 0; Cal = 1;
        Up_Look_Allow = 1; Down_Look_Allow = 1;
        Time_Cal = millis(); Time_Samp = millis();
        pre_val = 1500; Lower_Thresh = 0; Upper_Thresh = 0; first = 1;

        while (Cal == 1){

          if (Printed == 0){
            //Jasmine : I would replace this bottom part if possible
            Serial.println("Start Active Calibration: Look Left and right");
            Printed = 1;
          }
          
          if (millis()-samped >= 5){
            samped = millis();
            Sig = analogReadMilliVolts(EOG_Pin);

            Sig_Rol_Prev = Sig_Rol; 
            Sig_Rol= Sig_Rol*0.95 + Sig*0.05;

            Slope_Prev = Slope;
            Slope_Inst = Sig_Rol- Sig_Rol_Prev;
            Slope = Slope*0.95 + Slope_Inst*0.05;

            //Serial.print(Slope); Serial.print("\t"); Serial.print(Slope_Prev); Serial.print("\t"); Serial.print(-45); Serial.print("\t"); Serial.println(45);

            if ((millis()-Time_Samp >= 100) && (abs(Sig_Rol-Sig_Rol_Prev) >= 8)){//If its been at least 25 ms since the last sample
              //Serial.println(Slope);

              //If the slope is negative and the previous slope was positive and the last one was a miniumum
              if ((Slope < 0.0) && (Slope_Prev >= 0.0) && (Up_Look_Allow)){ 
                Time_Samp = millis();
                Count_Max = Count_Max + 1;
                if (Count_Min + Count_Max == 3){
                  first_samp = millis();
                }
                last_samp = millis();
                Upper_Thresh = Upper_Thresh + Sig_Rol;
                Up_Look_Allow = 0;
                Down_Look_Allow = 1;
                pre_val = Sig;
                
                Serial.print("New Upper Thresh"); Serial.print("\t"); Serial.print(Sig_Rol); Serial.print("\t"); Serial.println(Sig_Rol_Prev);
                
                if (first == 1){
                  Upper_Thresh = 0;
                  Count_Max = 0;
                  first = 0;
                }
              //It's a minimum if the slope is now positive and was negative before
              }else if ((Slope > 0.0) && (Slope_Prev <= 0.0) && (Down_Look_Allow)){ 
                Time_Samp = millis();
                Count_Min = Count_Min + 1;
                if (Count_Min + Count_Max == 3){
                  first_samp = millis();
                }
                last_samp = millis();
                Lower_Thresh = Lower_Thresh + Sig_Rol;
                Up_Look_Allow = 1;
                Down_Look_Allow = 0;
                pre_val = Sig;

                Serial.print("New Lower Thresh"); Serial.print("\t"); Serial.print(Sig_Rol); Serial.print("\t"); Serial.println(Sig_Rol_Prev);

                if (first == 1){
                  Lower_Thresh = 0;
                  Count_Min = 0;
                  first = 0;
                }
              }
            } //End of 100 ms check if statement

            //If enough time has elapsed then end the calibration
            if ((millis() - Time_Cal) > Cal_Thresh) {
              Cal = 0;
            }
          } //End of 5ms sampling block

          // Yield to FreeRTOS watchdog to prevent core panics
          vTaskDelay(1);
        }//End of while


        if ((Count_Min + Count_Max) <= 4) {
          Trigger = 4;
          MenuState = Alert_State;

        } else {
          MenuState = Main_Menu;
          Avg_T = (last_samp - first_samp) / ((Count_Min + Count_Max - 2) * 500); 
          Avg_F = (1 / Avg_T) - 0.06;

          Upper_Thresh /= Count_Max;
          Lower_Thresh /= Count_Min;
          PtoPM = Upper_Thresh - Lower_Thresh;  
          Midpoint = (Upper_Thresh + Lower_Thresh) / 2;

          Serial.print("Raw Thresholds"); Serial.print("\t"); Serial.print(Lower_Thresh); Serial.print("\t"); Serial.println(Upper_Thresh);

          PtoPA = 1.22 * Avg_F * PtoPM + 0.17 * PtoPM; 
          Serial.print("PtoPA"); Serial.print("\t"); Serial.print(PtoPA); Serial.print("\t"); Serial.println(PtoPM);
          Up_Adjust = PtoPA / 2; Down_Adjust = PtoPA / 2;

          //Categorizing Eye Movment by Frequency and Breadth
          if (PtoPA > UpperPtoP){Breadth = 2;}else if(PtoPA < LowerPtoP){Breadth = 0;}else{Breadth = 1;}
          if (Avg_F > UpperFreq){Freq = 2;}else if (Avg_F < LowerFreq){Freq = 0;}else{Freq = 1;}
          
          //Updating Parameters
            //If it is FB or FM
              if (Freq == 2 && Breadth >= 1){
                Serial.println("FB/FM");
                //Time Parameters
                NOEM = 5; SLET = 1000; MET = 5000; PO = 0.15; 

                //Frequency Parameters  
                nFFT = 64; Max_Freq = 2; Min_Freq = 0.1; LDAT = 0.2; LDAD = 7; 
              }

            //If it is FN 
              if (Freq == 2 && Breadth == 0){
                Serial.println("FN");
                
                //Time Parameters
                NOEM = 5; SLET = 1000; MET = 5000; PO = 0.15; 

                //Frequency Parameters  
                nFFT = 64; Max_Freq = 2; Min_Freq = 0.1; LDAT = 0.2; LDAD = 7; 
              }
            
            //If it is SB 
              if (Freq < 2 && Breadth == 2) {
                Serial.println("SB");
                //Time Parameters
                NOEM = 5; SLET = 1100; MET = 6250; PO = 0.15; 

                //Frequency Parameters  
                nFFT = 512; Max_Freq = 2; Min_Freq = 0.1; LDAT = 0.2; LDAD = 7; 
              }

            //If it is SM 
              if (Freq < 2 && Breadth == 1){
                Serial.println("SM");
                
                //Time Parameters
                NOEM = 5; SLET = 1100; MET = 8750; PO = 0.15; 

                //Frequency Parameters  
                nFFT = 512; Max_Freq = 2; Min_Freq = 0.1; LDAT = 0.2; LDAD = 13; 
              }

            //If it is SN 
              if (Freq < 2 && Breadth == 0){
                Serial.println("SN");

                //Time Parameters
                NOEM = 5; SLET = 1600; MET = 6250; PO = 0.2286; 

                //Frequency Parameters  
                nFFT = 512; Max_Freq = 2; Min_Freq = 0.655; LDAT = 0.5; LDAD = 19; 
              }
            //Print New Parameter Values
              Serial.print("NEOM"); Serial.print("\t"); Serial.println(NOEM);
              Serial.print("SLET"); Serial.print("\t"); Serial.println(SLET);
              Serial.print("MET"); Serial.print("\t"); Serial.println(MET);
              Serial.print("PO"); Serial.print("\t"); Serial.println(PO);
              Serial.print("nFFT"); Serial.print("\t"); Serial.println(nFFT);
              Serial.print("Max_Freq"); Serial.print("\t"); Serial.println(Max_Freq);
              Serial.print("Min_Freq"); Serial.print("\t"); Serial.println(Min_Freq);
              Serial.print("LDAT"); Serial.print("\t"); Serial.println(LDAT);
              Serial.print("LDAD"); Serial.print("\t"); Serial.println(LDAD);
            


          while (Midpoint + Up_Adjust > 3300) { Up_Adjust *= 0.9; }
          while (Midpoint - Down_Adjust <= 0) { Down_Adjust *= 0.9; }

          Upper_Thresh = Midpoint + Up_Adjust;
          Lower_Thresh = Midpoint - Down_Adjust;

          if ((Upper_Thresh - Lower_Thresh) <= 200) {
            Trigger = 4;
            MenuState = Alert_State;
          } else {
            Upper_Thresh = Midpoint + (Upper_Thresh - Midpoint) * PO;
            Lower_Thresh = Midpoint - (Midpoint - Lower_Thresh) * PO;

            Serial.print("Actual Thresholds to be used"); Serial.print("\t"); Serial.print(Lower_Thresh); Serial.print("\t"); Serial.println(Upper_Thresh);
            digitalWrite(Cal_LED, HIGH);
            Serial.print("Average Frequency is "); Serial.println(Avg_F);
            Serial.println("Done Calibration!");
        
            lcd.cls();
            lcd.display(LINES_4);  
            lcd.locate(1,1); lcd.print("Calibrate    ***");
            lcd.locate(2,1); lcd.print("Battery Menu    ");
            lcd.locate(3,1); lcd.print("Run Device      ");
            lcd.locate(4,1); lcd.print("Wifi Setup      ");
          }
        }
        digitalWrite(Cal_LED, LOW);
        break;

      case Battery_Menu: 
        runSampling = false;
        if (selButton.rose()) {
          MenuState = Main_Menu;
          SelState = opt1;
          lcd.cls(); lcd.display(LINES_4);  
          lcd.locate(1,1); lcd.print("Calibrate    ***");
          lcd.locate(2,1); lcd.print("Battery Menu    ");
          lcd.locate(3,1); lcd.print("Run Device      ");
          lcd.locate(4,1); lcd.print("Wifi Setup      ");
          W_power = 1; B_power = 1; Wp_prev = 1; Bp_prev = 1;
        } else {
          if (B_power == 0 && Bp_prev == 0) {
            lcd.display(LINES_3_2); 
            lcd.locate(1,1); lcd.print("                    ");
            if (millis() - B_Alt_delay > Blink_dly) {
              Blink = !Blink;
              lcd.locate(2,1); lcd.print(Blink ? "                 " : "  Battery  Low  ");
              B_Alt_delay = millis();  
            }
          } else if (B_power == 1) {
            lcd.display(LINES_3_2);  
            lcd.locate(2,1); lcd.print("Battery  Charged"); 
          }
        }
        break; 

      case Run_Device:
        if(stateJustChanged == 1){
          lcd.display(LINES_3_2);
          lcd.cls();
          lcd.locate(2,1); lcd.print(" Device Running");
          runSampling = true;
          stateJustChanged = 0; // Draw once; clearing every 20 ms made the LCD flicker
        }
        if (selButton.rose()) {  
          runSampling = false;
          MenuState = Main_Menu;
          SelState = opt1;
          lcd.cls(); lcd.display(LINES_4);  
          lcd.locate(1,1); lcd.print("Calibrate    ***");
          lcd.locate(2,1); lcd.print("Battery Menu    ");
          lcd.locate(3,1); lcd.print("Run Device      ");
          lcd.locate(4,1); lcd.print("Wifi Setup      ");
          W_power = 1; B_power = 1; Wp_prev = 1; Bp_prev = 1;

        } else {
          if (Trigger != 0) {
            runSampling = false;
            MenuState = Alert_State;
          }

          if (W_power == 0 && Wp_prev == 1) {  
            lcd.cls(); lcd.display(LINES_2);
            lcd.locate(1,4); lcd.print("Wall power");
            lcd.locate(2,3); lcd.print("disconnected");
            
          } else if (W_power == 0 && Wp_prev == 0) {
            if (B_power == 0 && Bp_prev == 0) {
              lcd.display(LINES_3_2); 
              lcd.locate(1,1); lcd.print("                    ");
              if (millis() - B_Alt_delay > Blink_dly) {
                Blink = !Blink;
                lcd.locate(2,1); lcd.print(Blink ? "                 " : "  Battery  Low  ");
                B_Alt_delay = millis();  
              }
            } else if (B_power == 1 && Bp_prev == 0) {
              lcd.locate(2,1); lcd.print("Battery  Charged"); 
            }
          } else if (W_power == 1 && Wp_prev == 0) {
            lcd.cls(); lcd.display(LINES_3_2);
            lcd.locate(2,1); lcd.print(" Device Running");  
          }
        }
        break; 

      case Alert_State: {
        runSampling = false;
        remoteStopRequested = false; // Ignore any stop that arrived before this alert

        // Queue the patient alarm for the Wi-Fi task (uploaded with retries)
        if (Trigger == 1) {
          queueCloudEvent(EVT_ALARM, remoteAlarmStarted ? SRC_REMOTE : SRC_PATIENT);
          remoteAlarmStarted = false;
        }

        // Electrode alerts are pushed once, only if still loose after the debounce time
        unsigned long alertStart = millis();
        bool electrodeEventSent = false;

        if (Trigger == 1) {
          while (resetButton.read() == LOW && Trigger == 1 && !remoteStopRequested) {
            lcd.display(LINES_4);
            lcd.locate(1,1); lcd.print("Alarm Triggered");
            lcd.locate(2,1); lcd.print("Hit Red button");
            lcd.locate(3,1); lcd.print("to reset device");

            resetButton.update(); 
            if (millis() - BeepTime >= BeepThresh) {
              BeepTime = millis(); Toggle = !Toggle;
              digitalWrite(Alarm, Toggle);
            }
            vTaskDelay(10);
          }
        } else if (Trigger == 2) { 
          while (resetButton.read() == LOW && Trigger == 2 && !remoteStopRequested) {
            lcd.display(LINES_4);
            lcd.locate(1,1); lcd.print("Left Electrode");
            lcd.locate(2,1); lcd.print("Loose           ");
            lcd.locate(3,1); lcd.print("Hit Red button");
            lcd.locate(4,1); lcd.print("to reset device");

            if (!electrodeEventSent && millis() - alertStart >= ELECTRODE_DEBOUNCE_MS
                && digitalRead(CompPin) == 1) {
              queueCloudEvent(EVT_ELECTRODE_LEFT, SRC_PATIENT);
              electrodeEventSent = true;
            }

            resetButton.update();
            if (millis() - BeepTime >= BeepThresh2) {
              BeepTime = millis(); Toggle = !Toggle;
              digitalWrite(Alarm, Toggle);
            }
            vTaskDelay(10);
          }
        } else if (Trigger == 3) {
          while (resetButton.read() == LOW && Trigger == 3 && !remoteStopRequested) {
            lcd.display(LINES_4);
            lcd.locate(1,1); lcd.print("Right Electrode ");
            lcd.locate(2,1); lcd.print("Loose            ");
            lcd.locate(3,1); lcd.print("Hit Red button");
            lcd.locate(4,1); lcd.print("to reset device");

            if (!electrodeEventSent && millis() - alertStart >= ELECTRODE_DEBOUNCE_MS
                && digitalRead(CompPin2) == 1) {
              queueCloudEvent(EVT_ELECTRODE_RIGHT, SRC_PATIENT);
              electrodeEventSent = true;
            }

            resetButton.update();
            if (millis() - BeepTime >= BeepThresh2) {
              BeepTime = millis(); Toggle = !Toggle;
              digitalWrite(Alarm, Toggle);
            }
            vTaskDelay(10);
          }
        } else if (Trigger == 4) {
          while (resetButton.read() == LOW && Trigger == 4 && !remoteStopRequested) {
            lcd.display(LINES_4);
            lcd.locate(1,1); lcd.print(" Calibration  ");
            lcd.locate(2,1); lcd.print(" Failed ");
            lcd.locate(3,1); lcd.print(" Recalibrate ");
            lcd.locate(4,1); lcd.print("              ");

            resetButton.update(); 
            if (millis() - BeepTime >= BeepThresh2) {
              BeepTime = millis(); Toggle = !Toggle;
              digitalWrite(Alarm, Toggle);
            }
            vTaskDelay(10);
          }
        }

        digitalWrite(Alarm, LOW);

        // Stopped from the app: silence and go straight back to monitoring the patient.
        // (A failed calibration has nothing to monitor with, so it still returns to the menu.)
        if (remoteStopRequested && Trigger != 4) {
          remoteStopRequested = false;
          Trigger = 0;
          SelState = opt1;
          MenuState = Run_Device;
          stateJustChanged = 1;
          break;
        }
        remoteStopRequested = false;

        lcd.cls(); lcd.display(LINES_4);
        lcd.locate(1,1); lcd.print("Calibrate    ***");
        lcd.locate(2,1); lcd.print("Battery Menu    ");
        lcd.locate(3,1); lcd.print("Run Device      ");
        lcd.locate(4,1); lcd.print("Wifi Setup      ");

        Trigger = 0;
        MenuState = Main_Menu;
        break;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(20)); // Button tick rate
  }
}

// ==================== TASK 2: EOG Signal Processing ====================
void TaskBiosignal(void *pvParameters) {
  TickType_t xLastWakeTime = xTaskGetTickCount();

  for (;;) {
    // Enforce precise 50 Hz sampling (20 ms period)
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(SAMPLE_PERIOD_MS));

    if (!runSampling) continue;

    // Read analog millivolts
    Sig_Prev = Sig; 
    Sig = analogReadMilliVolts(EOG_Pin); 

    //Serial.print(Sig); Serial.print("\t"); Serial.print(0); Serial.print("\t"); Serial.println(3500);

    Time_Samp = millis(); 

    // Look Upper Check
    if ((Sig > Upper_Thresh) && (Sig_Prev < Upper_Thresh) && (Up_Look_Allow == 1)) {
      Look++; Time_Look = millis();
      Up_Look_Allow = 0; Down_Look_Allow = 1;
      Serial.println("An Upper Look was Detected");
    }
    // Look Lower Check
    if ((Sig < Lower_Thresh) && (Sig_Prev > Lower_Thresh) && (Down_Look_Allow == 1)) {
      Look++; Time_Look = millis();
      Up_Look_Allow = 1; Down_Look_Allow = 0;
      Serial.println("A Lower Look was Detected");
    }

    // Look Expiration Time
    if (((millis() - Time_Look) >= SLET) && (Time_Look != 0)) {
      Serial.println("Look Removed");
      Look = 0; Time_Look = 0;
    }

    // Move Triggering Logic
    if (Look >= 2) {
      Look = 0; Time_Look = 0;
      Serial.println("Move Detected!");
      for (ii = 0; ii < NOEM; ii++) {
        if ((Move[ii] == 0) && (MoveAdded == 0)) {
          Move[ii] = 1;
          Time_Move[ii] = millis();
          MoveAdded = 1; 
        }
      }
      MoveAdded = 0; 
    }

    // Alarm Trigger Check
    if (Move[NOEM - 1] == 1) {
      Trigger = 1;
      Serial.println("Patient Trigger Detected");
      Look = 0; Up_Look_Allow = 1; Down_Look_Allow = 1;
      for (ii = 0; ii < NOEM; ii++) { Move[ii] = 0; Time_Move[ii] = 0; }
    }

    // Move Expiration Time
    if (((millis() - Time_Move[0]) >= MET) && (Time_Move[0] != 0)) {
      Serial.println("Move Removed");
      for (ii = 0; ii < NOEM - 1; ii++) {
        Move[ii] = Move[ii + 1];
        Time_Move[ii] = Time_Move[ii + 1];
      }
    }

    // Check Comparator Contact Pins
    ElectrodeLoose = digitalRead(CompPin);
    ElectrodeLoose2 = digitalRead(CompPin2);
    if (ElectrodeLoose == 1)  Trigger = 2;
    if (ElectrodeLoose2 == 1) Trigger = 3;
  }
}

// ==================== TASK 3: App/Cloud-Service Interface ====================
void TaskWiFiCloud(void *pvParameters) {
  bool staMode = storedSSID.length() > 0;
  bool firebaseStarted = false;
  bool connectionReported = false;  // "connected=1" handshake written for this boot
  unsigned long authStart = 0;
  unsigned long lastWifiAttempt = 0;
  unsigned long lastPoll = 0;
  unsigned long lastHeartbeat = 0;
  unsigned long lastEventAttempt = 0;

  if (staMode) {
    Serial.println("Attempting Wi-Fi Connection with saved EEPROM credentials...");
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(storedSSID.c_str(), storedPass.c_str());
    lastWifiAttempt = millis();
    cloudStatus = CLOUD_WIFI_WAIT;

    int wifiCount = 0;
    while (WiFi.status() != WL_CONNECTED && wifiCount < 30) {
      vTaskDelay(pdMS_TO_TICKS(500));
      Serial.print(".");
      wifiCount++;
    }

    // Fallback if failed to connect and not marked success:
    // the credentials from the app were never proven, so go back to SoftAP setup.
    // Once a setup has succeeded, keep retrying instead (router may still be booting).
    if (WiFi.status() != WL_CONNECTED && storedSuccess != "success") {
      clearStoredCredentials();
      ESP.restart();
    }
    server.begin();
  } else {
    // If no SSID stored, start SoftAP Mode
    WiFi.mode(WIFI_AP);
    WiFi.softAP("ALS Alert", "");
    server.begin();
    Serial.println("SoftAP Started ('ALS Alert' at 192.168.4.1)");
  }

  for (;;) {
    // 1. Process SoftAP Web Server Provisioning Requests
    WiFiClient client = server.available();
    if (client) {
      unsigned long clientStart = millis();
      while (client.connected() && !client.available() && millis() - clientStart < CLIENT_TIMEOUT_MS) {
        vTaskDelay(1);
      }
      if (client.available()) {
        String request = client.readStringUntil('\r');
        client.flush();
        handleWebProvisioning(client, request);
      } else {
        client.stop(); // Connected but sent nothing; don't block the cloud task
      }
    }

    if (staMode) {
      // 2. Keep Wi-Fi up; retry forever after a power or router outage
      if (WiFi.status() != WL_CONNECTED) {
        if (cloudStatus != CLOUD_WIFI_WAIT) Serial.println("Wi-Fi lost, retrying...");
        cloudStatus = CLOUD_WIFI_WAIT;
        if (millis() - lastWifiAttempt >= WIFI_RETRY_MS) {
          lastWifiAttempt = millis();
          WiFi.disconnect();
          WiFi.begin(storedSSID.c_str(), storedPass.c_str());
        }
      } else {
        // 3. Start Firebase once Wi-Fi is up (only once per boot)
        if (!firebaseStarted) {
          Serial.println("\nConnected to WiFi.");
          Serial.println(WiFi.localIP());

          config.api_key = firebaseApiKey.c_str();
          auth.user.email = storedEmail.c_str();
          auth.user.password = storedEpass.c_str();
          config.token_status_callback = tokenStatusCallback;

          Firebase.begin(&config, &auth);
          Firebase.reconnectWiFi(true);
          firebaseStarted = true;
          authStart = millis();
          cloudStatus = CLOUD_AUTH;
          Serial.println("Getting Firebase User UID...");
        }

        // 4. Wait for sign-in without blocking; report a failure after the timeout
        if (!Firebase.ready()) {
          if (cloudStatus == CLOUD_ONLINE) {
            cloudStatus = CLOUD_AUTH;   // Token refresh in progress or lost
            authStart = millis();
          }
          if (cloudStatus == CLOUD_AUTH && millis() - authStart >= AUTH_TIMEOUT_MS) {
            cloudStatus = CLOUD_AUTH_FAILED;
            Serial.println("Cloud auth failed. Check the account email/password from the app.");
          }
        } else {
          if (uid.length() == 0) {
            uid = auth.token.uid.c_str();
            path = "ALSAlert/" + uid;
            Serial.print("User UID: "); Serial.println(uid);
          }
          cloudStatus = CLOUD_ONLINE;

          // 5. Post connection info once per boot (retried until it succeeds)
          if (!connectionReported) {
            FirebaseJson content;
            content.set("fields/connected/stringValue", "1");
            content.set("fields/ipaddress/stringValue", WiFi.localIP().toString());
            content.set("fields/lastConnectiontemp/stringValue", "now");

            if (Firebase.Firestore.patchDocument(&fbdo, firebaseProjectId.c_str(), "", path.c_str(), content.raw(), "connected,lastConnectiontemp,ipaddress")) {
              connectionReported = true;
              if (storedSuccess != "success") {
                writeEEPROM(successAddr, "success", successMaxLen);
                storedSuccess = "success";
              }
              writeHeartbeat();
              lastHeartbeat = millis();
              Serial.println("Connection info posted to Firestore.");
            } else {
              Serial.print("Connection post failed: "); Serial.println(fbdo.errorReason());
            }
          }

          // 6. Upload queued alarm / electrode events (oldest first, kept until accepted)
          CloudEvent evt;
          if (connectionReported && millis() - lastEventAttempt >= EVENT_RETRY_MS
              && xQueuePeek(xEventQueue, &evt, 0) == pdTRUE) {
            if (uploadEvent(evt)) {
              xQueueReceive(xEventQueue, &evt, 0);
              lastEventAttempt = 0;  // Send the next one right away
            } else {
              lastEventAttempt = millis();
            }
          }

          // 7. Heartbeat so the server can tell the caregiver if the device goes offline
          if (connectionReported && millis() - lastHeartbeat >= HEARTBEAT_INTERVAL_MS) {
            lastHeartbeat = millis();
            writeHeartbeat();
          }

          // 8. Poll Firestore for app commands
          if (connectionReported && millis() - lastPoll >= POLL_INTERVAL_MS) {
            lastPoll = millis();

            if (Firebase.Firestore.getDocument(&fbdo, firebaseProjectId.c_str(), "", path.c_str(), "connected,startAlarm,stopAlarm,resetAll")) {
              FirebaseJson payload;
              payload.setJsonData(fbdo.payload().c_str());
              FirebaseJsonData jsonData;

              // Check Connection Handshake Field
              payload.get(jsonData, "fields/connected/stringValue", true);
              if (jsonData.stringValue == "3") {
                setFirestoreValue("lastConnectiontemp", "now");
                vTaskDelay(pdMS_TO_TICKS(1000));
                setFirestoreValue("connected", "4");
              }

              // Check Remote Start Alarm Command
              jsonData.clear();
              payload.get(jsonData, "fields/startAlarm/stringValue", true);
              if (jsonData.stringValue == "1") {
                setFirestoreValue("startAlarm", "0");
                remoteAlarmStarted = true;
                Trigger = 1; // UI task moves to Alert_State from any menu
              }

              // Check Remote Stop Alarm Command
              // Only a request here; the UI task silences and returns to Run Device.
              jsonData.clear();
              payload.get(jsonData, "fields/stopAlarm/stringValue", true);
              if (jsonData.stringValue == "1") {
                setFirestoreValue("stopAlarm", "0");
                if (MenuState == Alert_State) remoteStopRequested = true;
              }

              // Check Remote Reset All Settings Command
              jsonData.clear();
              payload.get(jsonData, "fields/resetAll/stringValue", true);
              if (jsonData.stringValue == "1") {
                setFirestoreValue("resetAll", "0");
                clearStoredCredentials();
                ESP.restart();
              }
            } else {
              Serial.print("Poll failed: "); Serial.println(fbdo.errorReason());
            }
          }
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(100)); // Yield to prevent CPU starvation
  }
}

// ==================== EEPROM & HELPER FUNCTIONS ====================
  // Writes at most maxLen-1 characters plus a terminating null, so a long
  // value can never spill into the next field.
  void writeEEPROM(int addr, String data, int maxLen) {
    int len = min((int)data.length(), maxLen - 1);
    for (int i = 0; i < len; ++i) {
      EEPROM.write(addr + i, data[i]);
    }
    EEPROM.write(addr + len, '\0');
    EEPROM.commit();
  }

  // Always null-terminated. 0xFF (blank flash) also ends the string.
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
    Serial.println("Stored WiFi credentials cleared.");
  }

  bool setFirestoreValue(String key, String value) {
    FirebaseJson content;
    content.set("fields/" + key + "/stringValue", value);
    if (Firebase.Firestore.patchDocument(&fbdo, firebaseProjectId.c_str(), "", path.c_str(), content.raw(), key)) {
      return true;
    }
    Serial.print("Write of "); Serial.print(key); Serial.print(" failed: "); Serial.println(fbdo.errorReason());
    return false;
  }

  // lastSeen = Firestore server time (not the device clock), so the offline
  // check compares two server times.
  bool writeHeartbeat() {
    std::vector<struct firebase_firestore_document_write_t> writes;
    struct firebase_firestore_document_write_t transform_write;
    transform_write.type = firebase_firestore_document_write_type_transform;
    transform_write.document_transform.transform_document_path = path.c_str();

    struct firebase_firestore_document_write_field_transforms_t field_transforms;
    field_transforms.fieldPath = "lastSeen";
    field_transforms.transform_type = firebase_firestore_transform_type_set_to_server_value;
    field_transforms.transform_content = "REQUEST_TIME";
    transform_write.document_transform.field_transforms.push_back(field_transforms);
    writes.push_back(transform_write);

    if (Firebase.Firestore.commitDocument(&fbdo, firebaseProjectId.c_str(), "", writes, "")) {
      return true;
    }
    Serial.print("Heartbeat failed: "); Serial.println(fbdo.errorReason());
    return false;
  }

  // Writes ALSAlert/{uid}/events/{id} = {type, source, delayMs, at: server time}.
  // The id is fixed when the event is raised, so a retry rewrites the same
  // document instead of creating a duplicate push.
  // delayMs = how long the event waited on the device (e.g. while offline);
  // the real event time is at - delayMs.
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
      Serial.print("Event uploaded: "); Serial.println(typeStr);
      return true;
    }
    Serial.print("Event upload failed (will retry): "); Serial.println(fbdo.errorReason());
    return false;
  }

  // Called from the UI task. Never blocks; if the queue is full the oldest
  // pending events are still delivered and this one is dropped.
  void queueCloudEvent(uint8_t type, uint8_t source) {
    CloudEvent evt;
    evt.type = type;
    evt.source = source;
    evt.id = esp_random();
    evt.raisedAt = millis();
    if (xQueueSend(xEventQueue, &evt, 0) != pdTRUE) {
      Serial.println("Event queue full, event dropped");
    }
  }

  // Returns the raw (still encoded) value of key in "?a=1&b=2", or "" if missing.
  String getQueryParam(const String& query, const String& key) {
    int start = -1;
    String pattern = "?" + key + "=";
    start = query.indexOf(pattern);
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

  // Decodes %XX escapes. '+' is kept as a literal '+' (the app must encode
  // spaces as %20, e.g. with Uri.encode), so a '+' in a password survives.
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
    // request is the first line, e.g. "GET /?ssid=...&password=... HTTP/1.1"
    int queryStart = request.indexOf('?');
    int queryEnd   = request.lastIndexOf(" HTTP/");
    String query = (queryStart != -1 && queryEnd > queryStart) ? request.substring(queryStart, queryEnd) : "";

    String ssidStr  = urlDecode(getQueryParam(query, "ssid"));
    String passStr  = urlDecode(getQueryParam(query, "password"));
    String emailStr = urlDecode(getQueryParam(query, "email"));
    String epassStr = urlDecode(getQueryParam(query, "epassword"));
    String apiKeyStr = urlDecode(getQueryParam(query, "apikey"));      // Optional: builder's own project
    String projIdStr = urlDecode(getQueryParam(query, "projectid"));   // Optional: builder's own project

    if (ssidStr.length() > 0 && query.indexOf("password=") != -1) {
      if (ssidStr.length() >= ssidMaxLen || passStr.length() >= passMaxLen ||
          emailStr.length() >= emailMaxLen || epassStr.length() >= epassMaxLen ||
          apiKeyStr.length() >= apiKeyMaxLen || projIdStr.length() >= projIdMaxLen) {
        client.println("HTTP/1.1 400 Bad Request\r\nContent-Type: text/html\r\n\r\n<!HTML><html><body><h1>A value is too long</h1></body></html>");
        client.stop();
        return;
      }

      writeEEPROM(ssidAddr, ssidStr, ssidMaxLen);
      writeEEPROM(passAddr, passStr, passMaxLen);
      writeEEPROM(emailAddr, emailStr, emailMaxLen);
      writeEEPROM(epassAddr, epassStr, epassMaxLen);
      writeEEPROM(successAddr, "", successMaxLen);   // New credentials are unproven until they connect
      writeEEPROM(apiKeyAddr, apiKeyStr, apiKeyMaxLen);
      writeEEPROM(projIdAddr, projIdStr, projIdMaxLen);

      client.println("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!HTML><html><body><h1>Credentials Saved! Restarting...</h1></body></html>");
      client.stop();
      vTaskDelay(pdMS_TO_TICKS(1000));
      ESP.restart();
    } else {
      client.println("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!HTML><html><body><h1>ALS Alert Provisioning</h1><p>Send SSID/Pass via form.</p></body></html>");
      client.stop();
    }
  }
