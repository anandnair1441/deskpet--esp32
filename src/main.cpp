#include <Arduino.h>
#include <Wire.h>
#include "weather.h"
#include <WiFi.h>
#include <time.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <HTTPClient.h>
#include <Arduino_JSON.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLR 0x3C

//#define WIFI_SSID "Wokwi-GUEST"
//#define WIFI_PASS "" 
#define WIFI_SSID "Anandarnair"
#define WIFI_PASS "anandnair12" 
#define GMT_OFFSET   19800
#define DAYLIGHT_OFF 0

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

//-----------------------Input State Machine-----------------------
#define TOUCH_PIN 4
#define DOUBLE_TAP_DELAY 450 // Max time b/w taps for double-click
#define LONG_PRESS_TIME 600  // Time to hold before triggering Long Press
#define PET_THRESHOLD 2000

int currentMode = 0;

enum FaceState{
    STATE_NORMAL,
    STATE_SQUINTING,
    STATE_PETTING,
    STATE_POSTPET,
    STATE_DIZZY
};
FaceState currentState = STATE_NORMAL;


//-----------------------Face geometry-----------------------
#define BASE_EYE_W 30
#define EYE_H 35
#define EYE_Y 5
#define EYE_X_L 20
#define EYE_X_R 78
#define EYE_RADIUS 8
#define MOUTH_Y 44

#define SQUINT_DURA 1250
#define DIZZY_CALM_TIME 8000
#define PET_RESET_TIME    15000

#define YAWN_SPEED 0.04f

#define RAPID_TAP_WINDOW 3000
#define RAPID_TAP_THRESHOLD 7

#define NTP_RETRY_INTERVAL 30000

bool ntpSynced = false;
unsigned long lastNtpAttempt = 0;

unsigned long now;

unsigned long touchStartTime = 0;
unsigned long lastTapTime = 0;
bool isTouching = false;
int touchCount = 0;
bool singleTouch = false;
bool doubleTouch = false;
bool isLongTouch = false;
unsigned long postTouchStart = 0;
int rapidTapCount = 0;
unsigned long rapidTapWindowStart = 0;

unsigned long lastInteractionTime = 0;
unsigned long lastBlink_time = 0;
long blinkInterval = 3500;
long blinkDuration = 150;
unsigned long squintStartTime = 0;
unsigned long sleepStartTime = 0;

struct EyeState {
    float h = EYE_H;
    float targetH = EYE_H;
    float w = BASE_EYE_W;
    float targetW = BASE_EYE_W;
    float OffsetX = 0;    
    float targetOffsetX = 0; 
    float OffsetY = 0;
    float targetOffsetY = 0;
};
EyeState leftEye, rightEye; 

enum MouthShape {
    MOUTH_NORMAL,
    MOUTH_WMOUTH,
};
MouthShape mouth_shape = MOUTH_NORMAL;

float currentMouthSize = 9.0;
float targetMouthSize = 9.0;

int petCount = 0;
unsigned long dizzyStart = 0;
unsigned long lastPetTime = 0;
unsigned long dizzyEndTime = 0;
bool dizzyFromPetting = false;

float mouthOffsetX = 0;
float targetMouthOffsetX = 0;
unsigned long mouthDelayStart = 0;  
bool mouthFollowing = false;         

unsigned long nextLookTime = 0;
bool centerPauseActive = false;
int lastLookDir = 0;

int idlePhase = 0;
// 0=none, 1=yawn1, 2=yawn2, 3=yawn3, 4=sleeping
float currentYawnFactor = 0.0f;
float targetYawnFactor  = 0.0f;
unsigned long yawnEndTime = 0;
bool isYawnEye = false;
unsigned long yawnMouthBlankUntil = 0;
float tiredeyes = 0.0f;
float targetTiredEyes = 0.0f;
bool isSleeping = false;
bool startDone = false;
unsigned long StartupStart = 0;
bool wakeFromSleep = false;

void onTouchStart();
void onLongRelease();
void updateDizzy();
void lookAround();
void setEyeTargetH(float h);
void setEyeTargetW(float w);
void triggerYawn();


//-----------------------ANIMATION-----------------------

//-----------------------Tweening------------------------
float moveTowards(float current, float target, float speed){
    if(abs(current - target) <= speed)
        return target;

    if(current < target)
        return current + speed;
    else
        return current - speed;
return target;
}


float smoothMove(float current, float target){
    if(abs(target - current) < 1.5f)
        return target;
    return (current + target) / 2.0f;
}

//------------------Squint Styles------------------
enum SquintStyle
{
    SQUINT_FLAT,
    SQUINT_CRESCENT,
    SQUINT_HAPPYCHEEKS
};

SquintStyle currentSquintStyle;

