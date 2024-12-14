/*
  LedIndicator.h
  バックグラウンドのタスクでLEDを点滅させる for XIAO BLE nRF52840

  Copyright (c) 2024 Kaz  (https://akibabara.com/blog/)
  Released under the MIT license.
  see https://opensource.org/licenses/MIT

  参考: 電流測定結果 Vcc3.0V ON-10ms OFF-240ms 時（秒間4回点滅時）のLED電流値増分
    赤 26uA   緑 6uA   青 11uA
    https://akibabara.com/blog/7539.html
*/
#pragma once

// 色の定義
#define RED   0b100
#define GREEN 0b010
#define BLUE  0b001

// 変数
struct TimerData {
  uint8_t color;
};
TimerData timerData;
TimerHandle_t timer1, timer2;

// LEDを全て消す
void clearLED() {
  digitalWrite(LED_RED, HIGH);
  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_BLUE, HIGH);
}

// GPIOの設定
void initPinLED() {
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  clearLED();
}

// LEDオン
void ledOn(uint8_t color) {
  if (color & 0b100) digitalWrite(LED_RED, LOW);
  if (color & 0b010) digitalWrite(LED_GREEN, LOW);
  if (color & 0b001) digitalWrite(LED_BLUE, LOW);
}

// LEDオフ
void ledOff(uint8_t color) {
  if (color & 0b100) digitalWrite(LED_RED, HIGH);
  if (color & 0b010) digitalWrite(LED_GREEN, HIGH);
  if (color & 0b001) digitalWrite(LED_BLUE, HIGH);
}

// タイマー処理　LEDオン
void timerLedOn(TimerHandle_t xTimer) {
  TimerData* data = (TimerData*)pvTimerGetTimerID(xTimer);
  ledOn(data->color);
  if (timer2 != NULL) xTimerStart(timer2, 0);
}

// タイマー処理　LEDオフ
void timerLedOff(TimerHandle_t xTimer) {
  TimerData* data = (TimerData*)pvTimerGetTimerID(xTimer);
  ledOff(data->color);
}

// LED点滅を停止する
void stopBlink() {
  if (xTimerIsTimerActive(timer1)) xTimerStop(timer1, 0);
  if (xTimerIsTimerActive(timer2)) xTimerStop(timer2, 0);
  clearLED();
}

// LEDを点滅する
void blinkLED(uint8_t color, uint16_t timeOn, uint16_t timeCycle) {
  timerData.color = color;
  stopBlink();    // 既存のタイマーを解除
  timer1 = xTimerCreate(  // timer1 LEDを点灯するタイマー
    "timerLedOn",
    pdMS_TO_TICKS(timeCycle),
    pdTRUE,   // 繰り返す
    (void*)&timerData,
    timerLedOn
  );
  timer2 = xTimerCreate(  // timer2 LEDを消灯するタイマー
    "timerLedOff",
    pdMS_TO_TICKS(timeOn),
    pdFALSE,   // 一度きり
    (void*)&timerData,
    timerLedOff
  );
  if (timer1 != NULL && timer2 != NULL) {
    timerLedOn(timer1);
    xTimerStart(timer1, 0);
  }
}

// LEDを1回点灯する
void oneshotLED(uint8_t color, uint16_t timeOn, bool waiting=false) {
  timerData.color = color;
  stopBlink();    // 既存のタイマーを解除
  timer2 = xTimerCreate(  // timer2 LEDを消灯するタイマー
    "timerLedOff",
    pdMS_TO_TICKS(timeOn),
    pdFALSE,   // 一度きり
    (void*)&timerData,
    timerLedOff
  );
  if (timer2 != NULL) {
    timerLedOn(timer2);
  }
  if (waiting) delay(timeOn);
}
