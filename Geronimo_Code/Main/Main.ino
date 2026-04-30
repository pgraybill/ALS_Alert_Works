

/////////////////////////////////////////////////////////////////////////////////
/////////////////////////ALS Alert Team LCD Screen Code/////////////////////////
///////////////////////////////////////////////////////////////////////////////



/////////////////////////Include libraries/////////////////////////
  #include <Wire.h>               //open I^2C library up
  #include <SSD1803A_I2C.h>       //Library for DOGS164-A's controller: SSD1803A
  #include <Bounce2.h>            //open debouncing library for buttons
  #include <Preferences.h>


/////////////////////////Variables/////////////////////////

     //Pin Declarations: HERE: Fill this in with the actual pin needed
    const int SDA_ESP32 = 2; 
    const int SCL_ESP32 = 3; 
    const int Reset_LCD = 0;
    const int Scroll = 1; 
    const int EOG_Pin = 6;
    const int Select = 8;
    const int Alarm = 10;
    const int Reset_Button = 26;
    const int Cal_LED = 23;
    const int CompPin = 27; //Comparator Pin for electrode contact
    const int Battery = 4;
    const int Wall = 5;
    const int CompPin2 = 14; //Comparator Pin for electrode contact

   
  //Trigger Detection Variables
    //Parameters
      uint16_t Upper_Thresh = 2700;
      uint16_t Lower_Thresh = 500;
      const uint8_t NOEM = 8;
      uint16_t MET = 13000; // Move Expiration time in mS
      uint16_t SLET = 1500; // Single Look Expiration Time in mS
      float PO = 0.41;

    //Things You Might want to Change
      uint16_t Cal_Thresh = 5000; //How Long Calibration Lasts
      uint8_t RA = 10; //How Large of a rolling average is used for calibration data

    //Timing Things
      uint8_t Samp_Per = 20; //Sample Period in mS
      unsigned long Time_Samp = 0; //Time of previous sample
      unsigned long Time_Look = 0; //This is the time that a look was detected

    //EOG Signal Stuff
      float Sig = 0; //This is the signal voltage
      float Sig_Prev = 0; //Previous signal value
      uint16_t Sig_Rol= 0; //This is the rolling average of signal voltage used in calibration
      uint16_t Sig_Rol_Prev = 0; //This is the previous value of the rolling average

    //Looks and Moves!
      uint8_t Move[NOEM] = {0};
      unsigned long Time_Move[NOEM] = {0}; //This saves when the moves were recorded
      bool Up_Look_Allow = 1;
      bool Down_Look_Allow = 1;
      uint8_t Look = 0; //This counts the number of looks 
      uint8_t ii = 0; //For incrementing through for loops to resaves values and things
      bool MoveAdded = 0;
    
    //Calibration
      unsigned long Time_Cal = 0; //Time Calibration started
      unsigned long Time_Cal_Alt = 0; //Time Calibration started
      unsigned long Time_Cal_Alt_Mini = 0; //Time Calibration started
      uint16_t Cal_Thresh_Alt = 3000;
      uint8_t Cal_Count = 0;
      unsigned long BeepTime = 0;
      uint16_t BeepThresh = 500;
      uint16_t BeepThresh2 = 1000;
      bool Toggle = 0;
    

      bool Done = 0;

      uint8_t Cal = 0;

      //Slope values are used for determinging maxes and mins
      float Slope = 0; //Technically the rolling average of slope 
      float Slope_Inst = 0;
      float Slope_Prev = 0;
      uint16_t Midpoint = 0;


      //This count how many maxes and mins there were to divide by sum total by later
      uint16_t Count_Max = 0;
      uint16_t Count_Min = 0;
   
    //Misc
      uint8_t Printed = 0;
      uint8_t Trigger = 0; //0 = no trigger, 1 = trigger, 2 = electrode 1 loose, 3 = electrode 2 loose
      bool ElectrodeLoose = 0;
      bool ElectrodeLoose2 = 0;
      
      

  //LCD Variables
    //power check variables
      int W_power;
      int Wp_prev;
      int B_power;
      int Bp_prev;

    //variables for blinking battery alert
      int Blink = 0;              //keeps track of blink state
      const int Blink_dly = 400;  //blink time
      unsigned long B_Alt_delay;  //timer

    //variables for alarm
      int AlertOn; //is alert on?
      int AlertType; //what alert is it? (trigger or electrode)

    //variables for blinking arrows
      int B_Blink = 0;            //keeps track of blink state
      const int Button_dly = 500; //blink time
      unsigned long Button_blink; //timer

    //variables for debouncing buttons
      Bounce scrollButton = Bounce(); //create bounce for scoll button
      Bounce selButton = Bounce();    //create bounce for scoll button
      Bounce resetButton = Bounce(); //create bounce for reset button (big red button!!!)

    //Set up SSD1803A constructor
      const uint8_t LCD_Address = 0x3C;
      SSD1803A_I2C lcd (LCD_Address);

