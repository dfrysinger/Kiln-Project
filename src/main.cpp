#include <Arduino.h>    // Core arduino library
#include <Wire.h>
//#include "avr8-stub.h"
#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789
#include <SPI.h>
#include <SD.h>
//#include <FreeSans12pt7b.h>
//#include <FreeSans18pt7b.h>
//#include <FreeSans24pt7b.h>
//#include <FreeSans9pt7b.h>
//#include <FreeSansBold12pt7b.h>
//#include <FreeSansBold18pt7b.h>
//#include <FreeSansBold24pt7b.h>
//#include <FreeSansBold9pt7b.h>
#include <Adafruit_MAX31855.h>
#include <Rotary.h>
#include <Debounce.h>

/*********************************************

TODO:
- Fix the bug where tens place and degrees save on next/back for temp page but hundreds place doesnt
- Create a RAMP screen with time editing UI
- Create SOAK screen derived from RAMP screen
- Get logic working to move forward and back through TEMP/RAMP/SOAK
- Consider getting logic working to go oback through CYCLES too
- Get NAME page UI built
- Get saving full Preset to SD card working
- Get SUMMARY page UI built (optional)

**********************************************/
// For the breakout board, you can use any 2 or 3 pins.
// These pins will also work for the 1.8" TFT shield.
#define TFT_CS        10
#define TFT_RST       -1 //set to -1 as reset is built in for this TFT screen
#define TFT_DC         9

#define SCREEN_HEIGHT 240
#define SCREEN_WIDTH 320

//Define the SD Card chip select pin (SDCS)
#define CS_PIN 4

#define RightButtonPin         A4 //What pin is the rotary encoder button connected to
#define LeftButtonPin          A5 //What pin is the push button connected to
#define CenterButtonPin        A3 //What pin is the push button connected to

Debounce RightButton(RightButtonPin, 60);
Debounce LeftButton(LeftButtonPin, 60);
Debounce CenterButton(CenterButtonPin, 60);

#define ROTCOUNT              24 // Number of detents in a single rotation of the knob

// Rotary encoder is wired with the common to ground and the two
// outputs to pins 2 and 3. 
// NOTE: Interrupt conflicts with I2C and only works on pin 2 & 3.
Rotary rotary = Rotary(3, 2);

// For 1.14", 1.3", 1.54", and 2.0" TFT with ST7789:
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// Example creating a thermocouple instance with software SPI on any three
// digital IO pins.
#define MAXDO   8
#define MAXCS   7
#define MAXCLK  6

// initialize the Thermocouple
Adafruit_MAX31855 thermocouple(MAXCLK, MAXCS, MAXDO);

#define REFRESH_INTERVAL 1000 // ms
static unsigned long lastRefreshTime = 0;

byte rightButtonState = HIGH;         // variable for reading the pushbutton status
byte leftButtonState = HIGH;          // variable for reading the pushbutton status
byte centerButtonState = HIGH;        // variable for reading the pushbutton status

byte rightButtonStatePrevious = LOW;         // variable for storing the previous pushbutton status (important for debouncing as the button records tons of HIGH or LOW states per press and we just want the one where it first changes)
byte leftButtonStatePrevious = LOW;          // variable for storing the previous pushbutton status (important for debouncing as the button records tons of HIGH or LOW states per press and we just want the one where it first changes)
byte centerButtonStatePrevious = LOW;        // variable for storing the previous pushbutton status (important for debouncing as the button records tons of HIGH or LOW states per press and we just want the one where it first changes)

#define SCREEN_LINES 5 //Number of lines of text that will be displayed on the screen when listing items

// Define the limits of the presets
#define NUM_PRESETS 10
#define NUM_CYCLES 8
#define VALUES_PER_CYCLE 3

#define NAME_SIZE 13

File presetsFile;

