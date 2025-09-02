#include <Arduino.h>
#include "TM1637.h"

// ---- Pins (XIAO nRF52840) ----
#define PIN_TM1637_CLK 2   // D2
#define PIN_TM1637_DIO 3   // DIO
#define PIN_VIBE       5   // Grove Vibration Motor SIG
#define PIN_ENC_A      6
#define PIN_ENC_B      7
#define PIN_ENC_SW     8
#define PIN_LOCK       9   // SPDT center

TM1637 tm1637(PIN_TM1637_CLK, PIN_TM1637_DIO);

// ---- State ----
enum Mode { IDLE, RUNNING, FINISHED };
Mode mode = IDLE;

volatile int32_t setSeconds = 60; // default 01:00
int32_t remainingMs = 60000;

unsigned long lastTick = 0;
int8_t encLast = 0;
unsigned long lastEncMoveMs = 0;
const unsigned long ENC_STEP_DEBOUNCE = 2;

bool lastBtn = HIGH;
unsigned long lastBtnChange = 0;
const unsigned long BTN_DEBOUNCE = 30;

int lastLock = LOW;
unsigned long lastLockRead = 0;

// ---- Helpers ----
void tmDigit(uint8_t pos, uint8_t d) {
  // d: 0..9; 0x7f = blank
  tm1637.display(pos, (d <= 9) ? d : 0x7f);
}

void showTime(uint32_t seconds) {
  uint16_t mm = seconds / 60;
  uint16_t ss = seconds % 60;
  if (mm > 99) mm = 99;

  // MM:SS across 4 digits, colon ON
  tm1637.point(POINT_ON);
  tmDigit(0, (mm / 10) % 10);
  tmDigit(1, mm % 10);
  tmDigit(2, (ss / 10) % 10);
  tmDigit(3, ss % 10);
}

int readLock() {
  int v = digitalRead(PIN_LOCK);
  if (millis() - lastLockRead > 5) {
    lastLock = v;
    lastLockRead = millis();
  }
  return lastLock; // HIGH=unlocked (to 3V3), LOW=locked (to GND)
}

void setVibe(bool on) {
  digitalWrite(PIN_VIBE, on ? HIGH : LOW);
}

void setup() {
  pinMode(PIN_VIBE, OUTPUT); digitalWrite(PIN_VIBE, LOW);
  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);
  pinMode(PIN_LOCK, INPUT); // hard-wired by SPDT

  tm1637.init();
  tm1637.set(5);            // 0..7 brightness
  tm1637.point(POINT_ON);   // show colon
  showTime(setSeconds);

  lastTick = millis();
  encLast = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
}

void handleEncoder() {
  if (readLock() == LOW) return;    // locked
  if (mode != IDLE) return;

  int a = digitalRead(PIN_ENC_A);
  int b = digitalRead(PIN_ENC_B);
  int state = (a << 1) | b;

  if (state != encLast) {
    unsigned long now = millis();
    if (now - lastEncMoveMs >= ENC_STEP_DEBOUNCE) {
      int delta = 0;
      if ((encLast == 0b00 && state == 0b01) ||
          (encLast == 0b01 && state == 0b11) ||
          (encLast == 0b11 && state == 0b10) ||
          (encLast == 0b10 && state == 0b00)) delta = +1;
      else if ((encLast == 0b00 && state == 0b10) ||
               (encLast == 0b10 && state == 0b11) ||
               (encLast == 0b11 && state == 0b01) ||
               (encLast == 0b01 && state == 0b00)) delta = -1;

      if (delta) {
        long ns = (long)setSeconds + delta;
        if (ns < 0) ns = 0;
        if (ns > 5999) ns = 5999; // 99:59
        setSeconds = (int)ns;
        remainingMs = setSeconds * 1000L;
        showTime(setSeconds);
        lastEncMoveMs = now;
      }
    }
    encLast = state;
  }
}

void handleButton() {
  bool btn = digitalRead(PIN_ENC_SW); // LOW on press
  unsigned long now = millis();
  if (btn != lastBtn && (now - lastBtnChange) > BTN_DEBOUNCE) {
    lastBtnChange = now;
    lastBtn = btn;
    if (btn == LOW) {
      if (mode == IDLE) {
        if (setSeconds > 0) { mode = RUNNING; lastTick = now; }
      } else if (mode == RUNNING) {
        mode = IDLE;
      } else if (mode == FINISHED) {
        setVibe(false);
        mode = IDLE;
        remainingMs = setSeconds * 1000L;
        showTime(setSeconds);
      }
    }
  }
}

void loop() {
  handleEncoder();
  handleButton();

  unsigned long now = millis();
  if (mode == RUNNING) {
    if (now - lastTick >= 10) {
      remainingMs -= (now - lastTick);
      if (remainingMs <= 0) {
        remainingMs = 0;
        mode = FINISHED;
        setVibe(true);
      }
      lastTick = now;

      static unsigned long lastDisp = 0;
      if (now - lastDisp >= 100 || mode == FINISHED) {
        showTime(remainingMs / 1000);
        lastDisp = now;
      }
    }
  }
}