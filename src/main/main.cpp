#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C
#define TOUCH_PIN 4



Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Input State Machine
const int DOUBLE_TAP_DELAY = 350; // Max time between taps for double-click
const int LONG_PRESS_TIME = 600;  // Time to hold before triggering Long Press
unsigned long touchStartTime = 0;
unsigned long lastTapTime = 0;
bool isTouching = false;
bool isLongPressing = false;
int tapCount = 0;

// Face geometry
#define BASE_EYE_W 30
#define EYE_H 44
#define EYE_Y 5
#define EYE_X_L 16
#define EYE_X_R 82
#define EYE_RADIUS 8
#define MOUTH_Y 42

unsigned long now;
unsigned long lastBlink_time=0;

float currentEyeH = EYE_H;
float targetEyeH = EYE_H;
float currentMouthSize = 9.0;
float targetMouthSize = 9.0;

//                 ANIMATION

//                  tweening
float moveTowards(float current, float target, float speed) {
    if (abs(current - target) < speed) return target;
    if (current < target) return current + speed;
    if (current > target) return current - speed;
    return target;
}


//                     INPUT 

void touchInput(){

}



void drawEyes() {
    int h=(int)currentEyeH;
     if (h < 2) h = 2;
    int ly = EYE_Y + (EYE_H - h) / 2;

    int radius = (h <= 4) ? 2 : EYE_RADIUS;

    display.fillRoundRect(EYE_X_L, ly, BASE_EYE_W, h, radius, SSD1306_WHITE);
    display.fillRoundRect(EYE_X_R, ly, BASE_EYE_W, h, radius, SSD1306_WHITE);
}

void drawMouth() {
    // big circle with top cut off by black circle
    display.fillCircle(64, MOUTH_Y + 5, 9, SSD1306_WHITE);
    display.fillCircle(64, MOUTH_Y + 1, 9, SSD1306_BLACK);
}

void updateBlink(){
    //if(isbeingpetted||postpet)return;
    
    now=millis();
    static long interval=3500;
    static long duration=150;
    static int isBlinking=0;
    static unsigned long Blinkstart_time=0;
    
    if(!isBlinking && now-lastBlink_time>interval){
        isBlinking=1;
        Blinkstart_time=now;
        targetEyeH=4;
    }
    
    if(isBlinking && now-Blinkstart_time>duration){
        isBlinking=0;
        lastBlink_time=now;
        targetEyeH=EYE_H;

        interval=random(3500,7000);
        duration=random(100,200);
    }
    
}



void setup() {
    Serial.begin(115200);
    Wire.begin(21, 22);

    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        Serial.println("OLED failed");
        for(;;);
    }
}

void loop() {
    updateBlink();

    currentEyeH = moveTowards(currentEyeH, targetEyeH, 3.0);
    currentMouthSize = moveTowards(currentMouthSize, targetMouthSize, 1.5);

    display.clearDisplay();
    drawEyes();
    drawMouth();
    display.display();
    delay(20);
}