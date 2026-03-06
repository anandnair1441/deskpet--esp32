#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <time.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLR 0x3C

#define WIFI_SSID    "esp32test"
#define WIFI_PASS    "anandnair12"
#define GMT_OFFSET   19800
#define DAYLIGHT_OFF 0

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

//-----------------------Input State Machine-----------------------
#define TOUCH_PIN 4
#define DOUBLE_TAP_DELAY 350 // Max time b/w taps for double-click
#define LONG_PRESS_TIME 600  // Time to hold before triggering Long Press
#define PET_THRESHOLD 2000

enum Mode{ 
    MODE_PET, 
    MODE_CLOCK 
};
Mode currentMode = MODE_PET;

enum FaceState{
    STATE_NORMAL,
    STATE_SQUINTING,
    STATE_PETTING,
    STATE_POSTPET,
    STATE_EXCITED
};
FaceState currentState = STATE_NORMAL;


//-----------------------Face geometry-----------------------
#define BASE_EYE_W 30
#define EYE_H 35
#define EYE_Y 5
#define EYE_X_L 16
#define EYE_X_R 82
#define EYE_RADIUS 8
#define MOUTH_Y 44

#define SQUINT_DURA 1250
#define EXCITED_CALM_TIME 8000
#define PET_RESET_TIME    15000

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

unsigned long lastInteractionTime = 0;
unsigned long lastBlink_time = 0;
unsigned long squintStartTime = 0;

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

float currentMouthSize = 9.0;
float targetMouthSize = 9.0;
int mouth_shape = 0;

int petCount = 0;
unsigned long excitedStart = 0;
unsigned long lastPetTime = 0;
unsigned long excitedEndTime = 0;

float mouthOffsetX = 0;
float targetMouthOffsetX = 0;
unsigned long mouthDelayStart = 0;  
bool mouthFollowing = false;         

unsigned long nextLookTime = 0;
bool centerPauseActive = false;
int lastLookDir = 0;

int idlePhase = 0;
// 0=none, 1=yawn1, 2=yawn2, 3=yawn3, 4=sleeping

void onTouchStart();
void onLongRelease();
void lookAround();
void setEyeTargetH(float h);
void setEyeTargetW(float w);


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