void setState(FaceState State){
    currentState = State;
    switch (State){
        case STATE_NORMAL:
            setEyeTargetH(EYE_H);
            targetMouthSize = 9.0;
            mouth_shape = MOUTH_NORMAL;
            lastBlink_time = now;
            blinkInterval = 3500;
            blinkDuration = 150;
            break;

        case STATE_SQUINTING:
           setEyeTargetH(12.0);
            break;
        
        case STATE_PETTING:
            currentSquintStyle=SQUINT_HAPPYCHEEKS;
            targetMouthSize = 0;
            break;
        
        case STATE_POSTPET:
            postTouchStart = now;
            mouth_shape = MOUTH_WMOUTH;
            setEyeTargetH(EYE_H + 5);
            
            if(petCount <= 1)
                targetMouthSize = 9.0;
            else if(petCount == 2)
                targetMouthSize = 7.0;
            else
                targetMouthSize = 5.0;
            break;

        case STATE_DIZZY:
            dizzyStart = now;
            mouth_shape = MOUTH_NORMAL;
            break;
    }
}


void setEyeTargetH(float h){
    leftEye.targetH = h;
    rightEye.targetH = h;
}

void setEyeTargetW(float w){
    leftEye.targetW = w;
    rightEye.targetW = w;
}

void setEyeOffsets(float ox, float oy){
    leftEye.targetOffsetX  = ox; rightEye.targetOffsetX = ox;
    leftEye.targetOffsetY  = oy; rightEye.targetOffsetY = oy;
}



//-----------------------INPUT-----------------------

void touchInput(){
    bool touch = digitalRead(TOUCH_PIN) == HIGH;
    // Rise
    if(touch && !isTouching){
        isTouching = 1;
        touchStartTime = now;
        onTouchStart();
        if(startDone && currentState != STATE_DIZZY && currentState != STATE_POSTPET)
            setEyeTargetH(EYE_H); 
    }

    // Hold
    if(touch && isTouching){
        if(!isLongTouch && (now - touchStartTime > LONG_PRESS_TIME)){
            isLongTouch = 1;
            touchCount = 0;
        }
    }

    // Fall
    if(!touch && isTouching){
        isTouching = 0;

        if(isLongTouch){ 
            onLongRelease();
        }
        else{
            touchCount++;
            lastTapTime = now;
        }
    }

    // Timeout Check
    if(!touch && !isLongTouch && touchCount > 0){
        if(now - lastTapTime > DOUBLE_TAP_DELAY){
            if(touchCount == 1)
                singleTouch = 1;
            else if(touchCount >= 2)
                doubleTouch = 1;
            touchCount = 0;
        }
    }
}


void onTouchStart(){
    lastInteractionTime = now;
    singleTouch = 0;
    doubleTouch = 0;
    setEyeOffsets(0,0);
    currentYawnFactor = 0.0f;
    targetYawnFactor  = 0.0f;
    yawnEndTime = 0;
    isYawnEye = false;
    targetMouthOffsetX = 0;
    mouthFollowing = false;
    centerPauseActive = false;
    yawnMouthBlankUntil = 0;
    mouth_shape = MOUTH_NORMAL;

    if(isSleeping){
        currentMode = 0;
        isSleeping = false;
        sleepStartTime = 0;
        idlePhase = 0;
        tiredeyes = 0.0f;
        dizzyEndTime = 0; 
        rapidTapCount = 0;
        rapidTapWindowStart = now;
        nextLookTime = now + 5000;
        targetTiredEyes = 0.0f;
        currentMouthSize = 2.0f;
        targetMouthSize = 9.0f;
        currentState = STATE_NORMAL;
        mouth_shape      = MOUTH_NORMAL;
        StartupStart     = now;
        wakeFromSleep    = true;
        startDone        = false;
        return;

    }else if(idlePhase >= 3) {
        idlePhase = 0;
        tiredeyes = 0.0f;
        targetTiredEyes = 0.0f;
        isSleeping = false;
        sleepStartTime = 0;
        currentMouthSize = 9.0;
        targetMouthSize = 9.0;
        setState(STATE_NORMAL);
    }
    
    if(now - rapidTapWindowStart > RAPID_TAP_WINDOW){
        rapidTapCount = 1;
        rapidTapWindowStart = now;
    }else{
        rapidTapCount++;
    }
    if(rapidTapCount >= RAPID_TAP_THRESHOLD){
        rapidTapCount = 0;
        dizzyFromPetting = false;
        setState(STATE_DIZZY);
        return;
    }

    if(currentState == STATE_DIZZY){
        dizzyStart = now;   
        return;              
    }

    if(currentState == STATE_POSTPET){
        setState(STATE_SQUINTING);
        squintStartTime = now;
        currentSquintStyle = SQUINT_CRESCENT;
    }

    if(dizzyEndTime != 0){
    dizzyEndTime = 0;
    targetMouthSize = 9.0;
    }
}

void onLongRelease(){
    if(!startDone) return;
    isLongTouch = 0;
    if(currentState == STATE_DIZZY){ 
        dizzyStart = now;            
        return;
    }
    bool fullPet = (now - touchStartTime) > PET_THRESHOLD;

    if(fullPet){
        petCount++;
        lastPetTime = now;
        if(petCount >= 5){
            dizzyFromPetting = true;
            setState(STATE_DIZZY);
        }else 
            setState(STATE_POSTPET);
    }else{
        setState(STATE_NORMAL);
    }
}


