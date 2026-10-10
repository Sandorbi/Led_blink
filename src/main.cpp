
#include <Arduino.h>
#include <esp_arduino_version.h>
#include <string.h>
#include <ctype.h>

// =====================================
// 1. PIN CONFIGURATION
// =====================================

const uint8_t RED_PINS[] = {
    16, 17, 18, 19, 21, 23, 25, 26, 27
};

const uint8_t RED_COUNT =
    sizeof(RED_PINS) / sizeof(RED_PINS[0]);

const uint8_t GREEN_PIN = 22;

// =====================================
// 2. PWM CONFIGURATION
// =====================================

const uint32_t PWM_FREQUENCY = 5000;
const uint8_t PWM_RESOLUTION = 8;
const uint16_t MAX_BRIGHTNESS = 255;

// =====================================
// 3. TIMING CONFIGURATION
// =====================================

// Time between successive LEDs
const unsigned long STAGGER_MS = 180;

// Time for each LED to fade in/out
const unsigned long FADE_MS = 500;

// Hold all LEDs at peak brightness
const unsigned long PEAK_HOLD_MS = 500;

// Time between PWM updates
const unsigned long FRAME_MS = 15;

// Safety timeout: 5 seconds
const unsigned long SAFETY_TIMEOUT = 5000;

// Calculated sequence timing
const unsigned long FADE_OUT_START =
    (RED_COUNT - 1) * STAGGER_MS +
    FADE_MS + PEAK_HOLD_MS;

const unsigned long SEQUENCE_END =
    FADE_OUT_START +
    (RED_COUNT - 1) * STAGGER_MS +
    FADE_MS;

// =====================================
// 4. SYSTEM STATE
// =====================================

bool sequenceRunning = false;
bool safetyActive = false;

unsigned long sequenceStart = 0;
unsigned long lastCommandTime = 0;
unsigned long lastFrameTime = 0;

char commandBuffer[32];
uint8_t commandLength = 0;

// =====================================
// 5. PWM COMPATIBILITY
// =====================================

void setupPWM() {
    for (uint8_t i = 0; i < RED_COUNT; i++) {

#if ESP_ARDUINO_VERSION_MAJOR >= 3
        ledcAttach(
            RED_PINS[i],
            PWM_FREQUENCY,
            PWM_RESOLUTION
        );
#else
        ledcSetup(
            i,
            PWM_FREQUENCY,
            PWM_RESOLUTION
        );
        ledcAttachPin(RED_PINS[i], i);
#endif
    }
}

void writeRed(uint8_t index, uint8_t duty) {

#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(RED_PINS[index], duty);
#else
    ledcWrite(index, duty);
#endif
}

// =====================================
// 6. OUTPUT CONTROL
// =====================================

void redOff() {
    for (uint8_t i = 0; i < RED_COUNT; i++) {
        writeRed(i, 0);
    }
}

void stopAll() {
    sequenceRunning = false;
    safetyActive = false;

    redOff();
    digitalWrite(GREEN_PIN, LOW);
}

// =====================================
// 7. BRIGHTNESS CALCULATION
// =====================================

uint8_t getBrightness(
    unsigned long elapsed,
    uint8_t index
) {
    // LEDs turn on from first to last
    unsigned long fadeInStart =
        index * STAGGER_MS;

    // LEDs turn off in reverse order
    unsigned long fadeOutStart =
        FADE_OUT_START +
        (RED_COUNT - 1 - index) * STAGGER_MS;

    // Not started yet
    if (elapsed < fadeInStart) {
        return 0;
    }

    // Gradually brighten
    if (elapsed < fadeInStart + FADE_MS) {
        return (elapsed - fadeInStart) *
               MAX_BRIGHTNESS / FADE_MS;
    }

    // Stay at maximum brightness
    if (elapsed < fadeOutStart) {
        return MAX_BRIGHTNESS;
    }

    // Gradually fade out
    if (elapsed < fadeOutStart + FADE_MS) {
        return MAX_BRIGHTNESS -
               (elapsed - fadeOutStart) *
               MAX_BRIGHTNESS / FADE_MS;
    }

    return 0;
}