/////////////////////////Create FSMs/////////////////////////

  //Menu states//
  enum Menu {
    Main_Menu,
    Run_Device,
    Battery_Menu,
    Calibration_Menu,
    Alert_State

  };
  Menu MenuState = Main_Menu;  //create menu variable

  //selection states//
  enum Sel {
    opt1,
    opt2,
    opt3

  };
  Sel SelState = opt1;  //create selection variable

  ///////////////////////////////////////////////////////////////////

/////////////////////////Void Setup -- Initialize LCD screen/////////////////////////
  void setup() {
    //set up pin modes
      pinMode(EOG_Pin,INPUT);
      pinMode(Wall, INPUT);     //temporary pin representing wall power
      pinMode(Battery, INPUT);  //temporary pin representing battery power
      pinMode(Cal_LED,OUTPUT);
      pinMode(Alarm,OUTPUT);
      
      pinMode(Scroll, INPUT);   //scroll button pin (pulled up externally with 10kohms)
      pinMode(Select, INPUT);   //select button pin(pulled down externally with 10kohms)
      pinMode (Reset_Button, INPUT_PULLDOWN); //reset button (BIG RED BUTTON) switch to be externally pulled up!!! 

      pinMode(CompPin,INPUT);   //Looks at comparitor signal  
      pinMode(CompPin2,INPUT);   //Looks at comparitor signal  

    //set up I2C pins
      Wire.begin(SDA_ESP32,SCL_ESP32);

    //attach button pins to button debounce objects
      scrollButton.attach(Scroll);
      selButton.attach(Select);
      resetButton.attach(Reset_Button); 

    //set debounce interval
      scrollButton.interval(20);
      selButton.interval(20);
      resetButton.interval(20);


    //Start Counters
      B_Alt_delay = millis();
      W_power = 1;
      B_power = 1;
      Wp_prev = 1;
      Bp_prev = 1;

    //initialize display and reset it with reset pin
      lcd.begin(DOGS164, Reset_LCD); 

    //set contrast 
      lcd.display(CONTRAST, 54);
    
    //Startup message
      lcd.display(SET_ROM_A);
      lcd.cls();
      lcd.display(VIEW_BOTTOM);
      lcd.display(LINES_3_2); //switch to 2 line mode
      lcd.locate(2,1);                  
      lcd.print("   Device  ON");  //text on screen showing that device is on
      delay(1000);

    //Code for Menu display
      lcd.cls();
      lcd.display(LINES_4);
      lcd.locate(1,1);
      lcd.print("    MAIN MENU");
      lcd.locate(2,1);
      lcd.print("Calibrate    ***");
      lcd.locate(3,1);
      lcd.print("Battery Menu    ");
      lcd.locate(4,1);
      lcd.print("Run Device");

    //Set Baudrate for Serial Monitor
      Serial.begin(115200);


  }

