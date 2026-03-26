

/////////////////////////////////////////////////////////////////////////////////
/////////////////////////ALS Alert Team LCD Screen Code/////////////////////////
///////////////////////////////////////////////////////////////////////////////



/////////////////////////Include libraries/////////////////////////
#include <Wire.h>               //open I^2C library up
#include <SSD1803A_I2C.h>       //Library for DOGS164-A's controller: SSD1803A
#include <Bounce2.h>            //open debouncing library for buttons


/////////////////////////Variables/////////////////////////

//pin variables
const int Reset = 5;  //was D7
const int Battery = 9; //was D6
const int Wall = 8;    //was D5
const int Select = 6;  //was D8
const int Scroll = 7; //was D4
const int SDA_ESP32 = 3; //was D1
const int SCL_ESP32 = 4; //was D2

//power check variables
int W_power;
int Wp_prev;
int B_power;
int Bp_prev;

//variables for blinking battery alert
int Blink = 0;              //keeps track of blink state
const int Blink_dly = 400;  //blink time
unsigned long B_Alt_delay;  //timer

//variables for blinking buttons
int B_Blink = 0;            //keeps track of blink state
const int Button_dly = 500; //blink time
unsigned long Button_blink; //timer

//variables for debouncing buttons
Bounce scrollButton = Bounce(); //create bounce for scoll button
Bounce selButton = Bounce();    //create bounce for scoll button

//Set up SSD1803A constructor
const uint8_t LCD_Address = 0x3C;
SSD1803A_I2C lcd (LCD_Address);


/////////////////////////Create FSMs/////////////////////////

//Menu states//
enum Menu {
  Main_Menu,
  Run_Device,
  Battery_Menu,
  Calibration_Menu

};
Menu MenuState = Main_Menu;  //create menu variable

 //selection states//
enum Sel {
  opt1,
  opt2,
  opt3

};
Sel SelState = opt1;  //create selection variable

 //Calibration pages//
enum Page {
  page1,
  page2,
  page3
 };
 Page page = page1; //create page variable


/////////////////////////Void Setup -- Initialize LCD screen/////////////////////////
void setup() {
  //set up pin modes
  pinMode(Wall, INPUT);     //temporary pin representing wall power
  pinMode(Battery, INPUT);  //temporary pin representing battery power
  //pinMode(Reset, OUTPUT);   //temporary reset pin
  pinMode(Scroll, INPUT);   //scroll button pin (pulled up externally with 10kohms)
  pinMode(Select, INPUT);   //select button pin(pulled down externally with 10kohms)

  //set up I2C pins
  Wire.begin(SDA_ESP32,SCL_ESP32);

  //debounce button pins
  scrollButton.attach(Scroll);
  selButton.attach(Select);

  //set debounce interval
  scrollButton.interval(20);
  selButton.interval(20);


  //Start Counters
  B_Alt_delay = millis();
  W_power = 1;
  B_power = 1;
  Wp_prev = 1;
  Bp_prev = 1;

  //reset lcd screen
  lcd.begin(DOGS164, Reset); //initialize display and reset it

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

void loop(){
scrollButton.update();  //constantly update the states of the buttons
  selButton.update();

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
            //Code for Calibration Menu
            lcd.cls();
            lcd.display(LINES_3_1);
            lcd.locate(1,4);
            lcd.print("Calibration");
            lcd.locate(2,4);
            lcd.print("EMPTY PAGE");
            lcd.locate(3,4);
            lcd.print("FILL LATER");
          }
          break;

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
          break;

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
          break; //break for SelState
      }
      break; //break for MenuState

      //////////////////////////////////
      ///////////Calibration///////////
      /////////////////////////////////
    case Calibration_Menu:
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
        lcd.print("Battery Menu   ");
        lcd.locate(4,1);
        lcd.print("Run Device    ");
      }

      break; //break for MenuState


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
      break; //break for MenuState


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
        ////////////////////////////////////////
        //PUT all code to trigger alarm here!!//
        ///////////////////////////////////////

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
      }
      break;
  }//end of MenuState FSM


}//end of void loop

///////////////////////////END OF CODE///////////////////////////