void triggerModeChange() {
    currentMode++;
    if (currentMode > 3) currentMode = 0;
    //0=pet,1=clocl,2=weatther,3=forcast
}

void tweenEye(EyeState &eye){
    float prevH = eye.h;
    eye.h       = smoothMove(eye.h,       eye.targetH);
    eye.w       = smoothMove(eye.w,       eye.targetW);
    eye.OffsetX = smoothMove(eye.OffsetX,  eye.targetOffsetX);
    eye.OffsetY = smoothMove(eye.OffsetY,  eye.targetOffsetY);
    if(eye.targetH <= 4.0f)
        eye.OffsetY += (prevH - eye.h) / 2.0f;
}


void drawCrescentEye(int centerX, float eyeH){
    int centerY, radius, thickness, blackR;

    if(isYawnEye){
        centerY   = 30;       // EYE_Y + EYE_H/2 + 5
        radius    = 21;       // large = flatter arc
        thickness = 3;
        blackR    = 23;       // radius + 2
    } else {
        centerY   = EYE_Y + EYE_H / 2;
        float t   = min (eyeH, (float)EYE_H) / (float)EYE_H;
        radius    = 10 + (int)(t * 4);
        thickness = 3  + (int)(t * 5);
        blackR    = radius - 1;
    }

    // draw white circle twice — slightly offset vertically to thicken the arc
    display.fillCircle(centerX, centerY - 1, radius, SSD1306_WHITE);
    display.fillCircle(centerX, centerY,     radius, SSD1306_WHITE);

    // black cover — bottom half
    display.fillRect(centerX - radius - 1, centerY, (radius + 1) * 2, radius + 2, SSD1306_BLACK);

    // black inner trim — draw twice to soften inner edge
    display.fillCircle(centerX, centerY + thickness,     blackR, SSD1306_BLACK);
    display.fillCircle(centerX, centerY + thickness + 1, blackR, SSD1306_BLACK);
}

void drawPostPettingEyes(){
    int outerY = 26;
    int innerY = 30;
    int botY   = 42;
    display.fillRoundRect(EYE_X_L, EYE_Y, BASE_EYE_W, EYE_H, EYE_RADIUS, SSD1306_WHITE);
    display.fillRoundRect(EYE_X_R, EYE_Y, BASE_EYE_W, EYE_H, EYE_RADIUS, SSD1306_WHITE);
    // left eye — fill band then restore outer white corner
    display.fillRect(EYE_X_L, outerY, BASE_EYE_W, botY - outerY, SSD1306_BLACK);
    display.fillTriangle(EYE_X_L, outerY, EYE_X_L + BASE_EYE_W, outerY, EYE_X_L, innerY, SSD1306_WHITE);
    // right eye — mirror
    display.fillRect(EYE_X_R, outerY, BASE_EYE_W, botY - outerY, SSD1306_BLACK);
    display.fillTriangle(EYE_X_R + BASE_EYE_W, outerY, EYE_X_R, outerY, EYE_X_R + BASE_EYE_W, innerY, SSD1306_WHITE);

}

void drawWavyLineMouth(){
    int cx = 64;
    int cy = MOUTH_Y + 5;
    for(int x = cx - 17; x <= cx + 17; x++){
        float wave = sin((x - cx) * 0.8) * 3;
        int y = cy + (int)wave;
        display.drawPixel(x, y,   SSD1306_WHITE);
        display.drawPixel(x, y+1, SSD1306_WHITE);
    }
}

void drawSpiralEye(int cx, int cy, int direction, float rotationOffset){
    float angle = 0;
    float radius = 0;
    while(radius <15){
        float ea = (angle + rotationOffset) * direction;
        int x = cx + (int)(cos(ea) * radius);
        int y = cy + (int)(sin(ea) * radius);
        display.drawPixel(x,   y, SSD1306_WHITE);
        display.drawPixel(x+1, y, SSD1306_WHITE);
        angle  += 0.3;
        radius += 0.22;
    }
}

void drawHappyCheeks(){
    int lCX = EYE_X_L + BASE_EYE_W / 2;
    int rCX = EYE_X_R + BASE_EYE_W / 2;
    display.fillRoundRect(EYE_X_L, EYE_Y, BASE_EYE_W, EYE_H, EYE_RADIUS, SSD1306_WHITE);
    display.fillRoundRect(EYE_X_R, EYE_Y, BASE_EYE_W, EYE_H, EYE_RADIUS, SSD1306_WHITE);
    float cr = 25.0f + sin(now * 0.003f) * 1.5f;
    display.fillCircle(lCX, 46, (int)cr, SSD1306_BLACK);
    display.fillCircle(rCX, 46, (int)cr, SSD1306_BLACK);
    return;
}

void SingleTapAction(){
    setState(STATE_SQUINTING);
    squintStartTime = now;
    // Randomly choose eye style
    int r = random(0, 3);
    if(r==0)
        currentSquintStyle = SQUINT_FLAT;
    else if(r==1)
        currentSquintStyle = SQUINT_CRESCENT;
    else
        currentSquintStyle = SQUINT_HAPPYCHEEKS;

    // 40% chance of bigger lower smile
    if(random(0, 100) < 40){
        targetMouthSize = 13; // Wider smile
    }
    else{
        targetMouthSize = 9; // Normal smile
    }
}

