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

// For the breakout board, you can use any 2 or 3 pins.
// These pins will also work for the 1.8" TFT shield.
#define TFT_CS        10
#define TFT_RST       -1 // Or set to -1 and connect to Arduino RESET pin
#define TFT_DC         9

//Define the SD Card chip select pin (SDCS)
#define CS_PIN 4

#define RightButtonPin         A4 //What pin is the rotary encoder button connected to
#define LeftButtonPin          A5 //What pin is the push button connected to

Debounce RightButton(RightButtonPin, 30);
Debounce LeftButton(LeftButtonPin, 30);

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

static const unsigned long REFRESH_INTERVAL = 1000; // ms
static unsigned long lastRefreshTime = 0;

byte rightButtonState = 0;         // variable for reading the pushbutton status
byte leftButtonState = 0;          // variable for reading the pushbutton status

#define SCREEN_LINES 5 //Number of lines of text that will be displayed on the screen when listing items

// Define the limits of the presets
#define NUM_PRESETS 20
#define NUM_CYCLES 10
#define VALUES_PER_CYCLE 3

#define NAME_SIZE 20

File presetsFile;

// Array for data.
char presetsNames[NUM_PRESETS][NAME_SIZE];
int presetValues[NUM_CYCLES*VALUES_PER_CYCLE];

byte presetsNamesCount = 1;
byte presetValuesCount = 0;
byte selectedPreset = 0;

byte highlight = 0;

#define errorHalt(msg) {Serial.println(F(msg)); while(1); }
//#define errorHalt(msg) {while(1); }

void drawScreen();
void drawTemp();
void rotate();
void rotate();
void readPresetsIntoArrays();
size_t readField(File* file, char presetsNames[][NAME_SIZE], char* name, int values[], char* delim);
char *rtrim(char *str, const char *seps);
char *ltrim(char *str, const char *seps);

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

  tft.init(240, 320);           // Init ST7789 320x240
  tft.setRotation(3);

  Serial.println(F("Initialized"));

  tft.setTextWrap(false);
  tft.fillScreen(ST77XX_WHITE);

  
  // tft print function!
  drawScreen();
  delay(1);

  // wait for MAX chip to stabilize
  delay(500);
  //Serial.print("Initializing sensor...");
  if (!thermocouple.begin()) {
    errorHalt("Thermocouple error");
  }
  //Serial.println("done");
  delay(3000);
}

void loop() {
  // read the state of the pushbutton value:
  //  buttonState = digitalRead(buttonPin);

  rightButtonState = RightButton.read();
  //Serial.print("rightButtonState: ");
  //Serial.println(rightButtonState);

  if (rightButtonState == LOW) {
    // turn LED on:
    //tft.fillScreen(ST77XX_RED);
    selectedPreset = highlight;
    //Serial.println("Right Button Pressed!");
  }

  leftButtonState = LeftButton.read();
  //Serial.print("leftButtonState: ");
  //Serial.println(leftButtonState);
  
  if (leftButtonState == LOW) {
    // turn LED on:
    tft.fillScreen(ST77XX_GREEN);
    //Serial.println("Left Button Pressed!");
  }
  
  if(millis() - lastRefreshTime >= REFRESH_INTERVAL) {
    lastRefreshTime += REFRESH_INTERVAL;
    drawTemp();
  }
}

void readPresetsIntoArrays() {

  // Create or open the file.
  presetsFile = SD.open("PRESETS.TXT",  O_READ | O_WRITE | O_CREAT);
  if (!presetsFile) {
    errorHalt("open failed");
  }

  presetsNamesCount = readField(&presetsFile, presetsNames, NULL, NULL, ",\n");
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
  char str[20];                                       //init temp string storage
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
      if((n + 1) > NAME_SIZE || strchr(delim, ch)) {  //if string size is full or we have reached a string deliminator then save the value
        str[n] = '\0';                                //add string terminator
        n = 0;                                        //reset string size counter
        //Serial.println(str);
        if(name == NULL) {                            //if name is not defined then we know we are looking for names not values
          //fill up presetsNames array
          strcpy(presetsNames[line], str);            //save string to presetsNames array
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

void drawTemp() {
  tft.setTextColor(ST77XX_RED);
  //tft.setFont(&FreeSansBold9pt7b);
  tft.setTextSize(1);

//void fillRect(x0, y0, w, h, color);
  tft.fillRect(270, 0 , 60, 25, ST77XX_WHITE);

  tft.setCursor(275, 20);
  double f = thermocouple.readFahrenheit();
  if (!isnan(f)) {
    tft.print(f, 0);
    tft.println(" F");
  }
}

void drawScreen() {
  int j = 0;

  tft.fillScreen(ST77XX_WHITE);

  drawTemp();
  
  tft.setTextColor(ST77XX_BLACK);
  //tft.setFont(&FreeSansBold18pt7b);
  tft.setTextSize(4);
  tft.setCursor(25, 47);
  
  for(int i = 0; i < SCREEN_LINES; i++){
    j = i-2+highlight;
    if(j < 0){
      j = presetsNamesCount+j;
    } else {
      j = j%presetsNamesCount;
    }
    tft.println(presetsNames[j]);
    //tft.println(RightButton.read());
    
    tft.setCursor(25, tft.getCursorY());
  }
  tft.fillTriangle(5, 110, 5, 130, 17, 120, ST77XX_BLACK);
  delay(1);
  //void fillTriangle(x0, y0, x1, y1, x2, y2, color);
}

// rotate is called anytime the rotary inputs change state.
void rotate() {
  unsigned char result = rotary.process();
  if (result == DIR_CCW) {
    Serial.print("Counter-clockwise!");
    highlight--;
    if(highlight < 0){
      highlight = presetsNamesCount+highlight;
    } else {
      highlight = highlight%presetsNamesCount;
    }
    Serial.print(highlight);
    drawScreen();
  } else if (result == DIR_CW) {
    Serial.print("Clockwise!");
    highlight++;
    if(highlight < 0){
      highlight = presetsNamesCount+highlight;
    } else {
      highlight = highlight%presetsNamesCount;
    }
    Serial.print(highlight);
    drawScreen();
  }
}