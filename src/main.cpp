#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C
#define TOUCH_PIN 4

// Face geometry - same values as original
#define BASE_EYE_W 30
#define EYE_H 44
#define EYE_Y 5
#define EYE_X_L 16
#define EYE_X_R 82
#define EYE_RADIUS 8
#define MOUTH_Y 42

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

void drawEyes() {
    display.fillRoundRect(EYE_X_L, EYE_Y, BASE_EYE_W, EYE_H, EYE_RADIUS, SSD1306_WHITE);
    display.fillRoundRect(EYE_X_R, EYE_Y, BASE_EYE_W, EYE_H, EYE_RADIUS, SSD1306_WHITE);
}

void drawMouth() {
    // Smile: big circle with top cut off by black circle
    display.fillCircle(64, MOUTH_Y + 5, 9, SSD1306_WHITE);
    display.fillCircle(64, MOUTH_Y + 1, 9, SSD1306_BLACK);
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
    display.clearDisplay();
    drawEyes();
    drawMouth();
    display.display();
    delay(20);
}