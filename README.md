# DesPet

ESP32-powered desktop companion with animated face, touch interaction, and live weather --
on a 128x64 OLED.

![Language](https://img.shields.io/badge/language-C%2FC%2B%2B-blue)
![Platform](https://img.shields.io/badge/platform-ESP32-blue)
![Framework](https://img.shields.io/badge/framework-Arduino-teal)
![IDE](https://img.shields.io/badge/IDE-PlatformIO-orange)
![License](https://img.shields.io/badge/license-MIT-green)


I came across a desk pet robot and figured I could build something similar. Most open-source versions I found did the same thing - 
switch expressions on touch and nothing else. This one has autonomous behaviour - idle cycles, yawning, looking around, falling asleep - 
driven by a state machine.

During development the face animation froze for ~3 seconds every time the ESP32 fetched weather data. The HTTP request was blocking 
the main loop entirely. Moving the fetch into a FreeRTOS task on its own core fixed it -- and was also when I realised the ESP32 is dual-core

## What it does
 
- Custom pixel-art face with animations -- blink, look around, yawn, progressive sleep cycle
- Capacitive touch: tap, double-tap, long press, rapid taps -- each triggers a distinct reaction
- Four display modes: face, clock (NTP-synced), weather, 3-day forecast
- Live weather via OpenWeatherMap with PROGMEM bitmap icons
 
## Hardware
 
| Component      | Details                        |
|----------------|--------------------------------|
| ESP32 (classic)| Dual-core, tested on DevKit v1 |
| SSD1306 OLED   | 128x64, I2C                    |
| Touch input    | GPIO 4 -- bare wire or TTP223B module |
 
## Wiring
 
```
GPIO 21 -> SDA
GPIO 22 -> SCL
GPIO 4  -> Touch wire
3.3V    -> VCC
GND     -> GND
```
 
## Setup
 
### 1. Clone
 
```bash
git clone https://github.com/yourusername/despet.git
cd despet
```
 
### 2. Create your secrets file
 
```bash
cp secrets.h.example secrets.h
```
 
```cpp
// secrets.h
#define WIFI_SSID    "your_wifi_name"
#define WIFI_PASS    "your_wifi_password"
#define OWM_API_KEY  "your_openweathermap_key"
```
 
> Free API key at openweathermap.org.
 
### 3. Set your city
 
Set `city` and `countryCode` in `weather.cpp`.
 
### 4. Flash
 
```bash
pio run --target upload
```
 
## Interaction
 
| Input                  | Result                        |
|------------------------|-------------------------------|
| Single tap             | Squint -- style chosen randomly |
| Double tap             | Switch display mode           |
| Long press (>2s)       | Petting reaction              |
| Rapid taps (7+ in 3s) | Dizzy state                   |
| Touch while sleeping   | Wake animation                |
 
Double-tap window: 450ms (`DOUBLE_TAP_DELAY`).
 
## Technical notes
 
### FreeRTOS weather task
 
Fetch runs as a dedicated task with an 8192-byte stack, waking every 10 minutes. The animation loop never waits on it.
 
Weather data is shared via globals -- low risk given the update frequency, but a mutex or queue is the correct approach and planned for v2.
 
### Animation
 
Eye geometry and mouth size are tweened every frame using `smoothMove` and `moveTowards`. Startup and wake sequences use `easeOut` and `easeInOut` curves. Loop 
targets 50fps via 20ms yield; I2C is the bottleneck and actual frame rate hasn't been profiled.
 
### Expressions
 
All expressions drawn from scratch using coordinate math -- no sprite sheets, no external assets. Designs were iterated in a pixel editor before being translated into draw calls.

### Memory
 
241,996 bytes free heap after initialisation. Icons stored in PROGMEM. Scrolling description text updates every 30ms.
 
## Project structure
 
```
src/
|-- main.cpp        -- state-machine driven behaviour, face logic, animation, touch input
|-- weather.cpp     -- fetch, rendering, bitmaps
|-- weather.h       -- shared types and externs
|-- secrets.h       -- api key and wifi creds
```
 
v2 will decompose `main.cpp` into separate animation, touch, and state modules.
 
## Planned
 
- Port to ESP32-C3
- BLE -- receive and display messages
- Motion sensor for proximity wake
- Custom PCB + enclosure with battery module
## License
 
MIT