// Array for data.
char presetsNames[NUM_PRESETS][NAME_SIZE];
unsigned int presetValues[NUM_CYCLES*VALUES_PER_CYCLE] = {};  //unsigned int in arduino is 2 bytes so it can go from 0 to 65,535 (which is about 18 hours in seconods, or 1,92 hours in minutes)

//PRESETS VALUES
#define HOME 0
#define NEW_MANUAL 1

//PAGES VALUES
#define CHOOSE 0
#define MANUAL 1
#define CYCLES 2
#define TEMP 3
#define RAMP 4
#define SOAK 5
#define SUMMARY 6
#define NAME 7

byte presetsNamesCount = 1;       //Number of presets found in the SD card plus 1 for new/manual
byte presetValuesCount = 0;       //Number of values found in SD card for a given preset
byte selectedPreset = HOME;       //Which preset is selected, none = HOME
byte page = 0;                    //Which page are we on, only used for new/manual and editing
byte element = 1;                 //Which page element are we editing, only used for new/manual and editing
byte elementLimit = 0;            //How many elements are there on the page (helps us rotate through them circularly)
byte totalCycles = 0;             //How many cycles are we creating/editing
byte currentCycle = 0;            //What cycle are we currently creating/editing
byte rotValue = 1;                //The value the rotary encoder is currently on
byte rotLimit = 0;                //The number of items the rotary encoder is choosing between (helps us rotate through them circularly)

#define errorHalt(msg) {Serial.println(F(msg)); while(1); }
//#define errorHalt(msg) {while(1); }

void refreshScreen();
void changeScreen();
void saveElement();
void drawTemp();
void rotate();
void rotate();
void readPresetsIntoArrays();
size_t readField(File* file, char presetsNames[][NAME_SIZE], char* name, int values[], char* delim);
void drawButtons(char* leftButton, char* rightButton);
word ConvertRGB( byte R, byte G, byte B);

void setup(void) {
  // initialize GDB stub
  //debug_init(); //this doesnt work, I can't figure out why yet
  
  Serial.begin(9600);
  Serial.println(F("Hello! Kiln Test"));

  // Initialize the SD.
  if (!SD.begin(CS_PIN)) {
    errorHalt("begin failed");
  }
  strcpy(presetsNames[0],"New/Manual");

  readPresetsIntoArrays();

  attachInterrupt(0, rotate, CHANGE);
  attachInterrupt(1, rotate, CHANGE);

  pinMode(RightButtonPin, INPUT);
  analogWrite(RightButtonPin, 255);

  pinMode(LeftButtonPin, INPUT);
  analogWrite(LeftButtonPin, 255);

  pinMode(CenterButtonPin, INPUT);
  analogWrite(CenterButtonPin, 255);

  tft.init(SCREEN_HEIGHT, SCREEN_WIDTH);           // Init ST7789 320x240
  tft.setRotation(3);

  //Serial.println(F("Initialized"));

  tft.setTextWrap(false);
  tft.fillScreen(ST77XX_WHITE);

  
  // tft print function!
  changeScreen();
  //delay(1);

  // wait for MAX chip to stabilize
  delay(500);
  //Serial.print("Initializing sensor...");
  if (!thermocouple.begin()) {
    errorHalt("Thermocouple error");
  }
  //Serial.println("done");
  //delay(3000);
}

