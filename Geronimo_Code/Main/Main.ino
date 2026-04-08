

/////////////////////////////////////////////////////////////////////////////////
/////////////////////////ALS Alert Team LCD Screen Code/////////////////////////
///////////////////////////////////////////////////////////////////////////////



/////////////////////////Include libraries/////////////////////////
#include <Wire.h>               //open I^2C library up
#include <SSD1803A_I2C.h>       //Library for DOGS164-A's controller: SSD1803A
#include <Bounce2.h>            //open debouncing library for buttons


/////////////////////////Variables/////////////////////////

//pin variables
const int Reset_LCD = 5;
const int Battery = 9;
const int Wall = 8;
const int Select = 6;
const int Scroll = 7; 
const int SDA_ESP32 = 3; 
const int SCL_ESP32 = 4; 
const int Reset_Button = 26;

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
  pinMode(Wall, INPUT);     //temporary pin representing wall power
  pinMode(Battery, INPUT);  //temporary pin representing battery power
  
  pinMode(Scroll, INPUT);   //scroll button pin (pulled up externally with 10kohms)
  pinMode(Select, INPUT);   //select button pin(pulled down externally with 10kohms)
  pinMode (Reset_Button, INPUT_PULLDOWN); //reset button (BIG RED BUTTON) switch to be externally pulled up!!! 

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

}

///////////////////////////////////////////VOID LOOP////////////////////////////////////////////////
void loop(){
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
      //temporary thing so don't get stuck in calibration!!
      if(selButton.rose()){
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

      } else{
        
        lcd.display(LINES_4);
        lcd.locate(1,1);
        lcd.print("PUT STUFF HERE ");
        lcd.locate(2,1);
        lcd.print("FOR CALIBRATION ");
        lcd.locate(3,1);
        lcd.print("                ");
        lcd.locate(4,1);
        lcd.print("                ");
      
    
      }
      //Put Calibration sequence here

      break; //break for Calibration_Menu


      ////////////////////////////////
      ///////////Battery Menu///////////
      ///////////////////////////////
    case Battery_Menu: 
      //Checking if battery power low
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


      if (selButton.rose()) {  //press select to return to main menu
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
        
        /// Instead of scrollButton.fell do AlertOn = 1; 
        if (scrollButton.fell()) {
           AlertType = 1; //Remove from here once have this somewhere below
           MenuState = Alert_State;
           lcd.cls();
            
        } else {


        ////////////////////////////////////////
        //PUT all code to detect trigger here!!//
        ///////////////////////////////////////

        //Somewhere in here set AlertOn = 1; when alarm needs to be triggered!!
        //set AlertType to appropriate value
       //Code checking device power//
            if (W_power == 0 && Wp_prev == 1) {  //if wall power just turned off
            lcd.cls();
            lcd.display(LINES_2);
            lcd.locate(1,4); 
            lcd.print("Wall power");
            lcd.locate(2,3);
            lcd.print("disconnected");

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

        } //end of check alert on if/else

      } //end check if select pressed if/else

    break; //break for Run Device State

    /////////////////////////////////
    ///////////ALERT STATE///////////
    ////////////////////////////////
    case Alert_State:

      if (AlertType == 1) {
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
        }

         
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

      } else if (AlertType == 0){
        while (resetButton.read() == LOW){

          //CODE for beeping alarm for electrode connection
          lcd.display(LINES_4);
          lcd.locate(1,1);
          lcd.print("Electrodes Loose");
          lcd.locate(2,1);
          lcd.print("Hit Red button");
          lcd.locate(3,1);
          lcd.print("to reset device");

          resetButton.update(); //update reset button's state
        }
        
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

    break; //break Alert_State



  
  }//end of MenuState FSM
}//end of void loop

/////////////////////////////////////////////////////////////////
///////////////////////////END OF CODE///////////////////////////
/////////////////////////////////////////////////////////////////