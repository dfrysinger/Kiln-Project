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
- DONE Fix the bug where tens place and degrees save on next/back for temp page but hundreds place doesnt
- DONE Create a RAMP screen with time editing UI
- DONE Create SOAK screen derived from RAMP screen
- DONE Get logic working to move forward and back through TEMP/RAMP/SOAK
- Add a "clear unused cycles in values array" function after the user hits next on the summary screen
- DONE Get NAME page UI built
- DONE Save name into temp variiable
- Get saving full Preset to SD card working
- Optimize draw space for home screen scrolling by making buttons mostly static
- DONE Get SUMMARY page UI built (optional)
- Allow user to scroll through summary page (perhaps using different text overlapping on the default text, might work without a white overlay)

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
Rotary rotary = Rotary(2, 3);

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

#define SCREEN_LINES_4 5 //Number of lines of text that will be displayed on the screen when listing items
#define SCREEN_LINES_3 6 //Number of lines of text that will be displayed on the screen when listing items

// Define the limits of the presets
#define NUM_PRESETS 10
#define NUM_CYCLES 8
#define NAME_SIZE 12

#define VALUES_PER_CYCLE 3
#define NAME_STRING_SIZE NAME_SIZE+1  //adding one to the actual character count to account for the string terminator character

File presetsFile;

// Array for data.
char presetsNames[NUM_PRESETS][NAME_STRING_SIZE];
unsigned int presetValues[NUM_CYCLES*VALUES_PER_CYCLE] = {};  //unsigned int in arduino is 2 bytes so it can go from 0 to 65,535 (which is about 18 hours in seconods, or 1,92 hours in minutes)

//PRESETS VALUES
#define HOME 0
#define NEW_MANUAL 1

//PAGES VALUES FOR NEW/MANUAL
#define CHOOSE 0
#define MANUAL 1
#define CYCLES 2
#define TEMP 3
#define RAMP 4
#define SOAK 5
#define SUMMARY 6
#define NAME 7
#define CONFIRM 8

//PAGES VALUES FOR PRESETS
#define RUN_SUMMARY 0
#define RUN 1
#define EDIT 2

#define DEFAULT_CHAR 1   //This is the char to start the user on when enttering a name. 1 is ' '  and 34 is 'A'
#define ASCII_OFFSET 32  //This is the ascii value of the first character we want to allow 32 is ' ' and the first visible char in the chart. This means a user has to scroll through a lot of synbols before arriving at an alpha character. Might be good to eventually change this to something like 65 ('A'). This will remove nearly all special chars but make it easier to enter alpha chars. 
#define ASCII_LIMIT 94   //This is the max value of rotValue for chars, and also the total characters after offset that we want to allow. There are 94 chars after 32 in the ascii chart so it is set to 94, but if the offset is changed this would need to be changed to something smaller so we dont overflow the ascii values.

char newName[NAME_STRING_SIZE] = "";   //this is a temp char array used to store a new preset name
byte presetsNamesCount = 1;       //Number of presets found in the SD card plus 1 for new/manual
byte presetValuesCount = 0;       //Number of values found in SD card for a given preset
byte selectedPreset = HOME;       //Which preset is selected, none = HOME
byte page = 0;                    //Which page are we on, only used for new/manual and editing
byte element = 1;                 //Which page element are we editing, only used for new/manual and editing
byte elementLimit = 1;            //How many elements are there on the page (helps us rotate through them circularly)
byte totalCycles = 0;             //How many cycles are we creating/editing
byte currentCycle = 1;            //What cycle are we currently creating/editing
byte rotValue = 1;                //The value the rotary encoder is currently on
byte rotLimit = 1;                //The number of items the rotary encoder is choosing between (helps us rotate through them circularly)
char* back = (char*)"Back";
char* next = (char*)"Next";
char* edit = (char*)"Edit";
char* run = (char*)"Run";

#define errorHalt(msg) {Serial.println(F(msg)); while(1); }
//#define errorHalt(msg) {while(1); }

void refreshScreen();

void clearPresetValues();
void saveElement();
void drawTemp();
void rotate();
void rotate();
void writePresetToFile();
void readPresetsIntoArrays();
size_t readField(File* file, char presetsNames[][NAME_STRING_SIZE], char* name, unsigned int values[], char* delim);
void changeScreen();
void drawSummary(int start = 0);
void drawTimeScreen(char* title, int currentValue, char* leftButton, char* rightButton);
void drawTempScreen(char* leftButton, char* rightButton);
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
    Serial.println("Right button pressed!");
    Serial.print("selectedPreset: ");
    Serial.println(selectedPreset);
    Serial.print("Page: ");
    Serial.println(page);
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
          case NAME:
            writePresetToFile();
            clearPresetValues();
            readPresetsIntoArrays();
            page = CHOOSE;
            selectedPreset = HOME;
            break;
          default:
            page++;
        }
        //need some logic here to run through cycles
        break;
      default:
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
      rotValue = selectedPreset;
      selectedPreset = HOME;
    } else {
      switch (selectedPreset) {
        case NEW_MANUAL:
          switch (page) {
            case CYCLES:
              clearPresetValues();
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
          break;
        default:
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
      // Serial.print("Previous Rot value: ");
      // Serial.println(rotValue);
      saveElement();
      // Serial.print("After saveElement Rot value: ");
      // Serial.println(rotValue);
      element++;
      if(element > 0) {
        element = element%elementLimit;        //if element is positive, change it to modulous of elementLimit so it doesnt go higher than elementLimit
      }
      if(element == 0){                        //If current element drops to zero (which is considered undefined for us)
        element = elementLimit+element;        //add the elementLimit to allow element to loop back to the end
      }
    }
    //Serial.println("Center Button Pressed!");
    // Serial.print("Before refreshScreen Rot value: ");
    // Serial.println(rotValue);
    refreshScreen();
    // Serial.print("After refreshScreen Rot value: ");
    // Serial.println(rotValue);
  }
  centerButtonStatePrevious = centerButtonState;

  // if(millis() - lastRefreshTime >= REFRESH_INTERVAL) {
  //   lastRefreshTime += REFRESH_INTERVAL;
  //   drawTemp();
  // }
}

