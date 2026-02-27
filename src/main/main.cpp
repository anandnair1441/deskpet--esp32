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
    STATE_CRASH,
    STATE_COOLDOWN
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

#define COOLDOWN_DURA   6000
#define FORGIVE_DURA    10000
#define ANNOYED_DURA    600
#define CRASH_DURA      900

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

int petCount=0;
unsigned long forgiveElapsed=0;
unsigned long cooldownStart=0;
bool forgiveRunning=0;
unsigned long forgiveLastTick = 0;
unsigned long annoyStart = 0;
bool isAnnoyed = false;
unsigned long annoyStart = 0;
unsigned long crashStart = 0;

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

        case STATE_CRASH:
            crashStart=now;
            break;
        
            case STATE_COOLDOWN:
            cooldownStart=now;
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
    bool fullPet = (now - touchStartTime) > PET_THRESHOLD;

    if (fullPet){
        petCount++;
        if(petCount>=4)
            setState(STATE_CRASH);
        else setState(STATE_POSTPET);
    } else {
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
        case STATE_POSTPET:
            if (mouth_shape == 1) drawPostPettingEyes();
            else { drawCrescentEye(leftCX); drawCrescentEye(rightCX); }
            return;

        case STATE_PETTING:
            drawCrescentEye(leftCX);
            drawCrescentEye(rightCX);
            return;

        case STATE_SQUINTING:
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

        default:
            int h      = (int)currentEyeH;
            if (h < 2) h = 2;
            int radius = (h <= 4) ? 2 : EYE_RADIUS;
            int ly     = EYE_Y + (EYE_H - h) / 2;
            display.fillRoundRect(EYE_X_L, ly, BASE_EYE_W, h, radius, SSD1306_WHITE);
            display.fillRoundRect(EYE_X_R, ly, BASE_EYE_W, h, radius, SSD1306_WHITE);
            return;
    }
}

void drawMouth()
{

    if (mouth_shape == 1){ // w mouth
        int cx = 64;
        int cy = MOUTH_Y + 4;

        display.drawLine(cx - 10, cy, cx - 5, cy + 5, SSD1306_WHITE);

        display.drawLine(cx - 5, cy + 5, cx, cy + 2, SSD1306_WHITE);

        display.drawLine(cx, cy + 2, cx + 5, cy + 5, SSD1306_WHITE);

        display.drawLine(cx + 5, cy + 5, cx + 10, cy, SSD1306_WHITE);
        return;
    }
    else if(mouth_shape ==2){ //frown mouth
        
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
    if (currentState==STATE_POSTPET && now - postTouchStart > 1000)
    {
       setState(STATE_NORMAL);
        mouth_shape = 0;
    }


}void updateCooldown(){
    if(currentState==STATE_COOLDOWN){
        if(now-cooldownStart>=COOLDOWN_DURA){
            setState(STATE_NORMAL);
        }
    }else if(currentState==STATE_CRASH){
        if(now-crashStart>=CRASH_DURA){
            setState(STATE_COOLDOWN);
        }
    }else if(isAnnoyed==1 && now-annoyStart>=ANNOYED_DURA){
        isAnnoyed=0;
        setState(STATE_COOLDOWN);
    }
}

void updateForgive(){
    if(petCount == 0)return;
    bool isPaused =(currentState==STATE_COOLDOWN|| 
             currentState==STATE_CRASH || 
             currentState==STATE_PETTING || 
             currentState==STATE_POSTPET|| isAnnoyed );

    if(isPaused){
        if(forgiveRunning){
            forgiveElapsed += now - forgiveLastTick;
            forgiveRunning=0;
        }
    }else{
        if(!forgiveRunning){
        forgiveRunning = true;
        forgiveLastTick = now;
        }
        if(forgiveElapsed + (now - forgiveLastTick)>=FORGIVE_DURA){
            petCount=0;
            forgiveElapsed=0;
            forgiveRunning=0;
            
        }
    }
}


void updateCrash(){
    unsigned long elapsed = now - crashStart;

    //0.0 → 1.0
    float progress = (float)elapsed / CRASH_DURA;

    // 1.0 max
    if (progress > 1.0)
        progress = 1.0;

    // 0 → 25%
    if (progress <= 0.25){
        targetEyeH = EYE_H;   
        targetMouthSize = 14.0; 
        mouth_shape = 0;        
    }
    // 25% → 100%
    else{
        // 25% → 100% into 0 → 1
        float shrinkProgress = (progress - 0.25) / 0.75;

        targetEyeH = EYE_H - shrinkProgress * (EYE_H - 14);

        if (shrinkProgress > 0.5)
            mouth_shape = 2;
        else
            mouth_shape = 0;
        targetMouthSize = 14.0 - shrinkProgress * 6.0;
    }if (elapsed >= CRASH_DURA)
        setState(STATE_COOLDOWN);
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

    if (singleTouch){
        SingleTapAction();
        singleTouch = 0;
    }

    if (doubleTouch){ 
        doubleTapAction(); 
        doubleTouch = 0; }

    if (isLongTouch && currentState!=STATE_PETTING){
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