void loop() {
  // read the state of the pushbutton value:
  //  buttonState = digitalRead(buttonPin);

  rightButtonState = RightButton.read();
  //Serial.print("rightButtonState: ");
  //Serial.println(rightButtonState);

  if (rightButtonState == LOW && rightButtonStatePrevious == HIGH) {
    // Serial.println("Right button pressed!");
    // Serial.print("selectedPreset: ");
    // Serial.println(selectedPreset);
    // Serial.print("Page: ");
    // Serial.println(page);
    saveElement();
    switch (selectedPreset) {
      case HOME:
        selectedPreset = rotValue;
        break;
      case NEW_MANUAL:
        switch (page) {
          case CHOOSE:
            if(rotValue == 1){
              page = CYCLES;
            } else {
              page = MANUAL;
            }
            break;
          case CYCLES:
            totalCycles = rotValue;
            currentCycle = 1;
            // Serial.print("Saving totalCycles: ");
            // Serial.println(totalCycles);
            page = TEMP;
            break;
          case SOAK:
            if(totalCycles > 1 && totalCycles > currentCycle){
              page = TEMP;
              currentCycle++;
            } else {
              page = SUMMARY;
            }
            break;
          case MANUAL:
            //run kiln
            break;
          default:
            page++;
        }
        //need some logic here to run through cycles
        break;
    }
    //Serial.println("Right Button Pressed!");
    changeScreen();
  }
  rightButtonStatePrevious = rightButtonState;

  leftButtonState = LeftButton.read();
  //Serial.print("leftButtonState: ");
  //Serial.println(leftButtonState);
  
  if (leftButtonState == LOW && leftButtonStatePrevious == HIGH) {
    saveElement();
    if(page == 0){
      selectedPreset = HOME;
    } else {
      switch (selectedPreset) {
        case NEW_MANUAL:
          switch (page) {
            case CYCLES:
              page = CHOOSE;
              break;
            case TEMP:
              if(currentCycle > 1){
                page = SOAK;
                currentCycle--;
              } else {
                page = CYCLES;
              }
              break;
            default:
              page--;
          }
          //need some logic here to run through cycles
          break;
      }
    }
    //Serial.println("Left Button Pressed!");
    changeScreen();
  }
  leftButtonStatePrevious = leftButtonState;

  centerButtonState = CenterButton.read();
  //Serial.print("centerButtonState: ");
  //Serial.println(centerButtonState);

  if (centerButtonState == LOW && centerButtonStatePrevious == HIGH) {
    // turn LED on:
    //tft.fillScreen(ST77XX_GREEN);
    if(page > 0 && selectedPreset > 0 && elementLimit > 0){
          
      //TODO: save value before refreshing screen
      saveElement();
      element++;
      if(element > 0) {
        element = element%elementLimit;        //if element is positive, change it to modulous of elementLimit so it doesnt go higher than elementLimit
      }
      if(element == 0){                        //If current element drops to zero (which is considered undefined for us)
        element = elementLimit+element;        //add the elementLimit to allow element to loop back to the end
      }
    }
    //Serial.println("Center Button Pressed!");
    //TODO: save value before refreshing screen (probably belongs above)
    refreshScreen();
  }
  centerButtonStatePrevious = centerButtonState;

  if(millis() - lastRefreshTime >= REFRESH_INTERVAL) {
    lastRefreshTime += REFRESH_INTERVAL;
    drawTemp();
  }
}