//this is called after the NAME page is saved to clear the array for the next preset
void clearPresetValues(){
  totalCycles = 0;
  for (int i = 0; i < NAME_SIZE; i++){
    newName[i] = 0;
  }
  for(int i = 0; i < NUM_CYCLES; i++){  //starting after the value of the cycle total the user selected, count up to the max number of cycles as this is how much memory was allocated
    for(int j = 0; j < VALUES_PER_CYCLE; j++){    //loop through the number of values per cycle 
      presetValues[i*VALUES_PER_CYCLE+j] = 0;     //clear each value for each cycle that isnt being used
    }
  }

}

void saveElement(){
  //[0,1,2,3,4,5,6,7,8]
  //(currentCycle-1)*3 is starting point of this cycle
  //
  int i = (currentCycle-1)*VALUES_PER_CYCLE;
  unsigned int tmp = 0;
  switch(page){
    case CYCLES:
      totalCycles = rotValue;
      currentCycle = 1;
      break;
    case TEMP:
      switch(element){
        case 1:
          // Serial.print("Hunds Rot value:");
          // Serial.println(rotValue-1);
          // Serial.print("Previous array value:");
          // Serial.println(presetValues[i]>>1);
          // Serial.print("Previous element value:");
          // Serial.println((presetValues[i]>>1)/100);
          tmp = ((rotValue-1)*100)+((presetValues[i]>>1)%100);  //Sub 1 from rotVal to get the actual number, then multiply by 100 because this element is the hundreds place, then shift values ribght to push off the degree flag and modulous by 100 to get the tens place digits.
          tmp = tmp << 1;                                       //shift tmp left by one to leave room for the degree flag (C=1 or F=0)
          presetValues[i] = presetValues[i] & 1;                //clear all the bits in this value except the first (degree flag) as we dont want to lose it
          presetValues[i] = presetValues[i] | tmp;              //save the bit shifted tmp into this value (it will not replace the degree flag)
          rotValue = ((presetValues[i]>>1)%100)+1;              //set the rotVal to the tens place value since the user might have hit the center button to move to the next element (tens)
          // Serial.print("Hunds Saved value:");
          // Serial.println(presetValues[i]>>1);
          // Serial.print("New rot value:");
          // Serial.println(rotValue);
          break;
        case 2:
          // Serial.print("Rot:");
          // Serial.println(rotValue-1);
          // Serial.print("Previous array value:");
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
          // Serial.print("Next rot value:");
          // Serial.println(rotValue);
          break;
        case 3:
          // Serial.print("Temp Rot value:");
          // Serial.println(rotValue-1);
          // Serial.print("Previous array value:");
          // Serial.println(presetValues[i]);
          //Serial.println(~1, BIN);
          //Serial.println(presetValues[i], BIN);
          presetValues[i] = presetValues[i] & ~1;           //Clear only the degree flag by and'ing with the binary invert (~) of 1 (1111~1110)
          presetValues[i] = presetValues[i] | (rotValue-1); //Save the degree flag to the first bit of the current value
          //Serial.println(presetValues[i], BIN);
          rotValue = ((presetValues[i]>>1)/100)+1;
          // Serial.print("Saved value:");
          // Serial.println(presetValues[i]);
          // Serial.print("New value:");
          // Serial.println(rotValue);
          break;
      }
      break;
    case SOAK:
      i = i+1;
      //there is no break here so that RAMP increments i again and ends up saving the correct values to i+2 for SOAK
    case RAMP:
      i = i+1;
      switch(element){
        case 1:
          // Serial.print("Hours Rot value:");
          // Serial.println(rotValue-1);
          // Serial.print("Previous array value:");
          // Serial.println(presetValues[i]);
          // Serial.print("Previous element value:");
          // Serial.println(presetValues[i]/60);
          presetValues[i] = ((rotValue-1)*60)+(presetValues[i]%60);
          rotValue = ((presetValues[i])%60)+1;
          // Serial.print("Hours Saved value:");
          // Serial.println(presetValues[i]);
          // Serial.print("New value:");
          // Serial.println(rotValue);
          break;
        case 2:
          // Serial.print("Minutes Rot value:");
          // Serial.println(rotValue-1);
          // Serial.print("Previous array value:");
          // Serial.println(presetValues[i]);
          // Serial.print("Previous element value:");
          // Serial.println(presetValues[i]%60);
          presetValues[i] = ((int)(presetValues[i]/60)*60)+rotValue-1;
          rotValue = ((presetValues[i])/60)+1;
          // Serial.print("Minutes Saved value:");
          // Serial.println(presetValues[i]);
          // Serial.print("New value:");
          // Serial.println(rotValue);
          break;
      }
      break;
    case NAME:
      if(element <= NAME_SIZE){
        newName[element-1] = rotValue-1+ASCII_OFFSET;
      }
      if(element == elementLimit){  //if we are on the last element, set the next element to 0
        i = 0;
      } else {
        i = element;
      }

      if((int)newName[i] < ASCII_OFFSET){
        rotValue = DEFAULT_CHAR;
      } else {
        rotValue = newName[i]-ASCII_OFFSET+1;
      }
      break;
  }
}

void writePresetToFile() {
  presetsFile = SD.open("PRESETS.TXT", FILE_WRITE);
  presetsFile.println();
  presetsFile.print(newName);
  for(int i = 0; i < totalCycles; i++){  
    for(int j = 0; j < VALUES_PER_CYCLE; j++){
      presetsFile.print(',');
      presetsFile.print(presetValues[i*VALUES_PER_CYCLE+j]);
    }
  }
  presetsFile.close();
}