void doubleTapAction(){
    if(!startDone) return;

    if(currentMode == 3){
        triggerModeChange();
        isSleeping = false;
        sleepStartTime = 0;
        lastInteractionTime = now;

        if(wakeFromSleep || idlePhase >= 3){
            // wake animation
            idlePhase = 0;
            tiredeyes = 0.0f;
            targetTiredEyes = 0.0f;
            dizzyEndTime = 0;
            rapidTapCount = 0;
            rapidTapWindowStart = now;
            nextLookTime = now + 5000;
            currentMouthSize = 2.0f;
            targetMouthSize = 9.0f;
            currentState = STATE_NORMAL;
            mouth_shape = MOUTH_NORMAL;
            StartupStart = now;
            wakeFromSleep = true;
            startDone = false;
        } else {
            idlePhase = 0;
            tiredeyes = 0.0f;
            targetTiredEyes = 0.0f;
            setState(STATE_NORMAL);
        }
        return;
    }
    //cycle modes
    triggerModeChange();

    if(currentMode == 2) scrollX = 0;
    if(currentMode == 1){ idlePhase = 0; wakeFromSleep = false; }
}

void LongPressAction()
{
    setState(STATE_PETTING);
    currentSquintStyle = SQUINT_HAPPYCHEEKS;
}

void updateSquint(){
    if(currentState != STATE_SQUINTING) return;

    if(now- squintStartTime > SQUINT_DURA){
        setState(STATE_NORMAL);
    } 
}


void drawEyes(){
    int leftCX = EYE_X_L + BASE_EYE_W / 2;
    int rightCX = EYE_X_R + BASE_EYE_W / 2;
    
    switch(currentState){
        case STATE_POSTPET:{
            if(petCount <= 1){
                drawCrescentEye(leftCX,leftEye.h);
                drawCrescentEye(rightCX,rightEye.h);
            }
            else{
                drawPostPettingEyes();
                }
            return;
        }

        case STATE_PETTING:{
            drawHappyCheeks();
            return;
        }

        case STATE_SQUINTING:{
            if(currentSquintStyle == SQUINT_CRESCENT){
                drawCrescentEye(leftCX,leftEye.h);
                drawCrescentEye(rightCX,rightEye.h);
                return;
            }if(currentSquintStyle == SQUINT_HAPPYCHEEKS){
                drawHappyCheeks();
                return;
            }
            // flat squint
            {
            int h  = (int)rightEye.h;
            int ly = EYE_Y + (EYE_H - h) / 2;
            display.fillRoundRect(EYE_X_L, ly, BASE_EYE_W, h, 4, SSD1306_WHITE);
            display.fillRoundRect(EYE_X_R, ly, BASE_EYE_W, h, 4, SSD1306_WHITE);
            }
            return;
        }

        case STATE_DIZZY:{
            float rot = (float)(now / 75.0f) * 0.18f;
            int cy = EYE_Y + EYE_H / 2;
            drawSpiralEye(leftCX, cy,  1, rot);
            drawSpiralEye(rightCX,  cy, -1, rot);
            return;
        }
        

        default:{
            if(currentYawnFactor > 0.01f){
                drawCrescentEye(leftCX,  0);
                drawCrescentEye(rightCX, 0);
                return;
            }
            int h = (int)rightEye.h;
            if(h < 2) h = 2;
            int radius = (h <= 4) ? 2 : min(EYE_RADIUS, h / 2);
            int ly     = EYE_Y + (EYE_H - h) / 2;
            int hoodH = (int)(tiredeyes * EYE_H);  // max droop

            if(idlePhase >= 3){
                // Draw full eye rect at fixed position — never changes size
                display.fillRoundRect(EYE_X_L, EYE_Y, BASE_EYE_W, EYE_H, EYE_RADIUS, SSD1306_WHITE);
                display.fillRoundRect(EYE_X_R, EYE_Y, BASE_EYE_W, EYE_H, EYE_RADIUS, SSD1306_WHITE);
                
                // Paint hood down from top in black — covers the eye from above
                display.fillRect(EYE_X_L, EYE_Y, BASE_EYE_W, hoodH, SSD1306_BLACK);
                display.fillRect(EYE_X_R, EYE_Y, BASE_EYE_W, hoodH, SSD1306_BLACK);
                return;
            }

            display.fillRoundRect(EYE_X_L + (int)leftEye.OffsetX, ly + (int)leftEye.OffsetY, 
            BASE_EYE_W, h, radius, SSD1306_WHITE);
            display.fillRoundRect(EYE_X_R + (int)rightEye.OffsetX, ly + (int)rightEye.OffsetY, 
            BASE_EYE_W, h, radius, SSD1306_WHITE);
            
            return;
        } 
    }
}


