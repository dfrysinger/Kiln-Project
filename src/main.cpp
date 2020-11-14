#include <Arduino.h>    // Core arduino library
#include <Wire.h>
#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789
#include <SPI.h>
//#include <FreeSans12pt7b.h>
//#include <FreeSans18pt7b.h>
//#include <FreeSans24pt7b.h>
#include <FreeSans9pt7b.h>
//#include <FreeSansBold12pt7b.h>
#include <FreeSansBold18pt7b.h>
//#include <FreeSansBold24pt7b.h>
#include <FreeSansBold9pt7b.h>
#include <Adafruit_MAX31855.h>
#include <Rotary.h>
#include <Debounce.h>

// For the breakout board, you can use any 2 or 3 pins.
// These pins will also work for the 1.8" TFT shield.
#define TFT_CS        10
#define TFT_RST       -1 // Or set to -1 and connect to Arduino RESET pin
#define TFT_DC         9

#define RightButtonPin         A4 //What pin is the rotary encoder button connected to
#define LeftButtonPin          A5 //What pin is the push button connected to

Debounce RightButton(RightButtonPin, 30);
Debounce LeftButton(LeftButtonPin, 30);

#define ROTCOUNT              24 // Number of detents in a single rotation of the knob
// Rotary encoder is wired with the common to ground and the two
// outputs to pins 2 and 3. 
//NOTE: Interrupt conflicts with I2C and only works on pin 2 & 3.
Rotary rotary = Rotary(3, 2);


// OPTION 1 (recommended) is to use the HARDWARE SPI pins, which are unique
// to each board and not reassignable. For Arduino Uno: MOSI = pin 11 and
// SCLK = pin 13. This is the fastest mode of operation and is required if
// using the breakout board's microSD card.

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

int highlight = 0;

int rightButtonState = 0;         // variable for reading the pushbutton status
int leftButtonState = 0;         // variable for reading the pushbutton status

int ScreenLines = 5; //Number of lines of text that will be displayed on the screen when listing items

int MenuSize = 8;
char *MenuStrings[] = {"New/Manual", "Melt Aluminum", "Melt Gold", "Lost SLA", "Lost Wax", "Melt Steel", "Warm Tea", "Bake Bread"};

void drawScreen();
void drawTemp();
void rotate();

void setup(void) {
  Serial.begin(9600);
  Serial.print(F("Hello! ST77xx TFT Test"));
  
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
  Serial.print("Initializing sensor...");
  if (!thermocouple.begin()) {
    Serial.println("ERROR.");
    while (1) delay(10);
  }

  Serial.println("done");
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
    tft.fillScreen(ST77XX_RED);
    Serial.println("Right Button Pressed!");
  }

  leftButtonState = LeftButton.read();
  //Serial.print("leftButtonState: ");
  //Serial.println(leftButtonState);
  
  if (leftButtonState == LOW) {
    // turn LED on:
    tft.fillScreen(ST77XX_GREEN);
    Serial.println("Left Button Pressed!");
  }
  
  if(millis() - lastRefreshTime >= REFRESH_INTERVAL) {
    lastRefreshTime += REFRESH_INTERVAL;
    drawTemp();
  }
}

void drawTemp() {
  tft.setTextColor(ST77XX_RED);
  tft.setFont(&FreeSansBold9pt7b);
  tft.setTextSize(1);

//void fillRect(x0, y0, w, h, color);
  tft.fillRect(270, 0 , 60, 25, ST77XX_WHITE);

  tft.setCursor(275, 20);
  double f = thermocouple.readFahrenheit();
  if (isnan(f)) {
    tft.println("Err");
  } else {
    tft.print(f, 0);
    tft.println(" F");
  }
}

void drawScreen() {
  int j = 0;

  tft.fillScreen(ST77XX_WHITE);

  drawTemp();
  
  tft.setTextColor(ST77XX_BLACK);
  tft.setFont(&FreeSansBold18pt7b);
  tft.setTextSize(1);
  tft.setCursor(25, 47);
  
  for(int i = 0; i < ScreenLines; i++){
    j = i-2+highlight;
    if(j < 0){
      j = MenuSize+j;
    } else {
      j = j%MenuSize;
    }
    tft.println(MenuStrings[j]);
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
      highlight = MenuSize+highlight;
    } else {
      highlight = highlight%MenuSize;
    }
    Serial.print(highlight);
    drawScreen();
  } else if (result == DIR_CW) {
    Serial.print("Clockwise!");
    highlight++;
    if(highlight < 0){
      highlight = MenuSize+highlight;
    } else {
      highlight = highlight%MenuSize;
    }
    Serial.print(highlight);
    drawScreen();
  }
}