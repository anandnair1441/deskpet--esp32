#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLR 0x3C


Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

//-----------------------Input State Machine-----------------------
#define TOUCH_PIN 4
#define DOUBLE_TAP_DELAY 350; // Max time b/w taps for double-click
#define LONG_PRESS_TIME 600;  // Time to hold before triggering Long Press
#define PET_THRESHOLD 2000
//-----------------------Face geometry-----------------------
#define BASE_EYE_W 30
#define EYE_H 44
#define EYE_Y 5
#define EYE_X_L 16
#define EYE_X_R 82
#define EYE_RADIUS 8
#define MOUTH_Y 42

#define SQUINT_DURATION 1250

unsigned long now;

unsigned long touchStartTime = 0;
unsigned long lastTapTime = 0;
bool isTouching = 0;
int touchCount = 0;
int singleTouch = 0;
int doubleTouch = 0;
int isLongTouch = 0;
int isPostTouch = 0;
unsigned long postTouchStart = 0;
bool isBeingPetted = 0;

unsigned long lastInteractionTime = 0;
unsigned long lastBlink_time = 0;
bool isSquinting = 0;
unsigned long squintStartTime = 0;

float currentEyeH = EYE_H;
float targetEyeH = EYE_H;
float currentMouthSize = 9.0;
float targetMouthSize = 9.0;
int mouth_shape = 0;




void doubleTapAction();





//-----------------------ANIMATION-----------------------

//-----------------------Tweening------------------------
float moveTowards(float current, float target, float speed)
{
    if (abs(current - target) <= speed)
        return target;

    if (current < target)
        return current + speed;
    if (current > target)
        return current - speed;
    return target;
}

//------------------Squint Styles------------------
enum SquintStyle
{
    SQUINT_FLAT,
    SQUINT_CRESCENT
};

SquintStyle currentSquintStyle;

//-----------------------INPUT-----------------------

void touchInput()
{
    now = millis();
    bool touch = touchRead(TOUCH_PIN) < 60;

    // Rise
    if (touch && !isTouching)
    {
        isTouching = 1;
        touchStartTime = now;
        lastInteractionTime = now;
        singleTouch = 0;
        doubleTouch = 0;
        isPostTouch = 0;
        mouth_shape = 0;     
        targetMouthSize = 9.0;

        if (isPostTouch){
        // transition directly to squint, no flat frame
        isSquinting = true;
        squintStartTime = now;
        targetEyeH = 12;
        currentSquintStyle = SQUINT_CRESCENT;
    }
    
    isPostTouch = 0;
    }

    // Hold
    if (touch && isTouching)
    {
        if (!isLongTouch && (now - touchStartTime > LONG_PRESS_TIME))
        {
            isLongTouch = 1;
            touchCount = 0;
        }
    }

    // Fall
    if (!touch && isTouching){
        isTouching = 0;

        if (isLongTouch){
            isLongTouch = 0;
            isBeingPetted = 0;
            bool fullPet = (now - touchStartTime)>PET_THRESHOLD;
            
            if (fullPet){
                isPostTouch = 1;
                postTouchStart = now;
                mouth_shape = 1;
                targetMouthSize = 9.0;
            }
            else{
                targetEyeH = EYE_H;
                targetMouthSize = 9.0;
                mouth_shape = 0;
            }
        }
        else{
            touchCount++;
            lastTapTime = now;
        }
    }

    // Timeout Check
    if (!touch && !isLongTouch && touchCount > 0)
    {
        if (now - lastTapTime > DOUBLE_TAP_DELAY)
        {
            if (touchCount == 1)
                singleTouch = 1;
            else if (touchCount >= 2)
                doubleTouch = 1;
            touchCount = 0;
        }
    }
}

void drawCrescentEye(int centerX){
    int centerY = EYE_Y + EYE_H / 2;

    float t = currentEyeH / EYE_H;    // gives 0.0 to 1.0
    int radius = 10 + (int)(t * 4);   // scales 6 to 14 Controls ,curve size
    int thickness = 3 + (int)(t * 5); // scales 3 to 8 ,Controls crescent thickness

    // Draw full white circle
    display.fillCircle(centerX, centerY, radius, SSD1306_WHITE);

    // Cover bottom part with black rectangle
    display.fillRect(centerX - radius, centerY, radius * 2, radius, SSD1306_BLACK);

    // thin bottom trim for smoother look
    display.fillCircle(centerX, centerY + thickness, radius, SSD1306_BLACK);
}

void drawPostPettingEyes()
{

    int centerY = EYE_Y + 20;

    int radiusX = 16;
    int radiusY = 10;

    int leftCX = 40;
    int rightCX = 88;

    for (int y = 0; y <= radiusY; y++)
    {

        float ratio = (float)y / radiusY;
        int xSpan = radiusX * sqrt(1.0 - ratio * ratio);

        // Left eye
        display.drawFastHLine(leftCX - xSpan,
                              centerY - y,
                              xSpan * 2,
                              SSD1306_WHITE);

        // Right eye
        display.drawFastHLine(rightCX - xSpan,
                              centerY - y,
                              xSpan * 2,
                              SSD1306_WHITE);
    }
}