void saveElement(){
  //[0,1,2,3,4,5,6,7,8]
  //(currentCycle-1)*3 is starting point of this cycle
  //
  int i = (currentCycle-1)*VALUES_PER_CYCLE;
  unsigned int tmp = 0;
  switch(page){
    case TEMP:
      switch(element){
        case 1:
          // Serial.print("Hunds Rot value:");
          // Serial.println(rotValue-1);
          // Serial.print("Previous array value:");
          // Serial.println(presetValues[i]);
          // Serial.print("Previous element value:");
          // Serial.println(presetValues[i]/100);
          tmp = ((rotValue-1)*100)+((presetValues[i]>>1)%100);  //Sub 1 from rotVal to get the actual number, then multiply by 100 because this element is the hundreds place, then shift values ribght to push off the degree flag and modulous by 100 to get the tens place digits.
          tmp = tmp << 1;                                       //shift tmp left by one to leave room for the degree flag (C=1 or F=0)
          presetValues[i] = presetValues[i] & 1;                //clear all the bits in this value except the first (degree flag) as we dont want to lose it
          presetValues[i] = presetValues[i] | tmp;              //save the bit shifted tmp into this value (it will not replace the degree flag)
          rotValue = ((presetValues[i]>>1)%100)+1;              //set the rotVal to the tens place value since the user might have hit the center button to move to the next element (tens)
          // Serial.print("Hunds Saved value:");
          // Serial.println(presetValues[i]>>1);
          // Serial.print("New value:");
          // Serial.println(rotValue);
          break;
        case 2:
          // Serial.print("Rot:");
          // Serial.println(rotValue-1);
          // Serial.print("Previous array value:");
          // Serial.println(presetValues[i]);
          // Serial.print("Old:");
          // Serial.println(presetValues[i]>>1);
          // Serial.print("Previous element value:");
          // Serial.println((presetValues[i]>>1)%100);
          tmp = ((int)((presetValues[i]>>1)/100)*100)+rotValue-1;
          // Serial.print("New:");
          // Serial.println(tmp);
          // Serial.print("New value in binary:");
          // Serial.println(tmp, BIN);
          tmp = tmp << 1;
          // Serial.print("Shifted new value in binary:");
          // Serial.println(tmp, BIN);
          // Serial.print("OldB:");
          // Serial.println(presetValues[i], BIN);
          presetValues[i] = presetValues[i] & 1;
          // Serial.print("T:");
          // Serial.println(presetValues[i], BIN);
          presetValues[i] = presetValues[i] | tmp;
          // Serial.print("NewB:");
          // Serial.println(presetValues[i], BIN);
          // Serial.print("NewA:");
          // Serial.println(presetValues[i]>>1);
          rotValue = (presetValues[i] & 1)+1;
          // Serial.print("Saved value:");
          // Serial.println(presetValues[i]);
          // Serial.print("Next rot value:");
          // Serial.println(rotValue);
          break;
        case 3:
          // Serial.print("Temp Rot value:");
          // Serial.println(rotValue-1);
          // Serial.print("Previous array value:");
          // Serial.println(presetValues[i+1]);
          //Serial.println(~1, BIN);
          //Serial.println(presetValues[i], BIN);
          presetValues[i] = presetValues[i] & ~1;           //Clear only the degree flag by and'ing with the binary invert (~) of 1
          presetValues[i] = presetValues[i] | (rotValue-1); //Save the degree flag to the first bit of the current value
          //Serial.println(presetValues[i], BIN);
          rotValue = ((presetValues[i]>>1)/100)+1;
          // Serial.print("Saved value:");
          // Serial.println(presetValues[i+1]);
          // Serial.print("New value:");
          // Serial.println(rotValue);
          break;
      }
    case RAMP:
      i = i+2;
      switch(element){
        case 1:
          presetValues[i] = (rotValue*60)+(presetValues[i]%60);
          break;
        case 2:
          presetValues[i] = (presetValues[i]/60)+rotValue;
          break;
      }
      break;
    case SOAK:
      i = i+3;
      switch(element){
        case 1:
          presetValues[i] = (rotValue*60)+(presetValues[i]%60);
          break;
        case 2:
          presetValues[i] = (presetValues[i]/60)+rotValue;
          break;
      }
      break;
  }
}

void readPresetsIntoArrays() {

  // Create or open the file.
  presetsFile = SD.open("PRESETS.TXT",  O_READ | O_WRITE | O_CREAT);
  if (!presetsFile) {
    errorHalt("open failed");
  }

  presetsNamesCount = readField(&presetsFile, presetsNames, NULL, NULL, (char*)",\n");
  // Serial.print("Name Size:");
  // Serial.println(presetsNamesCount);

  // for (int i = 0; i < presetsNamesCount; i++) {
  //   Serial.print("Name: ");
  //   Serial.println(presetsNames[i]);
  // }
  
  // presetValuesCount = readField(&presetsFile, NULL, "Melt Aluminium", presetValues, ",\n");
  // Serial.print("Values Size:");
  // Serial.println(presetValuesCount);
  // for (int i = 0; i < presetValuesCount; i++) {
  //   Serial.print("Values[");
  //   Serial.print(i);
  //   Serial.print("]: ");
  //   Serial.println(presetValues[i]);
  // }
  // Print the array.

  // Serial.println("Done");
  presetsFile.close();
}