///////////////////////////////////////////VOID LOOP////////////////////////////////////////////////
void loop(){
  //Serial.println("Printing is working");
  scrollButton.update();  //constantly update the states of the buttons
  selButton.update();
  resetButton.update(); 

  //states of variables
  Wp_prev = W_power;               //store last state of wall power
  Bp_prev = B_power;               //store last state of battery power
  W_power = digitalRead(Wall);     //check wall power
  B_power = digitalRead(Battery);  //check battery power
  

 /////////////////////////FSM for Menus/////////////////////////
  switch (MenuState) {

   ////////////////////////////////////////
   ///////////Main Menu options///////////
   ///////////////////////////////////////
    case Main_Menu:


      //Options selection
      switch (SelState) {
        Serial.println("Main Menu"); //HERE

        /////Calibration/////
        case opt1:  

          if (scrollButton.fell()) {
            SelState = opt2;
            lcd.locate(3,1);
            lcd.print("Battery Menu ***");
        
            lcd.clr(2);
            lcd.locate(2,1); //erase cursor
            lcd.print("Calibrate    ");

          } else if (selButton.rose()) {
            MenuState = Calibration_Menu;
            SelState = opt1;

            lcd.cls(); //clear screen for calibration stuff

          }
          break; //break for opt1

        /////Battery menu/////
        case opt2: 

          if (scrollButton.fell()) {
            SelState = opt3;
            lcd.locate(4,1); //move cursor
            lcd.print("Run Device   ***");

            lcd.clr(3);
            lcd.locate(3,1);  //erase cursor from previous space
            lcd.print("Battery Menu  ");


          } else if (selButton.rose()) {
            MenuState = Battery_Menu;
            SelState = opt1;
            lcd.cls();

          }
          break; //break for opt2

        /////run device/////
        case opt3:  

          if (scrollButton.fell()) {
            SelState = opt1;
           
            lcd.locate(2,1);
            lcd.print("Calibrate    ***");

            lcd.clr(4);
            lcd.locate(4,1);
            lcd.print("Run Device");

          } else if (selButton.rose()) {
            MenuState = Run_Device;
            SelState = opt1;
            lcd.display(LINES_3_2);
            //Code for Run Device
            lcd.cls();
            lcd.locate(2,1);
            lcd.print(" Device Running");  //say device running by default
          }
          break; //break for opt3
      }
      break; //break for Main_Menu

      //////////////////////////////////
      ///////////Calibration///////////
      /////////////////////////////////
    case Calibration_Menu:

      Time_Cal_Alt = millis();
      Cal_Count = 0;
      Done = 0;

        while (Done == 0){
          Cal_Count = Cal_Count+1;

          if(Cal_Count == 1){
            Time_Cal_Alt_Mini = millis();

            //Display: "Calibrating:3"
            lcd.display(LINES_3_3);
            lcd.locate(1,1);
            lcd.print(" CALIBRATING IN");
            lcd.locate(3,1);
            lcd.print("       3");

            Serial.println("3");
            while (millis()-Time_Cal_Alt_Mini <= 1000){}

          }else if (Cal_Count == 2){
            Time_Cal_Alt_Mini = millis();

            //Display: "Calibrating: 2" 
            lcd.display(LINES_3_3);
            lcd.locate(3,1);
            lcd.print("       2");

            Serial.println("2");
            while (millis()-Time_Cal_Alt_Mini <= 1000){}

          }else if (Cal_Count == 3){
            Time_Cal_Alt_Mini = millis();

            //Display: "Calibrating: 1" 
            lcd.display(LINES_3_3);
            lcd.locate(3,1);
            lcd.print("       1");

            Serial.println("1");
            while (millis()-Time_Cal_Alt_Mini <= 1000){}

          }else if (Cal_Count == 4){
            Time_Cal_Alt_Mini = millis();

            //Display: "Calibrating: GO" 
            lcd.display(LINES_3_3);
            lcd.locate(3,1);
            lcd.print("       GO!");

            Serial.println("GO!");
            while (millis()-Time_Cal_Alt_Mini <= 1000){}

            Done = 1;
          }
        }
        
        Count_Max = 1;
        Count_Min = 1;
        Serial.print(Lower_Thresh); Serial.print("\t"); Serial.println(Upper_Thresh);

        Time_Cal_Alt = millis();
        Printed = 0;
        Cal = 1;
        Up_Look_Allow = 1;
        Down_Look_Allow = 1;
        Time_Cal = millis();
        while (Cal == 1){

            if (Printed == 0){
              //Jasmine : I would replace this bottom part if possible
              Serial.println("Start Active Calibration: Look Left and right");
              Printed = 1;
            }
            
            Sig = analogReadMilliVolts(EOG_Pin);

            Sig_Rol_Prev = Sig_Rol; 

            Sig_Rol= Sig_Rol*0.95 + Sig*0.05;

            Slope_Prev = Slope;
            Slope_Inst = Sig_Rol- Sig_Rol_Prev;
            Slope = Slope*0.95 + Slope_Inst*0.05;

            //Serial.print(Slope); Serial.print("\t"); Serial.print(Slope_Prev); Serial.print("\t"); Serial.print(-45); Serial.print("\t"); Serial.println(45);

            if (millis()-Time_Samp >= 100){
              if ((Slope < 0.0) && (Slope_Prev >= 0.0) && (Up_Look_Allow)){ //Maximums here
                Time_Samp = millis();
                Count_Max = Count_Max + 1;
                Upper_Thresh = Upper_Thresh + Sig_Rol;
                Up_Look_Allow = 0;
                Down_Look_Allow = 1;

              Serial.print("New Upper Thresh"); Serial.print("\t"); Serial.print(Sig_Rol); Serial.print("\t"); Serial.println(Sig_Rol_Prev);
              
              }else if ((Slope > 0.0) && (Slope_Prev <= 0.0) && (Down_Look_Allow)){ //Minimums Here
                Time_Samp = millis();
                Count_Min = Count_Min + 1;
                Lower_Thresh = Lower_Thresh + Sig_Rol;
                Up_Look_Allow = 1;
                Down_Look_Allow = 0;

              Serial.print("New Lower Thresh"); Serial.print("\t"); Serial.print(Sig_Rol); Serial.print("\t"); Serial.println(Sig_Rol_Prev);
              
            }
            }     

              if ((millis() - Time_Cal) > Cal_Thresh) {
                Cal = 0;
              }

          }//End of while

          Upper_Thresh = Upper_Thresh/Count_Max;
          Lower_Thresh = Lower_Thresh/Count_Min;

          Midpoint = Upper_Thresh - Lower_Thresh;

          Upper_Thresh = Midpoint + (Upper_Thresh - Midpoint)*PO;
          Lower_Thresh = Midpoint - (Midpoint - Lower_Thresh)*PO;

          Serial.println("Done");
          Serial.print(Lower_Thresh); Serial.print("\t"); Serial.println(Upper_Thresh);      

        MenuState = Main_Menu;
        //Code for Menu display
        lcd.cls();
        lcd.display(LINES_4);  // Set to 4-line for main menu
        lcd.locate(1,1);
        lcd.print("    MAIN MENU");
        lcd.locate(2,1);
        lcd.print("Calibrate    ***");
        lcd.locate(3,1);
        lcd.print("Battery Menu    ");
        lcd.locate(4,1);
        lcd.print("Run Device    ");
      break; //break for Calibration_Menu


      ////////////////////////////////
      ///////////Battery Menu///////////
      ///////////////////////////////
    case Battery_Menu: 
      //Checking if battery power low
      //Serial.println("Battery Menu"); //HERE
      if  (selButton.rose()) {

        MenuState = Main_Menu;
        SelState = opt1;
        //Code for Menu display
        lcd.cls();
        lcd.display(LINES_4);  // Set to 4-line for main menu
        lcd.locate(1,1);
        lcd.print("    MAIN MENU");
        lcd.locate(2,1);
        lcd.print("Calibrate    ***");
        lcd.locate(3,1);
        lcd.print("Battery Menu    ");
        lcd.locate(4,1);
        lcd.print("Run Device    ");
        W_power = 1;
        B_power = 1;
        Wp_prev = 1;
        Bp_prev = 1;

      } else {
        if (B_power == 0 && Bp_prev == 0) {
            
              lcd.display(LINES_3_2); //set words to middle
              lcd.locate(1,1);
              lcd.print("                    ");
              //Blink the words "Battery Low"
              if (millis() - B_Alt_delay > Blink_dly) {
                if (Blink == 0) {
                  Blink = 1;
                  lcd.locate(2,1);
                  lcd.print("                 ");
                } else if (Blink == 1) {
                  Blink = 0;
                  lcd.locate(2,1);
                lcd.print("  Battery  Low  ");
                }
                B_Alt_delay = millis();  //restart timer
              }


        } else if (B_power == 1) {
            lcd.display(LINES_3_2);  
            lcd.locate(2,1);
            lcd.print("Battery  Charged"); //display "Battery Charged"
        }
      }
      break; //break for Battery_Menu


      /////////////////////////////////
      ///////////Run Device///////////
      ////////////////////////////////
    case Run_Device:
      //Serial.println("Made it to run device");
      while (Trigger == 0) {
          scrollButton.update();  //constantly update the states of the buttons
          selButton.update();
          resetButton.update(); 
        //Serial.println("Made it into while loop");
        if  (selButton.rose()) {  //press select to return to main menu
          //Serial.println("Button Pressed Detected Should be leaving this detection mode");
          MenuState = Main_Menu;
          SelState = opt1;
          //Code for Menu display
          lcd.cls();
          lcd.display(LINES_4);  // Set to 4-line for main menu
          lcd.locate(1,1);
          lcd.print("    MAIN MENU");
          lcd.locate(2,1);
          lcd.print("Calibrate    ***");
          lcd.locate(3,1);
          lcd.print("Battery Menu    ");
          lcd.locate(4,1);
          lcd.print("Run Device    ");
          W_power = 1;
          B_power = 1;
          Wp_prev = 1;
          Bp_prev = 1;
          //Serial.println("Should Break");
          break; 
      
        }else {
          ////// When this is uncommented you can just print the raw EOG signal to make sure it makes sense //////////
            //Serial.println("Made it to trigger detection code");
            // while (1){
            //   Sig = analogReadMilliVolts(EOG_Pin);
            //   Serial.print(Sig); Serial.print("\t"); Serial.print(0); Serial.print("\t"); Serial.println(3500);
            //   delay(25);
            // }

            ///////Poor Man's Break Point, copy and paste where needed///////
            // Button = 0;
            // while(Button == 0){
            //   Button = digitalRead(But_Pin);
            // }
            // while(Button == 1){
            //   Button = digitalRead(But_Pin);
            // }
        
          //Take Sample: Read the analog signal from EOG
            Sig_Prev = Sig; //Update previous signal value
            Sig = analogReadMilliVolts(EOG_Pin); //Records sample value in mV
            Time_Samp = millis(); //Save the time the sample was taken

          //Checking for look
            if ((Sig > Upper_Thresh) && (Sig_Prev < Upper_Thresh) && (Up_Look_Allow == 1)){
              Look = Look + 1;
              Time_Look = millis();
              Up_Look_Allow = 0;
              Down_Look_Allow = 1;
              Serial.println("An Upper Look was Detected");
            }
          //Checks for look in other direction
          if ((Sig < Lower_Thresh) && (Sig_Prev > Lower_Thresh) && (Down_Look_Allow == 1)){
            Look = Look + 1;
            Time_Look = millis();
            Up_Look_Allow = 1;
            Down_Look_Allow = 0;
            Serial.println("A Lower Look was Detected");
          }

          //Checking Look Expiration Time
          if (((millis() - Time_Look) >= SLET) && (Time_Look != 0)){
            Serial.println("Look Removed");
            Look = 0;
            Time_Look = 0;
          }

          //Adding a move
          if (Look >= 2){
            Look = 0;
            Time_Look = 0;
            Serial.println("Move Detected!");
            for (ii = 0; ii < NOEM; ii++){
              if ((Move[ii] == 0) && (MoveAdded == 0)){
                Serial.println(ii);
                Move[ii] = 1;
                Time_Move[ii] = millis();
                Serial.println("It Added a Move");
                MoveAdded = 1; //This move added is essential because it stops it from filling up array each time
              }
            }
          MoveAdded = 0; //Reset value so another move can be added next time
          }

          //Checking for Alarm
          if (Move[NOEM-1] == 1){//If the array is all the way filled up
            Trigger = 1;
            MenuState = Alert_State;
            Serial.println("Trigger Detected");
            Look = 0;
            Up_Look_Allow = 1;
            Down_Look_Allow = 1;

            //Reset Move Values
            for(ii = 0; ii < NOEM; ii++){
              Move[ii] = 0;
              Time_Move[ii] = 0; 
            }
          }

          //Checking Move Expiration Time
            if (((millis() - Time_Move[0]) >= MET) && (Time_Move[0] != 0)){
              Serial.println("Move Removed");
              for(ii = 0; ii < NOEM-1; ii++){
                Move[ii] = Move[ii+1];
                Time_Move[ii] = Time_Move[ii+1];
              }
            }

          //Keep the program waiting until the Sample Period has elapsed
          while((millis()-Time_Samp) <= Samp_Per) {
          }   
          //////////////////////////////////////////////////////////////////////////////////   

          //states of variables
              Wp_prev = W_power;               //store last state of wall power
              Bp_prev = B_power;               //store last state of battery power
              W_power = digitalRead(Wall);     //check wall power
              B_power = digitalRead(Battery);  //check battery power
              
          //Code checking device power//
              if (W_power == 0 && Wp_prev == 1) {  //if wall power just turned off
              lcd.cls();
              lcd.display(LINES_2);
              lcd.locate(1,4); 
              lcd.print("Wall power");
              lcd.locate(2,3);
              lcd.print("disconnected");
              Serial.println("Wall Power Disconnected");

              } else if (W_power == 0 && Wp_prev == 0) {  //if wall power has been out

                //Checking if battery power low
                if (B_power == 0 && Bp_prev == 0) {
                
                  lcd.display(LINES_3_2); //set words to middle
                  lcd.locate(1,1);
                  lcd.print("                    ");
                  //Blink the words "Battery Low"
                  if (millis() - B_Alt_delay > Blink_dly) {
                    if (Blink == 0) {
                      Blink = 1;
                      lcd.locate(2,1);
                      lcd.print("                 ");
                    } else if (Blink == 1) {
                      Blink = 0;
                      lcd.locate(2,1);
                    lcd.print("  Battery  Low  ");
                    Serial.println("Battery Low");
                    }
                    B_Alt_delay = millis();  //restart timer
                  }


                } else if (B_power == 1 && Bp_prev == 0) {
                  
                lcd.locate(2,1);
                lcd.print("Battery  Charged"); //display "Battery Charged"
                }

              } else if (W_power == 1 && Wp_prev == 0) {
                
                lcd.cls();  //clear screen
                lcd.display(LINES_3_2);
                lcd.locate(2,1);
                lcd.print(" Device Running");  //display "Device Running"
              }
        
  
          ElectrodeLoose = digitalRead(CompPin);
          ElectrodeLoose2 = digitalRead(CompPin2);
          
        if (ElectrodeLoose == 1){
          Trigger = 2;
          MenuState = Alert_State;
        }
        if (ElectrodeLoose2 == 1){
          Trigger = 3;
          MenuState = Alert_State;
        }
        }//End of else for no button pressed
      }//End of Detection while loop Loop

      
      break; //break for Run Device State

    /////////////////////////////////
    ///////////ALERT STATE///////////
    ////////////////////////////////
    case Alert_State:

      if (Trigger == 1) {
        while (resetButton.read() == LOW){ //CHECK WITH JERON ABOUT IF ACTIVE LOW OR ACTIVE HIGH!!
          
          ///CODE to trigger alarm for a real trigger!!!!
          lcd.display(LINES_4);
          lcd.locate(1,1);
          lcd.print("Alarm Triggered");
          lcd.locate(2,1);
          lcd.print("Hit Red button");
          lcd.locate(3,1);
          lcd.print("to reset device");

          resetButton.update(); //update reset button's state

          if (millis() - BeepTime >= BeepThresh){
            BeepTime = millis();
            Toggle = !Toggle;
            digitalWrite(Alarm,Toggle);
          }
        }
          digitalWrite(Alarm,LOW);

         
        //Code for Menu display
        lcd.cls();
        lcd.display(LINES_4);  // Set to 4-line for main menu
        lcd.locate(1,1);
        lcd.print("    MAIN MENU");
        lcd.locate(2,1);
        lcd.print("Calibrate    ***");
        lcd.locate(3,1);
        lcd.print("Battery Menu    ");
        lcd.locate(4,1);
        lcd.print("Run Device    "); 
        //Put line here to turn off alarm
        //Put line here to turn off alarm
        // Send to main menu to reset device
        MenuState = Main_Menu;

      } else if (Trigger == 2){ //Electrode 1 disconnnected
        while (resetButton.read() == LOW){
          //Serial.println("Left Loose Electrode");
          //CODE for beeping alarm for electrode connection
          lcd.display(LINES_4);
          lcd.locate(1,1);
          lcd.print("Left Electrode");
          lcd.locate(2,1);
          lcd.print("Loose           ");
          lcd.locate(3,1);
          lcd.print("Hit Red button");
          lcd.locate(4,1);
          lcd.print("to reset device");

          resetButton.update(); //update reset button's state

          if (millis() - BeepTime >= BeepThresh2){
            BeepTime = millis();
            Toggle = !Toggle;
            digitalWrite(Alarm,Toggle);
          }
        }
        digitalWrite(Alarm,LOW);
        
        //Code for Menu display
        lcd.cls();
        lcd.display(LINES_4);  // Set to 4-line for main menu
        lcd.locate(1,1);
        lcd.print("    MAIN MENU");
        lcd.locate(2,1);
        lcd.print("Calibrate    ***");
        lcd.locate(3,1);
        lcd.print("Battery Menu    ");
        lcd.locate(4,1);
        lcd.print("Run Device    "); 
        //Put line here to turn off alarm
        //Send to main menu to reset device
        MenuState = Main_Menu;

      }else if (Trigger == 3){//Electrode 2 disconnected
        while (resetButton.read() == LOW){
          //Serial.println("Right Loose Electrode");
          //CODE for beeping alarm for electrode connection
          lcd.display(LINES_4);
          lcd.locate(1,1);
          lcd.print("Right Electrode ");
          lcd.locate(2,1);
          lcd.print("Loose            ");
          lcd.locate(3,1);
          lcd.print("Hit Red button");
          lcd.locate(4,1);
          lcd.print("to reset device");

          resetButton.update(); //update reset button's state
          if (millis() - BeepTime >= BeepThresh2){
            BeepTime = millis();
            Toggle = !Toggle;
            digitalWrite(Alarm,Toggle);
          }
        }
        digitalWrite(Alarm,LOW);
        
        //Code for Menu display
        lcd.cls();
        lcd.display(LINES_4);  // Set to 4-line for main menu
        lcd.locate(1,1);
        lcd.print("    MAIN MENU");
        lcd.locate(2,1);
        lcd.print("Calibrate    ***");
        lcd.locate(3,1);
        lcd.print("Battery Menu    ");
        lcd.locate(4,1);
        lcd.print("Run Device    "); 
        //Put line here to turn off alarm
        //Send to main menu to reset device
        MenuState = Main_Menu;
      }
    Trigger = 0;
    break; //break Alert_State
  }//end of MenuState FSM
}//end of void loop

/////////////////////////////////////////////////////////////////
///////////////////////////END OF CODE///////////////////////////
/////////////////////////////////////////////////////////////////