void SingleTapAction()
{
    isSquinting = true;
    squintStartTime = millis();
    targetEyeH = 12;
    // Randomly choose eye style
    if (random(0, 2) == 0)
        currentSquintStyle = SQUINT_FLAT;
    else
        currentSquintStyle = SQUINT_CRESCENT;

    // 40% chance of bigger lower smile
    if (random(0, 100) < 40)
    {
        targetMouthSize = 13; // Wider smile
    }
    else
    {
        targetMouthSize = 9; // Normal smile
    }
}

void LongPressAction()
{
    isBeingPetted = 1;
    currentSquintStyle = SQUINT_CRESCENT;
    targetEyeH = 6;
    targetMouthSize = 0;
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

// petting animation after and if its long enough a happy content face at the end

// only three modes changable via manually--pet normal,clk,weather

// post petting face

void drawEyes()
{
    int h;
    int ly;
    int radius;

    if (isPostTouch)
    {
        if (mouth_shape == 1)
        {
            drawPostPettingEyes();
        }
        else
        {
            drawCrescentEye(EYE_X_L + BASE_EYE_W / 2); // crescent for short press
            drawCrescentEye(EYE_X_R + BASE_EYE_W / 2);
        }
        return;
    }
    if (isBeingPetted == 1)
    {
        drawCrescentEye(EYE_X_L + BASE_EYE_W / 2);
        drawCrescentEye(EYE_X_R + BASE_EYE_W / 2);
        return;
    }

    if (isSquinting)
    {
        if (currentSquintStyle == SQUINT_CRESCENT)
        {
            drawCrescentEye(EYE_X_L + BASE_EYE_W / 2);
            drawCrescentEye(EYE_X_R + BASE_EYE_W / 2);
            return;
        }
        // Flat squint
        h = (int)currentEyeH;
        radius = 4;
    }
    else
    {
        // normal eyes
        h = (int)currentEyeH;
        if (h < 2)
            h = 2;
        radius = (h <= 4) ? 2 : EYE_RADIUS;
    }

    ly = EYE_Y + (EYE_H - h) / 2;

    display.fillRoundRect(EYE_X_L, ly, BASE_EYE_W, h, radius, SSD1306_WHITE);
    display.fillRoundRect(EYE_X_R, ly, BASE_EYE_W, h, radius, SSD1306_WHITE);
}

void drawMouth()
{

    if (mouth_shape == 1)
    { // joy mode

        int cx = 64;
        int cy = MOUTH_Y + 4;

        display.drawLine(cx - 10, cy,
                         cx - 5, cy + 5,
                         SSD1306_WHITE);

        display.drawLine(cx - 5, cy + 5,
                         cx, cy + 2,
                         SSD1306_WHITE);

        display.drawLine(cx, cy + 2,
                         cx + 5, cy + 5,
                         SSD1306_WHITE);

        display.drawLine(cx + 5, cy + 5,
                         cx + 10, cy,
                         SSD1306_WHITE);
        return;
    }

    int s = (int)currentMouthSize;
    if (s < 1)
        return;

    display.fillCircle(64, MOUTH_Y + 5, s, SSD1306_WHITE);
    display.fillCircle(64, MOUTH_Y + 1, s, SSD1306_BLACK);
}

void updateBlink()
{
    if (isSquinting || isLongTouch || isPostTouch || isBeingPetted)
        return;

    now = millis();
    static long interval = 3500;
    static long duration = 150;
    static int isBlinking = 0;
    static unsigned long Blinkstart_time = 0;

    if (!isBlinking && now - lastBlink_time > interval)
    {
        isBlinking = 1;
        Blinkstart_time = now;
        targetEyeH = 4;
    }

    if (isBlinking && now - Blinkstart_time > duration)
    {
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

void updatePostTouch()
{
    if (isPostTouch && now - postTouchStart > 1000)
    {
        isPostTouch = 0;
        targetEyeH = EYE_H;
        targetMouthSize = 9.0;
        mouth_shape = 0;
        lastBlink_time = now;
    }
}

void setup()
{
    Serial.begin(115200);
    Wire.begin(21, 22);

    if (!display.begin(SSD1306_SWITCHCAPVCC, OLR))
    {
        Serial.println("OLED failed");
        for (;;)
            ;
    }
}

void loop()
{
    now=millis();

    touchInput();
    updateBlink();
    updateSquint();
    updatePostTouch();

    if (singleTouch){
        SingleTapAction();
        singleTouch = 0;
    }

    if (doubleTouch){ 
        doubleTapAction(); 
        doubleTouch = 0; }

    if (isLongTouch && !isBeingPetted){
        LongPressAction();
    }
//----------------Tweening----------------
    currentEyeH = moveTowards(currentEyeH, targetEyeH, 4.5);
    currentMouthSize = moveTowards(currentMouthSize, targetMouthSize, 1.5);

    display.clearDisplay();
    drawEyes();
    drawMouth();
    display.display();
    
    delay(20);
}