//This function fills either the presetsNames with all the names it finds or vthe alues arrays with the values for the line matching the passed in "name" string
//This assumes a CSV file with the name as the first value in the line, then up to NUM_CYCLES*VALUES_PER_CYCLE ints following.
size_t readField(File* file, char presetsNames[][NAME_SIZE], char* name, int values[], char* delim) {
  char ch;                                            //init temp character storage
  char str[NAME_SIZE];                                //init temp string storage
  int line = 1;                                       //init line counter for presetsNames array starts at 1 to leave room for the default preset
  int j = 0;                                          //init the values counter
  size_t n = 0;                                       //init size of string read to 0
  bool gotoNextLine = 0;
  bool thisLine = 0;
  char *ptr;                                          // Pointer for strtol used to test for valid number.
  //Serial.println("In read file.");
  presetsFile.seek(0);

  while(file->read(&ch, 1) == 1){
    //Serial.println(ch);
    if (ch == '\r') { continue; }                     //skip over carrage returns, they are not reliable newlines
    if(!gotoNextLine){
      if((n + 2) > NAME_SIZE || strchr(delim, ch)) {  //if string size is full or we have reached a string deliminator then save the value
        str[n] = '\0';                                //add string terminator
        n = 0;                                        //reset string size counter
        //Serial.println(str);
        if(name == NULL) {                            //if name is not defined then we know we are looking for names not values
          //fill up presetsNames array
          strcpy(presetsNames[line], str);            //save string to presetsNames array
          //Serial.println(presetsNames[line]);
          gotoNextLine = 1;                           //set flag to skip to end of line so we are ready to capture the next name
          line++;                                     //increment line now in case the last line doesnt end in a newline
          if(line > NUM_PRESETS){                     //if we have reached the max number of presets (including the first default, hense > and not >=)
            break;
          }
          continue;
        } else {
          if(thisLine){                               //if this is the line we want the values for (determined below)
            //fill up values array
            values[j] = strtol(str, &ptr, 10);        //convert string to long and store in int array
            j++;                                      //increment value counter
            if(j >= (NUM_CYCLES*VALUES_PER_CYCLE) || ch == '\n'){    //exit if we have rerached the max number of values or the end of the values on this line
              break;
            }
          } else {
            if(strcmp(name, str) == 0){               //if name is set and name = str then this is the line we want the values from
              //Serial.println("Name match!");
              thisLine = 1;                           //set flag to capture values from now on (we don't need the name as it was passed in)
              continue;
            } else {
              // Serial.print("'");
              // Serial.print(name);
              // Serial.print("'");
              // Serial.print(" doesnt match ");
              // Serial.print("'");
              // Serial.print(str);
              // Serial.print("'\n");
              gotoNextLine = 1;
            }
          }
        }
      } else {                                        //if not gotonextline and string isnt too long and char isnt a delim, then save char to string
        str[n++] = ch;                                //append the new char to the end of the string then increment the value of n 
      }
    } else {                                          //if gotonextline is true, dont do anything with ch except check if it is '/n'
      if(ch == '\n'){                                 //if we have reached the end of the line
        gotoNextLine = 0;                             //set gotonextline as false so we will save the next string
      }
    }
  }
  return max(line, j);                                //return size of the built array
}