void drawMouth(){ 

    if(mouth_shape == MOUTH_WMOUTH){ // w mouth
        int cx = 64;
        int cy = MOUTH_Y + 4;

        display.drawLine(cx - 10, cy, cx - 5, cy + 5, SSD1306_WHITE);
        display.drawLine(cx - 5, cy + 5, cx, cy + 2, SSD1306_WHITE);
        display.drawLine(cx, cy + 2, cx + 5, cy + 5, SSD1306_WHITE);
        display.drawLine(cx + 5, cy + 5, cx + 10, cy, SSD1306_WHITE);
        return;
    }

    if(currentState == STATE_DIZZY){
        drawWavyLineMouth();
        return;
    }
    if(currentYawnFactor > 0.05f){
        int r = (int)(currentYawnFactor * 16);
        if(r > 1){
            int centerY = MOUTH_Y + 14 - r;
            display.fillCircle(64, centerY, r, SSD1306_WHITE);
        }
        return;
    }  

    if(idlePhase >= 3){
        if(yawnMouthBlankUntil != 0 && now < yawnMouthBlankUntil)
            return;
        if(yawnMouthBlankUntil != 0)
            yawnMouthBlankUntil = 0;

        if(idlePhase >= 4){
           if(isSleeping){
                float breathe = (sin(now * 0.0015f) + 1.0f) * 0.5f;  
                int dotR = 1 + (int)(breathe * 2.0f);               
                display.fillCircle(64, MOUTH_Y + 4, dotR, SSD1306_WHITE);
                return;
            }
            // eyes still closing, show flat
            display.drawFastHLine(58, MOUTH_Y + 4, 12, SSD1306_WHITE);
            display.drawFastHLine(58, MOUTH_Y + 5, 12, SSD1306_WHITE);
            return;
        }
        display.drawFastHLine(58, MOUTH_Y + 4, 12, SSD1306_WHITE);
        display.drawFastHLine(58, MOUTH_Y + 5, 12, SSD1306_WHITE);
        return;
    }

    if(yawnMouthBlankUntil != 0){
        if(now < yawnMouthBlankUntil) return;
        yawnMouthBlankUntil = 0;
        targetMouthSize = 9.0;
        currentMouthSize = 9.0;
    }

    int s = (int)currentMouthSize;
    if(s < 1) return;

    int mx = 64 + (int)mouthOffsetX;

    display.fillCircle(mx, MOUTH_Y + 5, s, SSD1306_WHITE);
    display.fillCircle(mx, MOUTH_Y + 1, s, SSD1306_BLACK);
}

void updateBlink(){
    if(currentState != STATE_NORMAL || currentYawnFactor > 0.05f 
        || now - lastInteractionTime < 2000 || idlePhase >= 4) 
        return;


    static int isBlinking = 0;
    static unsigned long Blinkstart_time = 0;

    if(!isBlinking && now - lastBlink_time >blinkInterval){
        isBlinking = 1;
        Blinkstart_time = now;

        leftEye.h = 2; rightEye.h = 2;
        setEyeTargetH(2);
    }

    if(isBlinking && now - Blinkstart_time >blinkDuration){
        isBlinking = 0;
        lastBlink_time = now; 
        
        if(idlePhase >= 3){
            float restH = EYE_H * (1.0f - tiredeyes * 0.6f);
            restH = max(8.0f, restH);
            setEyeTargetH(restH);
            leftEye.h = restH;
            rightEye.h = restH;
            blinkInterval = random(8000, 12000);
            blinkDuration = random(300, 500);
        }else {
            setEyeTargetH(EYE_H);
            if(random(0,100) < 10)blinkInterval = random(200,400);
            else{
               blinkInterval = random(3500,7000);blinkDuration = random(100,200); 
            }
        }
    }
}
// single tap  → quick press and release under 600ms, no second tap follows
//  double tap  → two quick taps within 400ms of each other
//  long press  → held over 600ms
//  release     → finger lifted after long press

void updatePostTouch(){
    if(currentState != STATE_POSTPET) return;
    unsigned long postDur = (petCount == 1)?1200:(petCount == 2)?1000:500;
    if(now - postTouchStart >= postDur){
    setState(STATE_NORMAL);
    }
}

void updateDizzy(){
    if(currentState != STATE_DIZZY) return;
    unsigned long dur = dizzyFromPetting ? DIZZY_CALM_TIME : 3000;
    if(now - dizzyStart >= dur){
        if(dizzyFromPetting){
            petCount = 0;
            dizzyEndTime = now;
            targetMouthSize = 0;
        }
        dizzyFromPetting = false;
        setState(STATE_NORMAL);
    }
}

void updatePetReset(){
    if(currentState == STATE_DIZZY) return;
    if(petCount == 0) return;
    if(now - lastPetTime >= PET_RESET_TIME){
        petCount = 0;
    }
}


void drawCalmBar(){
    if(currentState != STATE_DIZZY) return;
    if(!dizzyFromPetting) return;
    float progress = (float)(now - dizzyStart) / DIZZY_CALM_TIME;
    if(progress > 1.0) progress = 1.0;
    int barH = (int)(progress * 60);
    display.fillRect(125, 62 - barH, 1, barH, SSD1306_WHITE);
}

