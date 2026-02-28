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
#define DOUBLE_TAP_DELAY 350 // Max time b/w taps for double-click
#define LONG_PRESS_TIME 600  // Time to hold before triggering Long Press
#define PET_THRESHOLD 2000


enum FaceState {
    STATE_NORMAL,
    STATE_SQUINTING,
    STATE_PETTING,
    STATE_POSTPET,
    STATE_EXCITED
};
FaceState currentState =STATE_NORMAL;


//-----------------------Face geometry-----------------------
#define BASE_EYE_W 30
#define EYE_H 44
#define EYE_Y 5
#define EYE_X_L 16
#define EYE_X_R 82
#define EYE_RADIUS 8
#define MOUTH_Y 42

#define SQUINT_DURA 1250
#define EXCITED_CALM_TIME 8000
#define PET_RESET_TIME    15000

unsigned long now;

unsigned long touchStartTime = 0;
unsigned long lastTapTime = 0;
bool isTouching = 0;
int touchCount = 0;
int singleTouch = 0;
int doubleTouch = 0;
int isLongTouch = 0;
unsigned long postTouchStart = 0;

unsigned long lastInteractionTime = 0;
unsigned long lastBlink_time = 0;
unsigned long squintStartTime = 0;

float currentEyeH = EYE_H;
float targetEyeH = EYE_H;
float currentMouthSize = 9.0;
float targetMouthSize = 9.0;
int mouth_shape = 0;

int petCount=0;
unsigned long excitedStart = 0;
unsigned long lastPetTime = 0;

void onTouchStart();
void onLongRelease();


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


void setState(FaceState State){
    currentState=State;
    switch (State){
        case STATE_NORMAL:
            targetEyeH=EYE_H;
            targetMouthSize=9.0;
            mouth_shape=0;
            lastBlink_time=now;
            break;

        case STATE_SQUINTING:
            targetEyeH=12.0;
            break;
        
        case STATE_PETTING:
            targetEyeH=6.0;
            targetMouthSize=0;
            break;
        
        case STATE_POSTPET:
            postTouchStart=now;
            mouth_shape=1;
            if (petCount <= 1)
                targetMouthSize = 9.0;
            else if (petCount == 2)
                targetMouthSize = 7.0;
            else
                targetMouthSize = 5.0;
            break;

        case STATE_EXCITED:
            excitedStart=now;
            targetEyeH = EYE_H + 6;  
            targetMouthSize = 13.0;
            mouth_shape = 0;
            break;
    }
}


//------------------Squint Styles------------------
enum SquintStyle
{
    SQUINT_FLAT,
    SQUINT_CRESCENT
};

SquintStyle currentSquintStyle;

//-----------------------INPUT-----------------------

