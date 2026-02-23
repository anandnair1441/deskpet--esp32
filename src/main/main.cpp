#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C
#define TOUCH_PIN 4

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

//-----------------------Input State Machine-----------------------
const int DOUBLE_TAP_DELAY = 350; // Max time between taps for double-click
const int LONG_PRESS_TIME = 600;  // Time to hold before triggering Long Press
unsigned long touchStartTime = 0;
unsigned long lastTapTime = 0;
bool isTouching = 0;
int tapCount = 0;
int singleTap = 0;
int doubleTap = 0;
int isLongpressing = 0;
int releasedLongpress = 0;

//-----------------------Face geometry-----------------------
#define BASE_EYE_W 30
#define EYE_H 44
#define EYE_Y 5
#define EYE_X_L 16
#define EYE_X_R 82
#define EYE_RADIUS 8
#define MOUTH_Y 42

#define SQUINT_HEIGHT 12
#define SQUINT_DURATION 1500


unsigned long now;
unsigned long lastBlink_time = 0;
unsigned long lastInteractionTime = 0;
bool isSquinting = false;
unsigned long squintStartTime = 0;

float currentEyeH = EYE_H;
float targetEyeH = EYE_H;
float currentMouthSize = 9.0;
float targetMouthSize = 9.0;

//-----------------------ANIMATION-----------------------

//-----------------------Tweening------------------------
float moveTowards(float current, float target, float speed){
    if (abs(current - target) < speed)
        return target;
    if (current < target)
        return current + speed;
    if (current > target)
        return current - speed;
    return target;
}


//------------------Squint Styles------------------
enum SquintStyle {
    SQUINT_FLAT,
    SQUINT_CRESCENT
};

SquintStyle currentSquintStyle;

//-----------------------INPUT-----------------------

void touchInput(){
    now = millis();
    bool touch = touchRead(TOUCH_PIN) < 60;

    // Rise
    if (touch && !isTouching){
        isTouching = 1;
        touchStartTime = now;
        lastInteractionTime = now;
        singleTap = 0;
        doubleTap = 0;
        releasedLongpress = 0;
    }

    // Hold
    if (touch && isTouching){
        if (!isLongpressing && (now - touchStartTime > LONG_PRESS_TIME)){
            isLongpressing = 1;
            tapCount = 0;
        }
    }

    // Fall
    if (!touch && isTouching){
        isTouching = 0;
        if (isLongpressing){
            isLongpressing = 0;
            releasedLongpress = 1;
        }
        else{
            tapCount++;
            lastTapTime = now;
        }
    }

    // Timeout Check
    if (!touch && !isLongpressing && tapCount > 0){
        if (now - lastTapTime > DOUBLE_TAP_DELAY){
            if (tapCount == 1)
                singleTap = 1;
            else if (tapCount >= 2)
                doubleTap = 1;
            tapCount = 0;
        }
    }
}


void drawCrescentEye(int centerX) {
    int centerY = EYE_Y + EYE_H / 2;

    float t = currentEyeH / EYE_H;  // gives 0.0 to 1.0
    int radius = 10 + (int)(t * 4);       // scales 6 to 14 Controls ,curve size
    int thickness = 3 + (int)(t * 5);    // scales 3 to 8 ,Controls crescent thickness

    // Draw full white circle
    display.fillCircle(centerX, centerY, radius, SSD1306_WHITE);

    // Cover bottom part with black rectangle
    display.fillRect(centerX - radius, centerY, radius * 2, radius, SSD1306_BLACK);

    //thin bottom trim for smoother look
    display.fillCircle(centerX, centerY + thickness, radius, SSD1306_BLACK);
}

 
void onSingleTap() {                       
    isSquinting = true;
    squintStartTime = millis();

    // Randomly choose eye style
    if (random(0, 2) == 0)
        currentSquintStyle = SQUINT_FLAT;
    else
        currentSquintStyle = SQUINT_CRESCENT;

    // 40% chance of bigger lower smile
    if (random(0, 100) < 40) {
        targetMouthSize = 13;  // Wider smile
    } else {
        targetMouthSize = 9;   // Normal smile
    }

    targetEyeH = SQUINT_HEIGHT; // Partial close
    
}


void updateSquint(){
    if (isSquinting){
        if (millis() - squintStartTime > SQUINT_DURATION){
            isSquinting = false;
            targetEyeH = EYE_H;
            targetMouthSize = 9.0;
            lastBlink_time = now;
        
        }
    }
}                                          

    //petting animation after and if its long enough a happy content face at the end

    // only three modes changable via manually--pet normal,clk,weather

    // post petting face

void drawEyes(){
    if (isSquinting) {
        if (currentSquintStyle == SQUINT_CRESCENT) {
            drawCrescentEye(EYE_X_L + BASE_EYE_W / 2);
            drawCrescentEye(EYE_X_R + BASE_EYE_W / 2);
            return;
        }
        // Flat squint
        int h = SQUINT_HEIGHT;
        int ly = EYE_Y + (EYE_H - h) / 2;

        display.fillRoundRect(EYE_X_L, ly, BASE_EYE_W, h, 4, SSD1306_WHITE);
        display.fillRoundRect(EYE_X_R, ly, BASE_EYE_W, h, 4, SSD1306_WHITE);
        return;
    }

    int h = (int)currentEyeH;
    if (h < 2) h = 2;
    int ly = EYE_Y + (EYE_H - h) / 2;

    int radius = (h <= 4) ? 2 : EYE_RADIUS;

    display.fillRoundRect(EYE_X_L, ly, BASE_EYE_W, h, radius, SSD1306_WHITE);
    display.fillRoundRect(EYE_X_R, ly, BASE_EYE_W, h, radius, SSD1306_WHITE);
}

void drawMouth(){
    int s = (int)currentMouthSize;
    if (s < 1) return;
    display.fillCircle(64, MOUTH_Y + 5, s, SSD1306_WHITE);
    display.fillCircle(64, MOUTH_Y + 1, s, SSD1306_BLACK);
}

void updateBlink(){
    if (isSquinting || isLongpressing || releasedLongpress) return;

    now = millis();
    static long interval = 3500;
    static long duration = 150;
    static int isBlinking = 0;
    static unsigned long Blinkstart_time = 0;

    if (!isBlinking && now - lastBlink_time > interval){
        isBlinking = 1;
        Blinkstart_time = now;
        targetEyeH = 4;
    }

    if (isBlinking && now - Blinkstart_time > duration){
        isBlinking = 0;
        lastBlink_time = now;
        targetEyeH = EYE_H;

        interval = random(3500, 7000);
        duration = random(100, 200);
    }
}
// single tap  → quick press and release under 600ms, no second tap follows
//  double tap  → two quick taps within 400ms of each other
//  long press  → held over 600ms
//  release     → finger lifted after long press




void setup(){
    Serial.begin(115200);
    Wire.begin(21, 22);

    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)){
        Serial.println("OLED failed");
        for (;;)
            ;
    }
}

void loop(){
    touchInput();   
    updateBlink();
    updateSquint();

    if (singleTap) {
        onSingleTap();
        singleTap = 0;
    }

    currentEyeH = moveTowards(currentEyeH, targetEyeH, 4.5);
    currentMouthSize = moveTowards(currentMouthSize, targetMouthSize, 1.5);

    display.clearDisplay();
    drawEyes();
    drawMouth();
    display.display();
    delay(20);
}