//-------------------lookaround------------------
//    0-34  (35%) → center
//   35-54 (20%) → left only
//   55-74 (20%) → right only
//   75-84 (10%) → left + up
//   85-94 (10%) → right + up
//   95-99  (5%) → straight up

void lookAround(){
    if(now - lastInteractionTime < 5000) return;
    if(abs(leftEye.OffsetX - leftEye.targetOffsetX) > 2.0f) return;
    int roll = random(0, 100);
    mouthFollowing = false;
    centerPauseActive = true;

    int Dir = 0;
    float X = 0, Y = 0;

    if(roll < 35){   //center
        Dir = 0; X = 0; Y = 0;
        nextLookTime = now + random(1500, 3000);
    
    }else if(roll < 55){   //left
        Dir = -1;
        X = random(-10, -4); Y = 0;
        mouthFollowing = true;
        nextLookTime = now + random(600, 1200);
    
    }else if(roll < 75){   //right
        Dir = 1;
        X = random(4, 10); Y = 0;
        mouthFollowing = true;
        nextLookTime = now + random(600, 1200);
    
    }else if(roll < 85){  //leftup
        Dir = -1;
        X = random(-10, -4); Y = random(-2, 0);
        mouthFollowing = true;
        nextLookTime = now + random(2000, 4000);
    
    }else if(roll < 95){    //rightup
        Dir = 1;
        X = random(4, 10); Y = random(-2, 0);
        mouthFollowing = true;
        nextLookTime = now + random(2000, 4000);
    
    }else {   //centerup
        Dir = 0; X = 0; Y = random(-4, -1);
        nextLookTime = now + random(300, 600);
    }
    // same direction
    if(Dir == lastLookDir && Dir != 0){
        centerPauseActive = false;
        leftEye.h  = max(2.0f, leftEye.h  - 6.0f);
        rightEye.h = max(2.0f, rightEye.h - 6.0f);
    } else {
        setEyeOffsets(X, Y);
        // saccade snap
        if(Dir != 0){
            leftEye.OffsetX  = X * 0.6f;
            rightEye.OffsetX = X * 0.6f;
        }
        leftEye.h  = max(2.0f, leftEye.h  - 6.0f);
        rightEye.h = max(2.0f, rightEye.h - 6.0f);
    }

    lastLookDir = Dir;

    if(mouthFollowing)
        mouthDelayStart = now;
}

void updateLook(){
    if(currentYawnFactor > 0.05f) return;
    if(idlePhase >= 3) return;

    if(now >= nextLookTime && currentState == STATE_NORMAL && now - lastInteractionTime > 5000){
        lookAround();
        return;
    }
    if(currentState != STATE_NORMAL) return;

    if(mouthFollowing && now - mouthDelayStart >= 100){
        targetMouthOffsetX = rightEye.targetOffsetX;
        mouthFollowing = false;
    }
    if(rightEye.targetOffsetX == 0 && abs(rightEye.OffsetX) < 1.5f)
        targetMouthOffsetX = 0;

    if(centerPauseActive && now >= nextLookTime)
        centerPauseActive = false;
}

void drawClock(){
    struct tm timeinfo;

     if(!getLocalTime(&timeinfo, 100)){
        display.setTextSize(3);
        display.setCursor(10, 20);
        display.print("--:--");
        return;
    }
    char timeBuf[6];
    strftime(timeBuf, sizeof(timeBuf), "%I:%M", &timeinfo);

    char dateBuf[16];
    strftime(dateBuf, sizeof(dateBuf), "%a, %d %b", &timeinfo);

    char ampmBuf[4];
    strftime(ampmBuf, sizeof(ampmBuf), "%p", &timeinfo);

    char secBuf[3];
    strftime(secBuf, sizeof(secBuf), "%S", &timeinfo);

    display.setTextSize(1);
    display.setCursor(31, 4);
    display.print(dateBuf);

    display.setTextSize(3);
    display.setCursor(10, 20);
    display.print(timeBuf);

    display.setTextSize(1);
    display.setCursor(105, 20);
    display.print(ampmBuf);

    display.setTextSize(1);
    display.setCursor(105, 35);
    display.print(secBuf);
}