void setState(FaceState State){
    currentState = State;
    switch (State){
        case STATE_NORMAL:
            setEyeTargetH(EYE_H);
            targetMouthSize = 9.0;
            mouth_shape = 0;
            lastBlink_time = now;
            break;

        case STATE_SQUINTING:
           setEyeTargetH(12.0);
            break;
        
        case STATE_PETTING:
            setEyeTargetH(6.0);
            targetMouthSize = 0;
            break;
        
        case STATE_POSTPET:
            postTouchStart = now;
            mouth_shape = 1;
            setEyeTargetH(EYE_H + 5);
            
            if(petCount <= 1)
                targetMouthSize = 9.0;
            else if(petCount == 2)
                targetMouthSize = 7.0;
            else
                targetMouthSize = 5.0;
            break;

        case STATE_EXCITED:
            excitedStart = now;
            mouth_shape = 0;
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
    if(touch && !isTouching){
        isTouching = 1;
        touchStartTime = now;
        onTouchStart();
        if(currentState != STATE_EXCITED && currentState != STATE_POSTPET)
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
    
    targetMouthOffsetX = 0;
    mouthFollowing = false;
    centerPauseActive = false;
    int idlePhase = 0;

    if(currentState == STATE_EXCITED){
        excitedStart = now;   
        return;              
    }

    if(currentState == STATE_POSTPET){
        setState(STATE_SQUINTING);
        squintStartTime = now;
        currentSquintStyle = SQUINT_CRESCENT;
    }

    if(excitedEndTime != 0){
    excitedEndTime = 0;
    targetMouthSize = 9.0;
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
        lastPetTime = now;
        if(petCount >= 5)
            setState(STATE_EXCITED);
        else 
            setState(STATE_POSTPET);
    }else{
        setState(STATE_NORMAL);
    }
}


void tweenEye(EyeState &eye){
    eye.h       = moveTowards(eye.h,       eye.targetH,      5.0f);
    eye.w       = moveTowards(eye.w,       eye.targetW,      2.0f);
    eye.OffsetX = smoothMove(eye.OffsetX,  eye.targetOffsetX);
    eye.OffsetY = smoothMove(eye.OffsetY,  eye.targetOffsetY);
}


void drawCrescentEye(int centerX, float eyeH){
    int centerY = EYE_Y + EYE_H / 2;
    float t = eyeH / (float)EYE_H;
    int radius    = 10 + (int)(t * 4);
    int thickness = 2  + (int)(t * 5);

    // draw white circle twice — slightly offset vertically to thicken the arc
    display.fillCircle(centerX, centerY - 1, radius, SSD1306_WHITE);
    display.fillCircle(centerX, centerY,     radius, SSD1306_WHITE);

    // black cover — bottom half
    display.fillRect(centerX - radius - 1, centerY, 
                     (radius + 1) * 2, radius + 2, SSD1306_BLACK);

    // black inner trim — draw twice to soften inner edge
    display.fillCircle(centerX, centerY + thickness,     radius - 1, SSD1306_BLACK);
    display.fillCircle(centerX, centerY + thickness + 1, radius - 1, SSD1306_BLACK);
}

void drawPostPettingEyes()
{
    // ----- Adjustable parameters -----
    int radiusX = 10;    // less width
    int radiusY = (EYE_H/2)+2;   //  more height
    int centery     = EYE_Y + (EYE_H /2);
    int leftCX = EYE_X_L + BASE_EYE_W / 2;
    int rightCX= EYE_X_R + BASE_EYE_W / 2;

    for (int y = 0; y <= radiusY; y++){
        float ratio = (float)y / radiusY;

        // Ellipse equation
        int xSpan = radiusX * sqrt(1.0 - ratio * ratio);

        // Left eye
        display.drawFastHLine(
            leftCX- xSpan,
            centery - y,
            xSpan * 2,
            SSD1306_WHITE
        );

        // Right eye
        display.drawFastHLine(
            rightCX- xSpan,
            centery - y,
            xSpan * 2,
            SSD1306_WHITE
        );
    }
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

void SingleTapAction(){
    setState(STATE_SQUINTING);
    squintStartTime = now;
    // Randomly choose eye style
    if(random(0, 2) == 0)
        currentSquintStyle = SQUINT_FLAT;
    else
        currentSquintStyle = SQUINT_CRESCENT;

    // 40% chance of bigger lower smile
    if(random(0, 100) < 40){
        targetMouthSize = 13; // Wider smile
    }
    else{
        targetMouthSize = 9; // Normal smile
    }
}

void doubleTapAction(){
    if(currentMode == MODE_PET)
        currentMode = MODE_CLOCK;
    else
        currentMode = MODE_PET;
}

void LongPressAction()
{
    setState(STATE_PETTING);
    currentSquintStyle = SQUINT_CRESCENT;
}

void updateSquint(){
    if(currentState == STATE_SQUINTING){
        if(now- squintStartTime > SQUINT_DURA){
            setState(STATE_NORMAL);
        }
    }
}

// petting animation after and if its long enough a happy content face at the end

// only three modes changable via manually--pet normal,clk,weather

// post petting face

void drawEyes()
{
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
            drawCrescentEye(leftCX,leftEye.h);
            drawCrescentEye(rightCX,rightEye.h);
            return;
        }

        case STATE_SQUINTING:{
            if(currentSquintStyle == SQUINT_CRESCENT){
                drawCrescentEye(leftCX,leftEye.h);
                drawCrescentEye(rightCX,rightEye.h);
                return;
            }
            // flat squint falls through to default with small h
            {
                int h  = (int)rightEye.h;
                int ly = EYE_Y + (EYE_H - h) / 2;
                display.fillRoundRect(EYE_X_L, ly, BASE_EYE_W, h, 4, SSD1306_WHITE);
                display.fillRoundRect(EYE_X_R, ly, BASE_EYE_W, h, 4, SSD1306_WHITE);
            }
            return;
        }

        case STATE_EXCITED:{
            float rot = (float)(now / 75.0f) * 0.18f;
            int cy = EYE_Y + EYE_H / 2;
            drawSpiralEye(leftCX, cy,  1, rot);
            drawSpiralEye(rightCX,  cy, -1, rot);
            return;
        }
        

        default:{
            int h      = (int)rightEye.h;
            if(h < 2) h = 2;
            int radius = (h <= 4) ? 2 : EYE_RADIUS;
            int ly     = EYE_Y + (EYE_H - h) / 2;

            display.fillRoundRect(EYE_X_L + (int)leftEye.OffsetX, ly + (int)leftEye.OffsetY, 
            BASE_EYE_W, h, radius, SSD1306_WHITE);
            display.fillRoundRect(EYE_X_R + (int)rightEye.OffsetX, ly + (int)rightEye.OffsetY, 
            BASE_EYE_W, h, radius, SSD1306_WHITE);
            return;
        }
   }
}


void drawMouth(){

    if(mouth_shape == 1){ // w mouth
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
    if(s < 1)
        return;

    int mx = 64 + (int)mouthOffsetX;

    display.fillCircle(mx, MOUTH_Y + 5, s, SSD1306_WHITE);
    display.fillCircle(mx, MOUTH_Y + 1, s, SSD1306_BLACK);
}

void updateBlink(){
    if(currentState != STATE_NORMAL)
        return;

    if(now - lastInteractionTime < 2000) return;

    static long interval = 3500;
    static long duration = 150;
    static int isBlinking = 0;
    static unsigned long Blinkstart_time = 0;

    if(!isBlinking && now - lastBlink_time > interval){
        isBlinking = 1;
        Blinkstart_time = now;
        leftEye.h = 2;      rightEye.h = 2;
        setEyeTargetH(2);
    }

    if(isBlinking && now - Blinkstart_time > duration){
        isBlinking = 0;
        lastBlink_time = now; 
        setEyeTargetH(EYE_H);

        if(!centerPauseActive)
            lookAround();

        if(random(0,100) < 10){
        interval = random(200,400);
        }else{
            interval = random(3500,7000);
        }
        duration = random(100, 200);
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

void updateExcited(){
    if(currentState == STATE_EXCITED){
        if(now - excitedStart >= EXCITED_CALM_TIME){
            petCount = 0;
            excitedEndTime = now;
            setState(STATE_NORMAL);
            targetMouthSize = 0; 
        }
        return;
    }
    if(excitedEndTime == 0) return;
    if(now - excitedEndTime >= 5000){
        targetMouthSize = 9.0;
        excitedEndTime = 0;
    }
}

void updatePetReset(){
    if(currentState == STATE_EXCITED) return;
    if(petCount == 0) return;
    if(now - lastPetTime >= PET_RESET_TIME){
        petCount = 0;
    }
}

//postpet eye changing height
void updatePetting(){
    if(currentState != STATE_PETTING) return;
    float ch = 6 + sin(now * 0.003) * 2;
    leftEye.h = ch; rightEye.h = ch;
}

void drawCalmBar(){
    if(currentState != STATE_EXCITED) return;
    float progress = (float)(now - excitedStart) / EXCITED_CALM_TIME;
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
    int roll = random(0, 100);
    mouthFollowing = false;
    centerPauseActive = true;

    int Dir = 0;
    float X = 0, Y = 0;

    if(roll < 35){   //center
        Dir = 0; X = 0; Y = 0;
        nextLookTime = now + random(300, 600);
    
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
        nextLookTime = now + random(600, 1200);
    
    }else if(roll < 95){    //rightup
        Dir = 1;
        X = random(4, 10); Y = random(-2, 0);
        mouthFollowing = true;
        nextLookTime = now + random(600, 1200);
    
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

void drawclock(){
    struct tm timeinfo;

     if(!getLocalTime(&timeinfo)){
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
    if(currentState != STATE_NORMAL) return;
    
    unsigned long elapsed = now - lastInteractionTime;

    if(elapsed < 30000){ 
        idlePhase = 0; 
        return; 
    }

    if(idlePhase == 0 && elapsed >= 30000){
        idlePhase = 1;
        //triggerYawn();
    }
    if(idlePhase == 1 && elapsed >= 45000){
        idlePhase = 2;
        //triggerYawn();
    }
    if(idlePhase == 2 && elapsed >= 60000){
        idlePhase = 3;
       // triggerYawn();
        // start drooping
    }
    if(idlePhase == 3 && elapsed >= 75000){
        idlePhase = 4;
        // enter sleep
    }
}

void setup(){
    Serial.begin(115200);
    Wire.begin(21, 22);
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    display.setTextColor(SSD1306_WHITE);

    if(!display.begin(SSD1306_SWITCHCAPVCC, OLR)){
        Serial.println("OLED failed");
        for (;;);
    }

    
}


void loop(){
    now = millis();

    touchInput();

    if(currentMode == MODE_PET){
        updateBlink();
        updateSquint();
        updatePostTouch();
        updateExcited();
        updatePetReset();
        updatePetting();
        updateLook();
    }
        //----------------time---------------------
    if(WiFi.status() == WL_CONNECTED && !ntpSynced){
        if(lastNtpAttempt == 0 || now - lastNtpAttempt >= NTP_RETRY_INTERVAL){
            lastNtpAttempt = now;
            configTime(GMT_OFFSET, DAYLIGHT_OFF, "pool.ntp.org");
            struct tm timeinfo;
            if(getLocalTime(&timeinfo)){
                ntpSynced = true;
            }
        }
    }

    if(currentMode == MODE_PET){
        if(singleTouch){
            if(currentState != STATE_EXCITED)
                SingleTapAction(); 
            singleTouch = 0;
        }
        if(isLongTouch && currentState != STATE_PETTING && currentState != STATE_EXCITED){
            LongPressAction();
        }
    }else{
        singleTouch = 0;
    }
    
    if(doubleTouch){ 
            doubleTapAction(); 
            doubleTouch = 0; 
    
    }
    //----------------Tweening----------------
    currentMouthSize = moveTowards(currentMouthSize, targetMouthSize, 1.5);
    tweenEye(leftEye);
    tweenEye(rightEye);

    mouthOffsetX = smoothMove(mouthOffsetX, targetMouthOffsetX);

    display.clearDisplay();

    if(currentMode == MODE_CLOCK){
        drawclock();   
        //drawTinyFace();    
    } else{
        drawEyes();
        drawMouth();
        drawCalmBar();
    }
    display.display();

    delay(20);
}