void readPresetsIntoArrays() {

  // Create or open the file.


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
}

//This function fills either the presetsNames with all the names it finds or vthe alues arrays with the values for the line matching the passed in "name" string
//This assumes a CSV file with the name as the first value in the line, then up to NUM_CYCLES*VALUES_PER_CYCLE ints following.
size_t readField(File* file, char presetsNames[][NAME_STRING_SIZE], char* name, unsigned int values[], char* delim) {
  char ch;                                            //init temp character storage
  char str[NAME_STRING_SIZE];                                //init temp string storage
  int line = 1;                                       //init line counter for presetsNames array starts at 1 to leave room for the default preset
  int j = 0;                                          //init the values counter
  size_t n = 0;                                       //init size of string read to 0
  bool gotoNextLine = 0;
  bool thisLine = 0;
  char *ptr;                                          // Pointer for strtol used to test for valid number.

  presetsFile = SD.open("PRESETS.TXT",  O_READ | O_WRITE | O_CREAT);
  if (!presetsFile) {
    errorHalt("open failed");
  }

  // Serial.println("In read file.");
  presetsFile.seek(0);

  while(file->read(&ch, 1) == 1){
    // Serial.println(ch);
    if (ch == '\r') { continue; }                     //skip over carrage returns, they are not reliable newlines
    if(!gotoNextLine){
      if((n + 2) > NAME_STRING_SIZE || strchr(delim, ch) || !presetsFile.available()) {  //if string size is full or we have reached a string deliminator or end of file then save the value
        if(!presetsFile.available()) { //if its the end of file there is no deliminator to override so we must increment the counter to ensure we add the string terminator to the end of the string and not the last char of the string
          n++;
        }
        str[n] = '\0';                                //add string terminator
        n = 0;                                        //reset string size counter
        // Serial.println(str);
        if(name == NULL) {                            //if name is not defined then we know we are looking for names not values
          //fill up presetsNames array
          strcpy(presetsNames[line], str);            //save string to presetsNames array
          // Serial.println(presetsNames[line]);
          gotoNextLine = 1;                           //set flag to skip to end of line so we are ready to capture the next name
          line++;                                     //increment line now in case the last line doesnt end in a newline
          if(line > NUM_PRESETS){                     //if we have reached the max number of presets (including the first default, hense > and not >=)
            break;
          }
          continue;
        } else {
          if(thisLine){                               //if this is the line we want the values for (determined below)
            //fill up values array
            //Serial.println(str);
            values[j] = strtol(str, &ptr, 10);        //convert string to long and store in int array
            // Serial.print("values: ");
            // Serial.println(values[j]);
            // Serial.print("j: ");
            // Serial.println(j);
            // Serial.print("max j: ");
            // Serial.println(NUM_CYCLES*VALUES_PER_CYCLE);
            if(j >= (NUM_CYCLES*VALUES_PER_CYCLE) || ch == '\n'){    //exit if we have rerached the max number of values or the end of the values on this line
              // Serial.print("Break! ");
              // Serial.println(NUM_CYCLES*VALUES_PER_CYCLE);
              break;
            }
            j++;                                      //increment value counter
          } else {
            if(strcmp(name, str) == 0){               //if name is set and name = str then this is the line we want the values from
              // Serial.println("Name match!");
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
  presetsFile.close();
  return max(line, j);                                //return size of the built array
}

#define BUTTON_HEIGHT 50
#define BUTTON_MARGIN 12
#define CHAR_WIDTH_5 25
#define CHAR_HEIGHT_5 35
#define LETTER_SPACING_5 5
#define TWO_CHAR_WIDTH_5 (CHAR_WIDTH_5+LETTER_SPACING_5)*2
#define CHAR_WIDTH_4 20
#define CHAR_HEIGHT_4 32
#define LETTER_SPACING_4 4
#define ONE_CHAR_WIDTH_4 (CHAR_WIDTH_4+LETTER_SPACING_4)

#define HOME_LIST_PADDING 25
#define TITLE_TEXT_LEFT_MARGIN 25
#define TITLE_TEXT_TOP_MARGIN 35
#define TOP_TEXT_TOP_MARGIN 10
#define ELEMENT_TEXT_TOP_MARGIN 100
#define ELEMENT_UNDERLINE_GAP_5 8
#define ELEMENT_UNDERLINE_GAP_4 5
#define ELEMENT_UNDERLINE_WIDTH 55
#define ELEMENT_UNDERLINE_HEIGHT 4
#define ELEMENT_UNDERLINE_TOP_MARGIN_5 ELEMENT_TEXT_TOP_MARGIN+CHAR_HEIGHT_5+ELEMENT_UNDERLINE_GAP_5
#define ELEMENT_UNDERLINE_TOP_MARGIN_4 ELEMENT_TEXT_TOP_MARGIN+CHAR_HEIGHT_4+ELEMENT_UNDERLINE_GAP_4
#define TEMP_ELEMENT_TEXT_LEFT_MARGIN 65
#define RAMP_ELEMENT_TEXT_LEFT_MARGIN 55

#define UNDERLINE_GREY ConvertRGB( 197, 197, 197)
#define BUTTON_GREEN ConvertRGB( 43, 160, 13)
#define BUTTON_ORANGE ConvertRGB( 255, 92, 0)

void changeScreen(){
  element = 1;
  elementLimit = 0;
  int tmp = 0;
  tft.fillScreen(ST77XX_WHITE);
  tft.setTextColor(ST77XX_BLACK);
  switch (selectedPreset) {
    case HOME:
      if(rotValue == 0){
        rotValue = 1;
      }
      rotLimit = presetsNamesCount;
      tft.fillTriangle(5, 85, 5, 105, 17, 95, ST77XX_BLACK);
      //draw buttons
      //drawButtons(edit, (char*)"Run");
      break;
    case NEW_MANUAL:
      switch (page) {
        case CHOOSE:
          rotValue = 1;
          rotLimit = 2;
          tft.setTextSize(4);
          tft.setCursor(TITLE_TEXT_LEFT_MARGIN, 47);
          tft.println("New Routine");
          tft.setCursor(TITLE_TEXT_LEFT_MARGIN, tft.getCursorY()+25);
          tft.println("Manual Temp");
          drawButtons(back, next);
          break;
        case TEMP:
          tft.setTextSize(2);
          tft.setCursor(TITLE_TEXT_LEFT_MARGIN, TOP_TEXT_TOP_MARGIN);
          tft.print(currentCycle);
          tft.print("/");
          tft.print(totalCycles);
          drawTempScreen(back, next);
          break;
        case MANUAL:
          drawTempScreen(back, next);
          break;
        case CYCLES:
          if(totalCycles == 0){
            drawButtons(back, next);
            rotValue = 1;
          } else {
            drawButtons((char*)"Delete", next);
            rotValue = totalCycles;
          }
          rotLimit = NUM_CYCLES;
          tft.setTextSize(2);
          tft.setCursor(TITLE_TEXT_LEFT_MARGIN, TOP_TEXT_TOP_MARGIN);
          tft.println("New routine:");
          tft.fillTriangle(255, 85, 255, 105, 267, 95, ST77XX_BLACK);
          tft.setTextSize(4);
          tft.setCursor(TITLE_TEXT_LEFT_MARGIN, TITLE_TEXT_TOP_MARGIN);
          tft.println("How many\n ramp/soak\n cycles?");
          break;
        case RAMP:
          elementLimit = 2;
          tft.setTextSize(2);
          tft.setCursor(TITLE_TEXT_LEFT_MARGIN, TOP_TEXT_TOP_MARGIN);
          tft.print(currentCycle);
          tft.print("/");
          tft.print(totalCycles);
          tft.print(":Ramp to ");
          //print temperature for this cycle
          tft.print(presetValues[(currentCycle-1)*VALUES_PER_CYCLE]>>1);
          //print degree type for this cycle
          if((presetValues[((currentCycle-1)*VALUES_PER_CYCLE)] & 1)+1 == 2){
            tft.println("C");
          } else {
            tft.println("F");
          }
          drawTimeScreen((char*)"How fast?", presetValues[((currentCycle-1)*VALUES_PER_CYCLE)+1], back, next);
          break;
        case SOAK:
          elementLimit = 2;
          tft.setTextSize(2);
          tft.setCursor(TITLE_TEXT_LEFT_MARGIN, TOP_TEXT_TOP_MARGIN);
          tft.print(currentCycle);
          tft.print("/");
          tft.print(totalCycles);
          tft.print(":");
          //print temperature for this cycle
          tft.print(presetValues[(currentCycle-1)*VALUES_PER_CYCLE]>>1);
          //print degree type for this cycle
          if((presetValues[((currentCycle-1)*VALUES_PER_CYCLE)] & 1)+1 == 2){
            tft.print("C");
          } else {
            tft.print("F");
          }
          tft.print(" in ");
          //print ramp time for this cycle
          tmp = presetValues[(currentCycle-1)*VALUES_PER_CYCLE+1];
          tft.print(tmp/60);
          tft.print("h");
          tft.print(tmp%60);
          tft.print("m");
          drawTimeScreen((char*)"Soak time?", presetValues[((currentCycle-1)*VALUES_PER_CYCLE)+2], back, next);
          break;
        case SUMMARY:
          rotValue = 1;
          rotLimit = totalCycles;
          drawSummary();
          drawButtons(edit, next);
          break;
        case NAME:
          elementLimit = NAME_SIZE;
          rotLimit = ASCII_LIMIT; //number of characters in the ascii table that are standard printable characters

          // Serial.print("newName: ");
          // Serial.println(newName);

          // Serial.print("newName[0]:'");
          // Serial.println((int)newName[0]);
          // Serial.print("newName[0]:'");
          // Serial.print((char)newName[0]);
          // Serial.println("'");
          // Serial.print("old rotValue:'");
          // Serial.print(rotValue);
          // Serial.println("'");
          if((int)newName[0] > ASCII_OFFSET){
            rotValue = newName[0]-ASCII_OFFSET+1;
          } else {
            rotValue = DEFAULT_CHAR;
          }
          // Serial.print("new rotValue:'");
          // Serial.print(rotValue);
          // Serial.println("'");
          tft.setTextSize(2);
          tft.setCursor(TITLE_TEXT_LEFT_MARGIN, TOP_TEXT_TOP_MARGIN);
          tft.print(totalCycles);
          tft.println(" cycles");
          tft.setTextSize(4);
          tft.setCursor(TITLE_TEXT_LEFT_MARGIN, TITLE_TEXT_TOP_MARGIN);
          tft.println("Name?");

          for(int i = 1; i < NAME_SIZE; i++){
            tft.setCursor(HOME_LIST_PADDING+(ONE_CHAR_WIDTH_4*i), ELEMENT_TEXT_TOP_MARGIN);
            tft.print(newName[i]);
            tft.fillRect(HOME_LIST_PADDING+(ONE_CHAR_WIDTH_4*i), ELEMENT_UNDERLINE_TOP_MARGIN_4, CHAR_WIDTH_4, ELEMENT_UNDERLINE_HEIGHT, UNDERLINE_GREY);
          }
          drawButtons(back, (char*)"Save");
          break;
      }
      break;
    default:
      switch (page) {
      case RUN_SUMMARY:
        // Serial.println("In preset draw summary!");
        // Serial.print("selectedPreset: ");
        // Serial.println(selectedPreset);
        // Serial.print("presetsNames[selectedPreset-1]: ");
        // Serial.println(presetsNames[selectedPreset-1]);
        presetValuesCount = readField(&presetsFile, NULL, presetsNames[selectedPreset-1], presetValues, (char*)",\n");
        // Serial.print("Values Size: ");
        // Serial.println(presetValuesCount);
        // for (int i = 0; i < presetValuesCount; i++) {
        //   Serial.print("Values[");
        //   Serial.print(i);
        //   Serial.print("]: ");
        //   Serial.println(presetValues[i]);
        // }
        totalCycles = presetValuesCount/VALUES_PER_CYCLE;
        // Serial.print("totalCycles: ");
        // Serial.println(totalCycles);
        elementLimit = 0;
        drawSummary();
        // Serial.print("elementLimit: ");
        // Serial.println(elementLimit);
        tmp = elementLimit-SCREEN_LINES_3;
        // Serial.print("summary tmp: ");
        // Serial.println(tmp);
        if(tmp > 0){
          rotValue = 1;
          rotLimit = tmp;
        } else {
          rotValue = 0;
          rotLimit = 0;
        }
        // Serial.print("rotLimit: ");
        // Serial.println(rotLimit);
        Serial.print("changeScreen page: ");
        Serial.println(page);
        drawButtons(back, run);
        break;
      }
      break;
  }
  refreshScreen();
}

void drawSummary(int start = 0){
  int tmp = 0;
  int count = 0;
  tft.setTextSize(3);
  tft.setCursor(TOP_TEXT_TOP_MARGIN, TOP_TEXT_TOP_MARGIN+5);
  // Serial.print("drawSummary totalCycles: ");
  // Serial.println(totalCycles);
  if(start == 0){
    count++;
    tft.print("Cycles:");
    tft.println(totalCycles);
  } else {
    start--;
  }
  tft.setCursor(TOP_TEXT_TOP_MARGIN, tft.getCursorY()+8);
  for(int i = 0; i < totalCycles; i++){
    if(start == 0){
      tft.print("Ramp:");
      //print temperature for this cycle
      tft.print(presetValues[i*VALUES_PER_CYCLE]>>1);
      //print degree type for this cycle
      if((presetValues[(i*VALUES_PER_CYCLE)] & 1)+1 == 2){
        tft.print("C");
      } else {
        tft.print("F");
      }
      tft.print(":");
      //print ramp time for this cycle
      tmp = presetValues[i*VALUES_PER_CYCLE+1];
      if(tmp/60){
        tft.print(tmp/60);
        tft.print("h");
      }
      if(tmp%60){
        tft.print(tmp%60);
        tft.println("m");
      } else {
        tft.println();
      }
      count++;
      if(count > SCREEN_LINES_3){
        break;
      }
    } else {
      start--;
    }
    if(start == 0){
      tft.setCursor(TOP_TEXT_TOP_MARGIN, tft.getCursorY());
      tmp = presetValues[i*VALUES_PER_CYCLE+2];
      if(tmp){
        tft.print("Soak:");
        //print soak time for this cycle
        if(tmp/60){
          tft.print(tmp/60);
          tft.print("h");
        }
        if(tmp%60){
          tft.print(tmp%60);
          tft.println("m");
        } else {
          tft.println();
        }
        count++;
        //TODO: this is real jacked up. We need to limit the lines printed but we also need to know the total number of lines so we can allow scrolling. I think this code needs to be pulled out and rethought.
        if(count > SCREEN_LINES_3){
          break;
        }
      }
    } else {
      start--;
    }
    tft.setCursor(TOP_TEXT_TOP_MARGIN, tft.getCursorY()+8);
  }
  if(elementLimit == 0){
    elementLimit = count;
  }
}

void drawTimeScreen(char* title, int currentValue,  char* leftButton, char* rightButton){
  tft.setTextSize(4);
  tft.setCursor(TITLE_TEXT_LEFT_MARGIN, TITLE_TEXT_TOP_MARGIN);
  tft.println(title);
  tft.setTextSize(5);
  tft.setCursor(RAMP_ELEMENT_TEXT_LEFT_MARGIN, ELEMENT_TEXT_TOP_MARGIN);
  tft.print("  h ");
  rotValue = (currentValue/60)+1;
  if(currentValue == 0){
    rotValue = 1+1;
  } else {
    rotValue = (currentValue/60)+1;
  }
  currentValue = currentValue%60;
  if(currentValue < 10){
    tft.print("0");
  }
  tft.print(currentValue);
  tft.print("m");
  drawButtons(leftButton, rightButton);
}

void drawTempScreen(char* leftButton, char* rightButton) {
  elementLimit = 3;
  int tmp = 0;
  // Serial.print("On temp page:");
  // Serial.println(page);
  // Serial.print("element:");
  // Serial.println(element);
  tft.setTextSize(4);
  tft.setCursor(TITLE_TEXT_LEFT_MARGIN, TITLE_TEXT_TOP_MARGIN);
  tft.println("What temp?");
  tft.setTextSize(5);
  tft.setCursor(65, ELEMENT_TEXT_TOP_MARGIN);
  tft.print("  ");
  tmp = presetValues[(currentCycle-1)*VALUES_PER_CYCLE]>>1;
  if(tmp == 0){
    rotValue = 10+1;
  } else  {
    rotValue = (tmp/100)+1;
  }
  tmp = tmp%100;
  if(tmp < 10){
    tft.print("0");
  }
  tft.print(tmp);
  //drawCircle(x0, y0, r, color);

  tft.fillCircle(TEMP_ELEMENT_TEXT_LEFT_MARGIN+((CHAR_WIDTH_5+LETTER_SPACING_5)*4)+CHAR_WIDTH_5/2+2, ELEMENT_TEXT_TOP_MARGIN+CHAR_HEIGHT_5/4, CHAR_HEIGHT_5/4, ST77XX_BLACK);
  tft.fillCircle(TEMP_ELEMENT_TEXT_LEFT_MARGIN+((CHAR_WIDTH_5+LETTER_SPACING_5)*4)+CHAR_WIDTH_5/2+2, ELEMENT_TEXT_TOP_MARGIN+CHAR_HEIGHT_5/4, 4, ST77XX_WHITE);
  if((presetValues[((currentCycle-1)*VALUES_PER_CYCLE)] & 1)+1 == 2){
    tft.println(" C");
  } else {
    tft.println(" F");
  }
  drawButtons(leftButton, rightButton);
}

void drawButtons(char* leftButton, char* rightButton){
  tft.setTextSize(4);
  tft.setTextColor(ST77XX_WHITE);
  if(leftButton != NULL){
    tft.fillRect(0, SCREEN_HEIGHT-BUTTON_HEIGHT, SCREEN_WIDTH/2, BUTTON_HEIGHT, BUTTON_ORANGE);
    tft.setCursor(BUTTON_MARGIN, SCREEN_HEIGHT-BUTTON_HEIGHT+BUTTON_MARGIN);
    tft.println(leftButton);
  } else {
    tft.fillRect(0, SCREEN_HEIGHT-BUTTON_HEIGHT, SCREEN_WIDTH/2, BUTTON_HEIGHT, ST77XX_WHITE);
  }
  //button width is the lenth of string times char width plus left and right margins and minus the letter spacing on the last letter
  int buttonWidth = ((CHAR_WIDTH_4+LETTER_SPACING_4)*strlen(rightButton))+(2*BUTTON_MARGIN)-LETTER_SPACING_4;
  //Serial.println(buttonWidth);
  if(buttonWidth > (SCREEN_WIDTH/2)){
    tft.fillRect(SCREEN_WIDTH-buttonWidth, SCREEN_HEIGHT-BUTTON_HEIGHT, buttonWidth, BUTTON_HEIGHT, BUTTON_GREEN);
  } else {
    tft.fillRect(SCREEN_WIDTH/2, SCREEN_HEIGHT-BUTTON_HEIGHT, SCREEN_WIDTH/2, BUTTON_HEIGHT, BUTTON_GREEN);
  }
  // Serial.println(strlen(rightButton));
  tft.setCursor(SCREEN_WIDTH-buttonWidth+BUTTON_MARGIN, SCREEN_HEIGHT-BUTTON_HEIGHT+BUTTON_MARGIN);
  // tft.fillRect(SCREEN_WIDTH-buttonWidth+BUTTON_MARGIN, SCREEN_HEIGHT-BUTTON_HEIGHT+BUTTON_MARGIN, CHAR_WIDTH_4+LETTER_SPACING_4, CHAR_HEIGHT_4, ST77XX_YELLOW);
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

void printCenteredDialText(char strings[][NAME_STRING_SIZE], byte length, int padding) {
  int j = 0;
  for(int i = SCREEN_LINES_4; i > 0; i--){
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
  Serial.println("drawScreen!");
  Serial.print("selectedPreset:");
  Serial.println(selectedPreset);
  //tft.setFont(&FreeSansBold18pt7b);
  switch(selectedPreset){
    case HOME:
      //void fillRect(x0, y0, w, h, color);
      tft.fillRect(25, 17, SCREEN_WIDTH-25, SCREEN_HEIGHT-17-BUTTON_HEIGHT, ST77XX_WHITE);
      tft.setTextSize(4);
      tft.setCursor(25, 17);
      printCenteredDialText(presetsNames, presetsNamesCount, HOME_LIST_PADDING);
      if(rotValue == 1){
        drawButtons(NULL, (char*)"New/Manual");
      } else {
        drawButtons(edit, run);
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
        case CYCLES:
          //void fillRect(x0, y0, w, h, color);
          tft.fillRect(275, 17, SCREEN_WIDTH-185, SCREEN_HEIGHT-17-50, ST77XX_WHITE);
          tft.setTextSize(4);
          tft.setCursor(275, 17);
          printCenteredDialText(NULL, NUM_CYCLES, 275);
          //delay(1);
          break;
        case TEMP:
          tft.setTextSize(5);
          tft.setCursor(TEMP_ELEMENT_TEXT_LEFT_MARGIN, ELEMENT_TEXT_TOP_MARGIN);
          switch (element){
            case 1:
              rotLimit = 100;
              tft.fillRect(TEMP_ELEMENT_TEXT_LEFT_MARGIN, ELEMENT_TEXT_TOP_MARGIN, ELEMENT_UNDERLINE_WIDTH, CHAR_HEIGHT_5, ST77XX_WHITE);
              if(rotValue-1 == 0){
                tft.print("  ");
              } else {
                if(rotValue-1 < 10){
                  tft.print(" ");
                }
                tft.print(rotValue-1);
              }
              //void fillRect(x0, y0, w, h, color);
              tft.fillRect(TEMP_ELEMENT_TEXT_LEFT_MARGIN, ELEMENT_UNDERLINE_TOP_MARGIN_5, ELEMENT_UNDERLINE_WIDTH, ELEMENT_UNDERLINE_HEIGHT, ST77XX_BLACK);
              tft.fillRect(TEMP_ELEMENT_TEXT_LEFT_MARGIN+TWO_CHAR_WIDTH_5, ELEMENT_UNDERLINE_TOP_MARGIN_5, ELEMENT_UNDERLINE_WIDTH, ELEMENT_UNDERLINE_HEIGHT, UNDERLINE_GREY);
              tft.fillRect(TEMP_ELEMENT_TEXT_LEFT_MARGIN+(TWO_CHAR_WIDTH_5*2), ELEMENT_UNDERLINE_TOP_MARGIN_5, ELEMENT_UNDERLINE_WIDTH, ELEMENT_UNDERLINE_HEIGHT, UNDERLINE_GREY);
              break;
            case 2:
              rotLimit = 100;
              tft.fillRect(TEMP_ELEMENT_TEXT_LEFT_MARGIN+TWO_CHAR_WIDTH_5, ELEMENT_TEXT_TOP_MARGIN, ELEMENT_UNDERLINE_WIDTH, CHAR_HEIGHT_5, ST77XX_WHITE);
              tft.setCursor(TEMP_ELEMENT_TEXT_LEFT_MARGIN+TWO_CHAR_WIDTH_5, ELEMENT_TEXT_TOP_MARGIN);
              // Serial.print("Rot value: ");
              // Serial.println(rotValue);
              if(rotValue-1 < 10){
                tft.print("0");
              }
              tft.print(rotValue-1);
              tft.fillRect(TEMP_ELEMENT_TEXT_LEFT_MARGIN, ELEMENT_UNDERLINE_TOP_MARGIN_5, ELEMENT_UNDERLINE_WIDTH, ELEMENT_UNDERLINE_HEIGHT, UNDERLINE_GREY);
              tft.fillRect(TEMP_ELEMENT_TEXT_LEFT_MARGIN+TWO_CHAR_WIDTH_5, ELEMENT_UNDERLINE_TOP_MARGIN_5, ELEMENT_UNDERLINE_WIDTH, ELEMENT_UNDERLINE_HEIGHT, ST77XX_BLACK);
              tft.fillRect(TEMP_ELEMENT_TEXT_LEFT_MARGIN+(TWO_CHAR_WIDTH_5*2), ELEMENT_UNDERLINE_TOP_MARGIN_5, ELEMENT_UNDERLINE_WIDTH, ELEMENT_UNDERLINE_HEIGHT, UNDERLINE_GREY);
              break;
            case 3:
              rotLimit = 2;
              //x margin for this whiteout rect is a bit complex since it skips over the degree symbol
              tft.fillRect(TEMP_ELEMENT_TEXT_LEFT_MARGIN+(TWO_CHAR_WIDTH_5*2)+CHAR_WIDTH_5+LETTER_SPACING_5, ELEMENT_TEXT_TOP_MARGIN, CHAR_WIDTH_5, CHAR_HEIGHT_5, ST77XX_WHITE);
              tft.setCursor(TEMP_ELEMENT_TEXT_LEFT_MARGIN+(TWO_CHAR_WIDTH_5*2)+CHAR_WIDTH_5+LETTER_SPACING_5, ELEMENT_TEXT_TOP_MARGIN);
              // Serial.print("rotValue:");
              // Serial.println(rotValue);
              // Serial.print("rotLimit:");
              // Serial.println(rotLimit);
              if(rotValue == 2){
                tft.println(" C");
              } else {
                tft.println(" F");
              }
              tft.fillRect(TEMP_ELEMENT_TEXT_LEFT_MARGIN, ELEMENT_UNDERLINE_TOP_MARGIN_5, ELEMENT_UNDERLINE_WIDTH, ELEMENT_UNDERLINE_HEIGHT, UNDERLINE_GREY);
              tft.fillRect(TEMP_ELEMENT_TEXT_LEFT_MARGIN+TWO_CHAR_WIDTH_5, ELEMENT_UNDERLINE_TOP_MARGIN_5, ELEMENT_UNDERLINE_WIDTH, ELEMENT_UNDERLINE_HEIGHT, UNDERLINE_GREY);
              tft.fillRect(TEMP_ELEMENT_TEXT_LEFT_MARGIN+(TWO_CHAR_WIDTH_5*2), ELEMENT_UNDERLINE_TOP_MARGIN_5, ELEMENT_UNDERLINE_WIDTH, ELEMENT_UNDERLINE_HEIGHT, ST77XX_BLACK);
              break;
          }
          break;
        case SOAK:
        case RAMP:
          tft.setTextSize(5);
          tft.setCursor(RAMP_ELEMENT_TEXT_LEFT_MARGIN, ELEMENT_TEXT_TOP_MARGIN);
          switch (element){
            case 1:
              rotLimit = 24+1;
              tft.fillRect(RAMP_ELEMENT_TEXT_LEFT_MARGIN, ELEMENT_TEXT_TOP_MARGIN, ELEMENT_UNDERLINE_WIDTH, CHAR_HEIGHT_5, ST77XX_WHITE);
              if(rotValue-1 < 10){
                tft.print(" ");
              }
              tft.print(rotValue-1);
              //void fillRect(x0, y0, w, h, color);
              tft.fillRect(RAMP_ELEMENT_TEXT_LEFT_MARGIN, ELEMENT_UNDERLINE_TOP_MARGIN_5, ELEMENT_UNDERLINE_WIDTH, ELEMENT_UNDERLINE_HEIGHT, ST77XX_BLACK);
              tft.fillRect(RAMP_ELEMENT_TEXT_LEFT_MARGIN+(TWO_CHAR_WIDTH_5*2), ELEMENT_UNDERLINE_TOP_MARGIN_5, ELEMENT_UNDERLINE_WIDTH, ELEMENT_UNDERLINE_HEIGHT, UNDERLINE_GREY);
              break;
            case 2:
              rotLimit = 60;
              tft.fillRect(RAMP_ELEMENT_TEXT_LEFT_MARGIN+(TWO_CHAR_WIDTH_5*2), ELEMENT_TEXT_TOP_MARGIN, ELEMENT_UNDERLINE_WIDTH, CHAR_HEIGHT_5, ST77XX_WHITE);
              tft.setCursor(RAMP_ELEMENT_TEXT_LEFT_MARGIN+(TWO_CHAR_WIDTH_5*2), ELEMENT_TEXT_TOP_MARGIN);
              if(rotValue-1 < 10){
                tft.print("0");
              }
              tft.print(rotValue-1);
              tft.fillRect(RAMP_ELEMENT_TEXT_LEFT_MARGIN, ELEMENT_UNDERLINE_TOP_MARGIN_5, ELEMENT_UNDERLINE_WIDTH, ELEMENT_UNDERLINE_HEIGHT, UNDERLINE_GREY);
              tft.fillRect(RAMP_ELEMENT_TEXT_LEFT_MARGIN+(TWO_CHAR_WIDTH_5*2), ELEMENT_UNDERLINE_TOP_MARGIN_5, ELEMENT_UNDERLINE_WIDTH, ELEMENT_UNDERLINE_HEIGHT, ST77XX_BLACK);
              break;
          }
          break;
        case NAME:
          char tmp;
          tft.setTextSize(4);
          // Serial.print("int newName[element]:");
          // Serial.print((int)newName[element]);
          // Serial.print("char newName[element]:'");
          // Serial.print(newName[element]);
          // Serial.println("'");
          // Serial.print("rotValue:'");
          // Serial.print(rotValue);
          // Serial.println("'");
          switch (element){
            case 1:
              tft.fillRect(HOME_LIST_PADDING, ELEMENT_TEXT_TOP_MARGIN, CHAR_WIDTH_4, CHAR_HEIGHT_4, ST77XX_WHITE);
              tft.setCursor(HOME_LIST_PADDING, ELEMENT_TEXT_TOP_MARGIN);
              tmp = rotValue-1+ASCII_OFFSET;
              tft.print(tmp);
              //void fillRect(x0, y0, w, h, color);
              tft.fillRect(HOME_LIST_PADDING, ELEMENT_UNDERLINE_TOP_MARGIN_4, CHAR_WIDTH_4, ELEMENT_UNDERLINE_HEIGHT, ST77XX_BLACK);
              tft.fillRect(HOME_LIST_PADDING+(ONE_CHAR_WIDTH_4*(NAME_SIZE-1)), ELEMENT_UNDERLINE_TOP_MARGIN_4, CHAR_WIDTH_4, ELEMENT_UNDERLINE_HEIGHT, UNDERLINE_GREY);
              break;
            default:
              tft.fillRect(HOME_LIST_PADDING+(ONE_CHAR_WIDTH_4*(element-1)), ELEMENT_TEXT_TOP_MARGIN, CHAR_WIDTH_4, CHAR_HEIGHT_4, ST77XX_WHITE);
              tft.setCursor(HOME_LIST_PADDING+(ONE_CHAR_WIDTH_4*(element-1)), ELEMENT_TEXT_TOP_MARGIN);
              tmp = rotValue-1+ASCII_OFFSET;
              tft.print(tmp);
              tft.fillRect(HOME_LIST_PADDING+(ONE_CHAR_WIDTH_4*(element-2)), ELEMENT_UNDERLINE_TOP_MARGIN_4, CHAR_WIDTH_4, ELEMENT_UNDERLINE_HEIGHT, UNDERLINE_GREY);
              tft.fillRect(HOME_LIST_PADDING+(ONE_CHAR_WIDTH_4*(element-1)), ELEMENT_UNDERLINE_TOP_MARGIN_4, CHAR_WIDTH_4, ELEMENT_UNDERLINE_HEIGHT, ST77XX_BLACK);
              break;
          }
          break;
      }
      //delay(1);
      break;
    default:
      Serial.println("refresh default!");
      Serial.print("page:");
      Serial.println(page);
      switch (page) {
        case RUN_SUMMARY:
          //void fillRect(x0, y0, w, h, color);
          tft.fillRect(25, 17, SCREEN_WIDTH-25, SCREEN_HEIGHT-17-BUTTON_HEIGHT, ST77XX_WHITE);
          Serial.print("refresh default summary! rotValue:");
          Serial.println(rotValue);
          if(rotLimit == 0){
            drawSummary();
          } else {
            drawSummary(rotValue);
          }
          //delay(1);
          break;
        case RUN:
          break;

      }
      break;
  }
  //void fillTriangle(x0, y0, x1, y1, x2, y2, color);
}

// rotate is called anytime the rotary inputs change state.
void rotate() {
  unsigned char result = rotary.process();

  if (result == DIR_CCW) {
    // Serial.println("Counter-clockwise!");
    // Serial.print("pre-rotLimit:");
    // Serial.println(rotLimit);
    // Serial.print("pre-rotValue:");
    // Serial.println(rotValue);
    rotValue--;

    if(rotValue > 0) {
      rotValue = rotValue%rotLimit;        //if rotValue is positive, change it to modulous of rotLimit so it doesnt go higher than rotLimit
    }
    if(rotValue == 0){                      //If current rotValue goes below zero
      rotValue = rotLimit+rotValue;        //add the rotLimit to allow rotValue to loop back to the end
    }
    // Serial.print("post-rotLimit:");
    // Serial.println(rotLimit);
    // Serial.print("post-rotValue:");
    // Serial.println(rotValue);
    refreshScreen();

  } else if (result == DIR_CW) {
    // Serial.println("Clockwise!");
    // Serial.print("pre-rotLimit:");
    // Serial.println(rotLimit);
    // Serial.print("pre-rotValue:");
    // Serial.println(rotValue);
    rotValue++;

    if(rotValue > 0) {
      rotValue = rotValue%rotLimit;        //if rotValue is positive, change it to modulous of rotLimit so it doesnt go higher than rotLimit
    }
    if(rotValue == 0){                      //If current rotValue goes below zero
      rotValue = rotLimit+rotValue;        //add the rotLimit to allow rotValue to loop back to the end
    }
    // Serial.print("post-rotLimit:");
    // Serial.println(rotLimit);
    // Serial.print("post-rotValue:");
    // Serial.println(rotValue);
    refreshScreen();
  }
  //CODE CANT GO HERE BECAUSE THIS FUNCTION IS CALLED EVEN WHEN THERE IS NO ROTATION
  //COMMON CODE WOULD NEED TO BE NESTED IN AN IF STATEMENT THAT CHECKS FOR EITHER ROTATION
  //Serial.print(rotValue);
}

word ConvertRGB( byte R, byte G, byte B)
{
  return ( ((R & 0xF8) << 8) | ((G & 0xFC) << 3) | (B >> 3) );
}