void updateIdle(){
    if(!startDone) return;
    if(currentState != STATE_NORMAL) return;
    unsigned long elapsed = now - lastInteractionTime;

    if(elapsed < 3000){ 
        idlePhase = 0; 
        return; 
    }
    if(idlePhase == 0 && elapsed >= 30000){
        idlePhase = 1;
        triggerYawn();
    }
    else if(idlePhase == 1 && elapsed >= 45000){
        idlePhase = 2;
        setEyeOffsets(0, 0);
        targetMouthSize = 0;
        triggerYawn();
    }
    else if(idlePhase == 2 && elapsed >= 60000){
        idlePhase = 3;
        setEyeOffsets(0, 0);
        setEyeTargetH(EYE_H);
        targetMouthSize = 0;
        triggerYawn();
    }
    else if(idlePhase == 3 && elapsed >= 75000){
        idlePhase = 4;
        isSleeping = false;
        targetTiredEyes = 1.0f;
    }

    if(idlePhase == 3){
        targetTiredEyes = min(0.33f, (float)(elapsed - 60000) / 15000.0f);
    }
    else if(idlePhase == 4 && !isSleeping){
        targetTiredEyes = min(1.0f, 0.33f + (float)(elapsed - 75000) / 15000.0f);
        if(tiredeyes >= 0.99f){
        sleepStartTime = now;
        isSleeping = true;
        leftEye.h = 2.0f;  rightEye.h = 2.0f;
        leftEye.targetH = 2.0f; rightEye.targetH = 2.0f;
        }
    }
    if(isSleeping && sleepStartTime != 0 && now - sleepStartTime >= 20000){
    currentMode = 1;
    sleepStartTime = 0;
    wakeFromSleep = true;
    }
}

void triggerYawn(){
    if(currentState != STATE_NORMAL) return;
    if(currentYawnFactor >0.1f) return;
    isYawnEye = true;
    targetYawnFactor =1.0f;
    yawnEndTime=now + 3000;
    setEyeOffsets(0, 0);
}

float easeOut(float t,int exp){
    return 1 - pow(1 - t, exp);
}
float easeInOut(float t){
    if (t < 0.5) return 2 * t * t;
    else         return 1 - pow(-2*t + 2, 2) / 2;
} 

void runStartup(){
    display.clearDisplay();
    unsigned long elapsed = now - StartupStart;
    if(wakeFromSleep) elapsed += 2700;
    float t, h, y; 
    int r, mouthR, hoodH;
    if(elapsed <= 800){   
        mouthR = 2;                                                             //1
        display.fillRect(EYE_X_L, EYE_Y + EYE_H - 4, BASE_EYE_W, 2, SSD1306_WHITE);
        display.fillRect(EYE_X_R, EYE_Y + EYE_H - 4, BASE_EYE_W, 2, SSD1306_WHITE);
        display.fillCircle(64, MOUTH_Y + 4, mouthR, SSD1306_WHITE);
        return;
    }else if(elapsed <= 1400){                                                  //2
        t = (elapsed-800)/600.0f;
        h = 2 + easeOut(t, 4) * 4;        // 2->6px
        y = EYE_Y + EYE_H - h;           // bottom pin
        r = (h<=4)?2:min((float)EYE_RADIUS, h/2.0f);           
    }else if(elapsed <= 1900){                                                  //3
        h = 6;
        y = 34;
        r=2;
    }else if(elapsed <= 2500){                                                  //4
        t = (elapsed-1900)/600.0f;
        h = 6- easeInOut(t)*4; //6->2
        y = 40-h;
        r=2;
    }else if(elapsed <= 2700 ){   
        mouthR = 2;                                                             //5
        display.fillRect(EYE_X_L, EYE_Y+EYE_H-4, BASE_EYE_W, 2, SSD1306_WHITE);
        display.fillRect(EYE_X_R, EYE_Y+EYE_H-4, BASE_EYE_W, 2, SSD1306_WHITE);
        display.fillCircle(64, MOUTH_Y+4, mouthR, SSD1306_WHITE);
        return;
    }else if(elapsed <= 3500){                                                  //6
        t = (elapsed-2700)/900.0f;
        hoodH = EYE_H - (int)(easeOut(t, 3)*21);
       
        display.fillRoundRect(EYE_X_L, EYE_Y, BASE_EYE_W, EYE_H, EYE_RADIUS, SSD1306_WHITE);
        display.fillRoundRect(EYE_X_R, EYE_Y, BASE_EYE_W, EYE_H, EYE_RADIUS, SSD1306_WHITE);
        display.fillRect(EYE_X_L, EYE_Y, BASE_EYE_W, hoodH, SSD1306_BLACK);
        display.fillRect(EYE_X_R, EYE_Y, BASE_EYE_W, hoodH, SSD1306_BLACK);
        display.fillCircle(64, MOUTH_Y + 4, 2, SSD1306_WHITE);
        return;
        
    }else if(elapsed <= 3900){                                                  //7                
        display.fillRoundRect(EYE_X_L, EYE_Y, BASE_EYE_W, EYE_H, EYE_RADIUS, SSD1306_WHITE);
        display.fillRoundRect(EYE_X_R, EYE_Y, BASE_EYE_W, EYE_H, EYE_RADIUS, SSD1306_WHITE);
        display.fillRect(EYE_X_L, EYE_Y, BASE_EYE_W, 14, SSD1306_BLACK);
        display.fillRect(EYE_X_R, EYE_Y, BASE_EYE_W, 14, SSD1306_BLACK);
        display.fillCircle(64, MOUTH_Y + 4, 2, SSD1306_WHITE);
        return;
        
    }else if(elapsed <= 4700){                                                  //8
        t = (elapsed - 3900) / 800.0f;
        hoodH = 14 - (int)(easeOut(t, 3)*14);

        display.fillRoundRect(EYE_X_L, EYE_Y, BASE_EYE_W, EYE_H, EYE_RADIUS, SSD1306_WHITE);
        display.fillRoundRect(EYE_X_R, EYE_Y, BASE_EYE_W, EYE_H, EYE_RADIUS, SSD1306_WHITE);
        display.fillRect(EYE_X_L, EYE_Y, BASE_EYE_W, hoodH, SSD1306_BLACK);
        display.fillRect(EYE_X_R, EYE_Y, BASE_EYE_W, hoodH, SSD1306_BLACK);
        display.fillCircle(64, MOUTH_Y + 4, 2, SSD1306_WHITE);
        return;
        
    }else{
    wakeFromSleep    = false;
    currentState     = STATE_NORMAL;
    mouth_shape      = MOUTH_NORMAL;
    currentMouthSize = 2.0f;
    targetMouthSize  = 9.0f;
    leftEye.h        = EYE_H;  rightEye.h        = EYE_H;
    leftEye.targetH  = EYE_H;  rightEye.targetH  = EYE_H;
    lastBlink_time   = now;
    blinkInterval    = 3500;
    blinkDuration    = 150;
    lastInteractionTime = now;
    isLongTouch  = false;
    isTouching   = false;
    touchCount   = 0;
    startDone = true;

    drawEyes();
    drawMouth();
    return;
}

    display.fillCircle(64, MOUTH_Y + 4, 2, SSD1306_WHITE);
    display.fillRoundRect(EYE_X_L, (int)y, BASE_EYE_W, (int)h, r, SSD1306_WHITE);
    display.fillRoundRect(EYE_X_R, (int)y, BASE_EYE_W, (int)h, r, SSD1306_WHITE);

}