void changeScreen(){
  rotValue = 1;
  element = 1;
  elementLimit = 0;
  int static_digits = 0;
  tft.fillScreen(ST77XX_WHITE);
  tft.setTextColor(ST77XX_BLACK);
  switch (selectedPreset) {
    case HOME:
      rotLimit = presetsNamesCount;
      tft.fillTriangle(5, 85, 5, 105, 17, 95, ST77XX_BLACK);
      //draw buttons
      //drawButtons((char*)"Edit", (char*)"Run");
      break;
    case NEW_MANUAL:
      switch (page) {
        case CHOOSE:
          rotLimit = 2;
          tft.setTextSize(4);
          tft.setCursor(25, 47);
          tft.println("New Routine");
          tft.setCursor(25, tft.getCursorY()+25);
          tft.println("Manual Temp");
          drawButtons((char*)"Back", (char*)"Next");
          break;
        case TEMP:
          elementLimit = 3;
          // Serial.print("On temp page:");
          // Serial.println(page);
          // Serial.print("element:");
          // Serial.println(element);
          tft.setTextSize(4);
          tft.setCursor(25, 27);
          tft.println("What temp?");
          tft.setTextSize(5);
          tft.setCursor(65, 120);
          tft.print("  ");
          static_digits = (presetValues[(currentCycle-1)*VALUES_PER_CYCLE]>>1)%100;
          if(static_digits < 10){
            tft.print("0");
          }
          tft.print(static_digits);
          if((presetValues[((currentCycle-1)*VALUES_PER_CYCLE)] & 1)+1 == 2){
            tft.println(" C");
          } else {
            tft.println(" F");
          }
          drawButtons((char*)"Back", (char*)"Next");
          break;
        case MANUAL:
          elementLimit = 3;
          switch (element){
            case 3:
              rotLimit = 2;
              break;
            default:
              rotLimit = 100;
              break;
          }
          tft.setTextSize(4);
          tft.setCursor(25, 27);
          tft.println("What temp?");
          tft.setTextSize(5);
          tft.setCursor(65, 120);
          tft.print("  ");
          tft.print("00");
          tft.println(" F");
          drawButtons((char*)"Back", (char*)"Run");
          break;
        case CYCLES:
          rotLimit = NUM_CYCLES;
          tft.fillTriangle(255, 85, 255, 105, 267, 95, ST77XX_BLACK);
          tft.setTextSize(4);
          tft.setCursor(25, 27);
          tft.println("How many\n ramp/soak\n cycles?");
          drawButtons((char*)"Back", (char*)"Next");
          break;
        case RAMP:
        case SOAK:
          rotLimit = 60;
          break;
        case SUMMARY:
          rotLimit = totalCycles;
          break;
        case NAME:
          rotLimit = 26;
          break;
      }
      break;
    default:
      break;
  }
  refreshScreen();
}

#define BUTTON_HEIGHT 50
#define BUTTON_MARGIN 12
#define CHAR_WIDTH 24
#define LETTER_SPACING 4

void drawButtons(char* leftButton, char* rightButton){
  tft.setTextSize(4);
  tft.setTextColor(ST77XX_WHITE);
  if(leftButton != NULL){
    tft.fillRect(0, SCREEN_HEIGHT-BUTTON_HEIGHT, SCREEN_WIDTH/2, BUTTON_HEIGHT, ConvertRGB( 255, 92, 0));
    tft.setCursor(BUTTON_MARGIN, SCREEN_HEIGHT-BUTTON_HEIGHT+BUTTON_MARGIN);
    tft.println(leftButton);
  } else {
    tft.fillRect(0, SCREEN_HEIGHT-BUTTON_HEIGHT, SCREEN_WIDTH/2, BUTTON_HEIGHT, ST77XX_WHITE);
  }
  //button width is the lenth of string times char width plus left and right margins and minus the letter spacing on the last letter
  int buttonWidth = (CHAR_WIDTH*strlen(rightButton))+(2*BUTTON_MARGIN)-LETTER_SPACING;
  //Serial.println(buttonWidth);
  if(buttonWidth > (SCREEN_WIDTH/2)){
    tft.fillRect(SCREEN_WIDTH-buttonWidth, SCREEN_HEIGHT-BUTTON_HEIGHT, buttonWidth, BUTTON_HEIGHT, ConvertRGB( 43, 160, 13));
  } else {
    tft.fillRect(SCREEN_WIDTH/2, SCREEN_HEIGHT-BUTTON_HEIGHT, SCREEN_WIDTH/2, BUTTON_HEIGHT, ConvertRGB( 43, 160, 13));
  }
  //Serial.println(strlen(rightButton));
  tft.setCursor(SCREEN_WIDTH-BUTTON_MARGIN+LETTER_SPACING-(CHAR_WIDTH*strlen(rightButton)), SCREEN_HEIGHT-BUTTON_HEIGHT+BUTTON_MARGIN);
  tft.println(rightButton);
  tft.setTextColor(ST77XX_BLACK);
}

