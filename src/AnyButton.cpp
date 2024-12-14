/*
  AnyButton.h
  物理ボタンを異なる種類のように振舞わせるライブラリ

  Copyright (c) 2024 Kaz  (https://akibabara.com/blog/)
  Released under the MIT license.
  see https://opensource.org/licenses/MIT
*/
#include "AnyButton.h"

// 動作設定
void AnyButton::configButton(AnyButton::InputType type, AnyButton::OutputMode mode, AnyButton::OutSpan span) {
  _type = type;
  _mode = mode;
  _span = span;
  clear();
}

// 内部変数の初期化
void AnyButton::clear() {
  lastState = 0;
  nowState = 0;
  changed = false;
  autoClose = false;
  outputState = 0;
  closeRemain = 0;
  lastCheck = 0;
  lastResShot = -1;
}

// ボタンの状態をクラスに与える
void AnyButton::loadState(int state) {  // 入力は 0=OFF 1=ON changeState()とは違うので注意
  if ((millis() - lastCheck) > antiChatteringTime) {
    nowState = state;
    if (nowState != lastState) {
      lastState = nowState;
      changeState();
    }
  }
  lastCheck = millis();
}

// ボタンの状態が変化した場合の内部処理
void AnyButton::changeState() {
  //sp("changeState "+String(nowState));

  // 出力：ダイレクトモードの場合
  if (_mode == ModeDirect) {
    outputState = nowState + 1;
    changed = true;
  }

  // 出力：セレクトモードの場合
  if (_mode == ModeSelect) {
    if (_type == TypePush && nowState == 1) {
      outputState ++;
      changed = true;
    } else if (_type == TypeToggle) {
      if (_selmax == 2) outputState = nowState + 1; // 2接点の場合は位置と同じ値にする
      else outputState ++;  // 3接点以上の場合（こうゆう使い方は想定してない）
      changed = true;
    }
    if (outputState > _selmax) outputState = 1;
  }

  // 自動オフの時間指定
  if (changed && _span == SpanOneshot) {
    closeRemain = millis() + _spanms;
    autoClose = true;
  }
}

// 各モードに応じた仮想ボタンの状態を返す（値に変化があったとき以外は-1）
int AnyButton::getStateChanged() {
  int res = -1;
  if (changed) {
    if (lastResShot != outputState) {
      res = outputState;
      lastResShot = res;
    }
    changed = false;
  } else if (autoClose && _span == SpanOneshot && closeRemain < millis()) {
    //if (_mode != ModeSelect) outputState = 0;
    if (lastResShot != 0) res = 0;
    lastResShot = res;
    autoClose = false;
  }
  return res;
}

// 各モードに応じた仮想ボタンの状態を返す（常に値を返す）
int AnyButton::getStateValue() {
  return outputState;
}