void weatherForcastTask(void* param){
    while(WiFi.status() != WL_CONNECTED){
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    for(;;){
        fetchWeather();
        vTaskDelay(pdMS_TO_TICKS(600000));
    }
}



void setup(){
    Serial.begin(115200);
    Wire.begin(21, 22);
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

 

    display.setTextColor(SSD1306_WHITE);
    xTaskCreate(weatherForcastTask, "weather", 8192, NULL, 1, NULL);  

    if(!display.begin(SSD1306_SWITCHCAPVCC, OLR)){  
        Serial.println("OLED failed");
        for (;;);
    }

    
}


void loop(){
    now = millis();
    if(StartupStart == 0) StartupStart = now;

    if(!startDone){
        runStartup();
        display.display();
        delay(20);
        return;
    }
    touchInput();

    if(currentMode == 0){
        updateBlink();
        updateSquint();
        updatePostTouch();
        updateDizzy();
        updatePetReset();
        updateLook();
        updateIdle();
    }
    
        //----------------time---------------------
    if(WiFi.status() == WL_CONNECTED && !ntpSynced){
        if(lastNtpAttempt == 0 || now - lastNtpAttempt >= NTP_RETRY_INTERVAL){
            lastNtpAttempt = now;
            configTime(GMT_OFFSET, DAYLIGHT_OFF, "pool.ntp.org");
            struct tm timeinfo;
            if(getLocalTime(&timeinfo, 100)){
                ntpSynced = true;
            }
        }
    }


    if(currentMode == 0){
        if(singleTouch){
            if(currentState != STATE_DIZZY)
                SingleTapAction(); 
            singleTouch = 0;
        }
        if(isLongTouch && currentState != STATE_PETTING && currentState != STATE_DIZZY){
            LongPressAction();
        }
    }else{
        singleTouch = 0;
    }
    
    if(doubleTouch){ 
        //Serial.println("doubleTouch fired, mode=" + String(currentMode) + " startDone=" + String(startDone));
        if(currentState != STATE_DIZZY)
            doubleTapAction(); 
        doubleTouch = 0; 
        
    }
    //----------------Tweening----------------
    currentMouthSize = moveTowards(currentMouthSize, targetMouthSize, 1.5);
    tweenEye(leftEye);
    tweenEye(rightEye);

    mouthOffsetX = smoothMove(mouthOffsetX, targetMouthOffsetX);
    

    //-------------yawning--------------------

    if(yawnEndTime != 0 && now >= yawnEndTime){
        targetYawnFactor = 0.0f;
        yawnEndTime = 0;
        yawnMouthBlankUntil = now + 3000;
        if(idlePhase < 3)
            setEyeTargetH(EYE_H);
    }

    if(currentYawnFactor <= 0.0f && isYawnEye && yawnEndTime == 0){
        isYawnEye = false;
        if(idlePhase < 3){
            setEyeTargetH(EYE_H);
        }
    }
    currentYawnFactor = moveTowards(currentYawnFactor, targetYawnFactor, YAWN_SPEED);
    tiredeyes = moveTowards(tiredeyes, targetTiredEyes, 0.005f);

    display.clearDisplay();

    if(currentMode == 1){
        drawClock();
    } else if(currentMode == 2){
        drawWeather();
    } else if(currentMode == 3){
        drawForcast();
    } else if(startDone){
        drawEyes();
        drawMouth();
        drawCalmBar();
    };

    display.display();

    delay(20);
}