// =====================================
// 8. NON-BLOCKING LED ANIMATION
// =====================================

void updateSequence() {
    if (!sequenceRunning) return;

    unsigned long now = millis();

    if (now - lastFrameTime < FRAME_MS) {
        return;
    }

    lastFrameTime = now;

    unsigned long elapsed =
        now - sequenceStart;

    // Animation completed
    if (elapsed >= SEQUENCE_END) {
        redOff();
        sequenceRunning = false;
        Serial.println("OK: Sequence complete");
        return;
    }

    // Update brightness of every red LED
    for (uint8_t i = 0; i < RED_COUNT; i++) {
        uint8_t brightness =
            getBrightness(elapsed, i);

        writeRed(i, brightness);
    }
}

// =====================================
// 9. SERIAL COMMAND HANDLER
// =====================================

void handleCommand(const char* cmd) {

    if (strcmp(cmd, "START") == 0) {

        stopAll();

        unsigned long now = millis();

        sequenceRunning = true;
        safetyActive = true;

        sequenceStart = now;
        lastCommandTime = now;
        lastFrameTime = now - FRAME_MS;

        digitalWrite(GREEN_PIN, HIGH);

        Serial.println("OK: Hot flash started");
    }

    else if (strcmp(cmd, "STOP") == 0) {

        stopAll();

        Serial.println("OK: All LEDs OFF");
    }

    else if (strcmp(cmd, "PING") == 0) {

        if (safetyActive) {
            lastCommandTime = millis();
            Serial.println("OK: Safety timer reset");
        } else {
            Serial.println("ERR: Safety inactive");
        }
    }

    else if (strcmp(cmd, "STATUS") == 0) {

        if (safetyActive) {
            lastCommandTime = millis();
        }

        Serial.print("Sequence: ");
        Serial.println(
            sequenceRunning ? "RUNNING" : "IDLE"
        );

        Serial.print("Safety: ");
        Serial.println(
            safetyActive ? "ACTIVE" : "OFF"
        );
    }

    else if (strcmp(cmd, "HELP") == 0) {

        if (safetyActive) {
            lastCommandTime = millis();
        }

        Serial.println(
            "Commands: START, STOP, PING, STATUS, HELP"
        );
    }

    else {
        Serial.println("ERR: Unknown command");
    }
}

// =====================================
// 10. USB SERIAL INPUT
// =====================================

void readSerial() {

    while (Serial.available() > 0) {

        char c = Serial.read();

        // Command completed by Enter
        if (c == '\n' || c == '\r') {

            if (commandLength > 0) {

                commandBuffer[commandLength] = '\0';

                handleCommand(commandBuffer);

                commandLength = 0;
            }
        }

        // Add valid characters to buffer
        else if (c >= 32 && c <= 126) {

            if (commandLength <
                sizeof(commandBuffer) - 1) {

                commandBuffer[commandLength++] =
                    toupper((unsigned char)c);

            } else {
                commandLength = 0;
                Serial.println("ERR: Command too long");
            }
        }
    }
}

// =====================================
// 11. SETUP
// =====================================

void setup() {

    Serial.begin(115200);

    pinMode(GREEN_PIN, OUTPUT);

    setupPWM();
    stopAll();

    Serial.println();
    Serial.println("======================");
    Serial.println("ESP32 HOT FLASH SYSTEM");
    Serial.println("======================");
    Serial.println("9 Red PWM LEDs");
    Serial.println("1 Green Safety LED");
    Serial.println("Commands: START, STOP, PING, STATUS, HELP");
    Serial.println("System ready!");
}

// =====================================
// 12. MAIN LOOP
// =====================================

void loop() {

    // First priority: read commands
    readSerial();

    // Automatically shut down if communication stops
    if (safetyActive &&
        millis() - lastCommandTime >= SAFETY_TIMEOUT) {

        stopAll();

        Serial.println(
            "SAFETY TIMEOUT: All outputs OFF"
        );
    }

    // Keep animation running without blocking
    updateSequence();
}