void touchInput(){
    bool touch = touchRead(TOUCH_PIN) < 60;

    // Rise
    if (touch && !isTouching){
        isTouching = 1;
        touchStartTime = now;
        onTouchStart();
        if(currentState != STATE_EXCITED)
            targetEyeH = EYE_H; 
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
            onLongRelease();
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


void onTouchStart(){
    lastInteractionTime = now;
    singleTouch = 0;
    doubleTouch = 0;

    if(currentState == STATE_EXCITED){
        excitedStart = now;   
        return;              
    }

    if (currentState==STATE_POSTPET){
        setState(STATE_SQUINTING);
        squintStartTime=now;
        currentSquintStyle = SQUINT_CRESCENT;
    }else{
       
    }
    mouth_shape = 0;
    targetMouthSize = 9.0;
}

void onLongRelease(){
    isLongTouch = 0;
    if(currentState == STATE_EXCITED){ 
        excitedStart = now;            
        return;
    }
    bool fullPet = (now - touchStartTime) > PET_THRESHOLD;

    if(fullPet){
        petCount++;
        lastPetTime=now;
        if(petCount>=5)
            setState(STATE_EXCITED);
        else 
            setState(STATE_POSTPET);
    }else{
        setState(STATE_NORMAL);
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
    // ----- Adjustable parameters -----
    int radiusX = 10;    // smaller = less width
    int radiusY = 20;   // larger = more height
    int centery     = EYE_Y + (EYE_H /2);
    int leftCX  = EYE_X_L + BASE_EYE_W / 2;
    int rightCX = EYE_X_R + BASE_EYE_W / 2;

    for (int y = 0; y <= radiusY; y++)
    {
        float ratio = (float)y / radiusY;

        // Ellipse equation
        int xSpan = radiusX * sqrt(1.0 - ratio * ratio);

        // Left eye
        display.drawFastHLine(
            leftCX - xSpan,
            centery - y,
            xSpan * 2,
            SSD1306_WHITE
        );

        // Right eye
        display.drawFastHLine(
            rightCX - xSpan,
            centery - y,
            xSpan * 2,
            SSD1306_WHITE
        );
    }
}

void drawWavyLineMouth(){
    int cx = 64;
    int cy = MOUTH_Y + 4;
    for(int x = cx - 17; x <= cx + 17; x++){
        float wave = sin((x - cx) * 0.55) * 3;
        int y = cy + (int)wave;
        display.drawPixel(x, y,   SSD1306_WHITE);
        display.drawPixel(x, y+1, SSD1306_WHITE);
    }
}

void drawSpiralEye(int cx, int cy, int direction, float rotationOffset){
    float angle = 0;
    float radius = 0;
    while(radius < 13){
        float ea = (angle + rotationOffset) * direction;
        int x = cx + (int)(cos(ea) * radius);
        int y = cy + (int)(sin(ea) * radius);
        display.drawPixel(x,   y, SSD1306_WHITE);
        display.drawPixel(x+1, y, SSD1306_WHITE);
        angle  += 0.4;
        radius += 0.25;
    }
}

void SingleTapAction(){
    setState(STATE_SQUINTING);
    squintStartTime=now;
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


void doubleTapAction(){
    //mode change
}
void LongPressAction()
{
    setState(STATE_PETTING);
    currentSquintStyle = SQUINT_CRESCENT;
}

void updateSquint(){
    if (currentState==STATE_SQUINTING){
        if (now- squintStartTime > SQUINT_DURA){
            setState(STATE_NORMAL);
        }
    }
}

// petting animation after and if its long enough a happy content face at the end

// only three modes changable via manually--pet normal,clk,weather

// post petting face

void drawEyes()
{
    int leftCX  = EYE_X_L + BASE_EYE_W / 2;
    int rightCX = EYE_X_R + BASE_EYE_W / 2;
    
     switch(currentState){
        case STATE_POSTPET:{
            if (mouth_shape == 1) drawPostPettingEyes();
            else { drawCrescentEye(leftCX); drawCrescentEye(rightCX); }
            return;
        }

        case STATE_PETTING:{
            drawCrescentEye(leftCX);
            drawCrescentEye(rightCX);
            return;
        }

        case STATE_SQUINTING:{
            if (currentSquintStyle == SQUINT_CRESCENT){
                drawCrescentEye(leftCX);
                drawCrescentEye(rightCX);
                return;
            }
            // flat squint falls through to default with small h
            {
                int h  = (int)currentEyeH;
                int ly = EYE_Y + (EYE_H - h) / 2;
                display.fillRoundRect(EYE_X_L, ly, BASE_EYE_W, h, 4, SSD1306_WHITE);
                display.fillRoundRect(EYE_X_R, ly, BASE_EYE_W, h, 4, SSD1306_WHITE);
            }
            return;
        }

        case STATE_EXCITED:{
            float rot = (float)(now / 75) * 0.18;
            int cy = EYE_Y + EYE_H / 2;
            drawSpiralEye(leftCX,  cy,  1, rot);
            drawSpiralEye(rightCX,  cy, -1, rot);
            return;
        }
        

        default:{
            int h      = (int)currentEyeH;
            if (h < 2) h = 2;
            int radius = (h <= 4) ? 2 : EYE_RADIUS;
            int ly     = EYE_Y + (EYE_H - h) / 2;
            display.fillRoundRect(EYE_X_L, ly, BASE_EYE_W, h, radius, SSD1306_WHITE);
            display.fillRoundRect(EYE_X_R, ly, BASE_EYE_W, h, radius, SSD1306_WHITE);
            return;
        }
   }
}


void drawMouth(){

    if (mouth_shape == 1){ // w mouth
        int cx = 64;
        int cy = MOUTH_Y + 4;

        display.drawLine(cx - 10, cy, cx - 5, cy + 5, SSD1306_WHITE);
        display.drawLine(cx - 5, cy + 5, cx, cy + 2, SSD1306_WHITE);
        display.drawLine(cx, cy + 2, cx + 5, cy + 5, SSD1306_WHITE);
        display.drawLine(cx + 5, cy + 5, cx + 10, cy, SSD1306_WHITE);
        return;
    }

    if(currentState == STATE_EXCITED){
    drawWavyLineMouth();
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
    if (currentState !=STATE_NORMAL)
        return;

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

void updatePostTouch(){
    if(currentState!=STATE_POSTPET) return;
    unsigned long postDur = (petCount==1)?2000:(petCount==2)?1000:500;
    if (now - postTouchStart >= postDur){
    setState(STATE_NORMAL);
        mouth_shape = 0;
    }
}

void updateExcited(){
    if(currentState!=STATE_EXCITED)return;

    if(now - excitedStart >= EXCITED_CALM_TIME){
        petCount = 0;
        setState(STATE_NORMAL);
    }
}

void updatePetReset(){
    if(currentState == STATE_EXCITED) return;
    if(petCount == 0) return;
    if(now - lastPetTime >= PET_RESET_TIME){
        petCount = 0;
    }
}

void updatePetting(){
    if(currentState != STATE_PETTING) return;
    currentEyeH = 6 + sin(now * 0.003) * 2;
    targetEyeH=currentEyeH;
}

void drawCalmBar(){
    if(currentState != STATE_EXCITED) return;
    float progress = (float)(now - excitedStart) / EXCITED_CALM_TIME;
    if(progress > 1.0) progress = 1.0;
    int barH = (int)(progress * 60);
    display.fillRect(125, 62 - barH, 1, barH, SSD1306_WHITE);
}

void setup()
{
    Serial.begin(115200);
    Wire.begin(21, 22);

    if (!display.begin(SSD1306_SWITCHCAPVCC, OLR)){
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
    updateExcited();   
    updatePetReset();
    updatePetting();

    if(singleTouch){
        if(currentState != STATE_EXCITED)
            SingleTapAction(); 
        singleTouch = 0;
    }
    
    if (doubleTouch){ 
        if(currentState != STATE_EXCITED)
            doubleTapAction(); 
        doubleTouch = 0; }

    if(isLongTouch && currentState!=STATE_PETTING && currentState!=STATE_EXCITED){
        LongPressAction();
    }
    
//----------------Tweening----------------
    currentEyeH = moveTowards(currentEyeH, targetEyeH, 4.5);
    currentMouthSize = moveTowards(currentMouthSize, targetMouthSize, 1.5);

    display.clearDisplay();
    drawEyes();
    drawMouth();
    drawCalmBar();
    display.display();

    delay(20);
}