void drawTemp() {
  //tft.setTextColor(ST77XX_RED);
  //tft.setFont(&FreeSansBold9pt7b);
  tft.setTextSize(2);

//void fillRect(x0, y0, w, h, color);
  tft.fillRect(260, 0 , 60, 25, ST77XX_WHITE);

  tft.setCursor(275, 5);
  double f = thermocouple.readFahrenheit();
  if (!isnan(f)) {
    tft.print(f, 0);
    tft.println("F");
  }
}

void printCenteredDialText(char strings[][NAME_SIZE], byte length, int padding) {
  int j = 0;
  for(int i = SCREEN_LINES; i > 0; i--){
    j = i-3+(rotValue-1);
    if(j < 0){
      j = length+j;
    } else {
      j = j%length;
    }
    if(strings != NULL){
      //tft.println(i);
      tft.println(strings[j]);
    } else {
      tft.println(j+1);
    }
    tft.setCursor(padding, tft.getCursorY());
  }
}

void refreshScreen() {
  //Serial.println("drawScreen!");
  //tft.setFont(&FreeSansBold18pt7b);
  switch(selectedPreset){
    case HOME:
      //void fillRect(x0, y0, w, h, color);
      tft.fillRect(25, 17, SCREEN_WIDTH-25, SCREEN_HEIGHT-17-50, ST77XX_WHITE);
      tft.setTextSize(4);
      tft.setCursor(25, 17);
      printCenteredDialText(presetsNames, presetsNamesCount, 25);
      if(rotValue == 1){
        drawButtons(NULL, (char*)"New/Manual");
      } else {
        drawButtons((char*)"Edit", (char*)"Run");
      }
      //delay(1);
      break;
    case NEW_MANUAL:
      switch (page) {
        case CHOOSE:
          if(rotValue == 2){
            //void fillRect(x0, y0, w, h, color);
            tft.fillRect(5, 52, 12, 20, ST77XX_WHITE);
            tft.fillTriangle(5, 108, 5, 128, 17, 118, ST77XX_BLACK);
          } else {
            tft.fillRect(5, 108, 12, 20, ST77XX_WHITE);
            tft.fillTriangle(5, 52, 5, 72, 17, 62, ST77XX_BLACK);
          }
          break;
        case TEMP:
          tft.setTextSize(5);
          tft.setCursor(65, 120);
          switch (element){
            case 1:
              rotLimit = 100;
              tft.fillRect(65, 120, 55, 35, ST77XX_WHITE);
              if(rotValue-1 < 10){
                tft.print(" ");
              }
              tft.print(rotValue-1);
              //void fillRect(x0, y0, w, h, color);
              tft.fillRect(65, 170, 55, 4, ST77XX_BLACK);
              tft.fillRect(125, 170, 55, 4, ConvertRGB( 197, 197, 197));
              tft.fillRect(185, 170, 55, 4, ConvertRGB( 197, 197, 197));
              break;
            case 2:
              rotLimit = 100;
              tft.fillRect(125, 120, 55, 35, ST77XX_WHITE);
              tft.print("  ");
              if(rotValue-1 < 10){
                tft.print("0");
              }
              tft.print(rotValue-1);
              tft.fillRect(65, 170, 55, 4, ConvertRGB( 197, 197, 197));
              tft.fillRect(125, 170, 55, 4, ST77XX_BLACK);
              tft.fillRect(185, 170, 55, 4, ConvertRGB( 197, 197, 197));
              break;
            case 3:
              rotLimit = 2;
              tft.fillRect(185, 120, 55, 35, ST77XX_WHITE);
              tft.print("    ");  //TODO: consider using setCursor instead of space strings to save memory
              // Serial.print("rotValue:");
              // Serial.println(rotValue);
              // Serial.print("rotLimit:");
              // Serial.println(rotLimit);
              if(rotValue == 2){
                tft.println(" C");
              } else {
                tft.println(" F");
              }
              tft.fillRect(65, 170, 55, 4, ConvertRGB( 197, 197, 197));
              tft.fillRect(125, 170, 55, 4, ConvertRGB( 197, 197, 197));
              tft.fillRect(185, 170, 55, 4, ST77XX_BLACK);
              break;
          }
          break;
        case CYCLES:
          //void fillRect(x0, y0, w, h, color);
          tft.fillRect(275, 17, SCREEN_WIDTH-185, SCREEN_HEIGHT-17-50, ST77XX_WHITE);
          tft.setTextSize(4);
          tft.setCursor(275, 17);
          printCenteredDialText(NULL, NUM_CYCLES, 275);
          //delay(1);
          break;
        default:
          break;
      }
      //delay(1);
      break;
    default:
      tft.fillScreen(ST77XX_WHITE);
      tft.println(presetsNames[selectedPreset-1]);
      break;
  }
  //void fillTriangle(x0, y0, x1, y1, x2, y2, color);
}

// rotate is called anytime the rotary inputs change state.
void rotate() {
  unsigned char result = rotary.process();

  if (result == DIR_CCW) {
    //Serial.println("Counter-clockwise!");
    rotValue++;

    if(rotValue > 0) {
      rotValue = rotValue%rotLimit;        //if rotValue is positive, change it to modulous of rotLimit so it doesnt go higher than rotLimit
    }
    if(rotValue == 0){                      //If current rotValue goes below zero
      rotValue = rotLimit+rotValue;        //add the rotLimit to allow rotValue to loop back to the end
    }
    // Serial.print("rotLimit:");
    // Serial.println(rotLimit);
    // Serial.print("rotValue:");
    // Serial.println(rotValue);
    //Serial.print(rotValue);
    refreshScreen();

  } else if (result == DIR_CW) {
    //Serial.println("Clockwise!");
    rotValue--;

    if(rotValue > 0) {
      rotValue = rotValue%rotLimit;        //if rotValue is positive, change it to modulous of rotLimit so it doesnt go higher than rotLimit
    }
    if(rotValue == 0){                      //If current rotValue goes below zero
      rotValue = rotLimit+rotValue;        //add the rotLimit to allow rotValue to loop back to the end
    }
    // Serial.print("rotLimit:");
    // Serial.println(rotLimit);
    // Serial.print("rotValue:");
    // Serial.println(rotValue);
    //Serial.print(rotValue);
    refreshScreen();
  }
  //CODE CANT GO HERE BECAUSE THIS FUNCTION IS CALLED EVEN WHEN THERE IS NO ROTATION
  //COMMON CODE WOULD NEED TO BE NESTED IN AN IF STATEMENT THAT CHECKS FOR EITHER ROTATION
  //Serial.print(rotValue);
  //refreshScreen();
}

word ConvertRGB( byte R, byte G, byte B)
{
  return ( ((R & 0xF8) << 8) | ((G & 0xFC) << 3) | (B >> 3) );
}