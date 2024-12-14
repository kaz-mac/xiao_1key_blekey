/*
  AnyButton.h
  物理ボタンを異なる種類のように振舞わせるライブラリ

  Copyright (c) 2024 Kaz  (https://akibabara.com/blog/)
  Released under the MIT license.
  see https://opensource.org/licenses/MIT

  これは何？
  ・プッシュスイッチをトグルスイッチのように扱いたい
  ・トグルスイッチをプッシュスイッチのように扱いたい
  ・状態が変化したときだけ処理できるようにしたい
  そんな夢を叶えるやつ

  これの何が嬉しいのか？
    ゲームによってはキー設定が、押しっぱなしの場合と、1回押すだけ場合があり、
    物理的なスイッチの挙動と求められる動作方法が一致しないことが多々あります。
    このライブラリを使えば、動作の違いを吸収することができます。
    また、状態が変化したときだけ処理をさせることができるので、プログラムを簡素化できます。

  基本的な仕様
    getStateChanged() の戻り値
      -1 : 状態に変化なし
       0 : ワンショットモードのとき、値がOFFに戻ったことを意味する
       1 : OFFに変わった
       2 : ON に変わった
      セレクトモードのときは 1,2,3,4...と変化する

  使用例
  プッシュスイッチを押すと2(ON)、離すと1(OFF)
    btnPush.configButton(AnyButton::TypePush, AnyButton::ModeDirect, AnyButton::SpanEver);
  プッシュスイッチを押すたびに 1→2→1→2 と変化する 
    btnAtom.configButton(AnyButton::TypePush, AnyButton::ModeSelect, AnyButton::SpanEver);
    最大値を変化させたい場合は setSelectMax(4); のように設定する
  トグルスイッチをONにすると2、OFFにすると1を出力して、100ms後に0にする
    btnPush.configButton(AnyButton::TypeToggle, AnyButton::ModeDirect, AnyButton::SpanOneshot);
*/
#pragma once
#include <Arduino.h>
//#include <M5Unified.h>

// デバッグに便利なマクロ定義 --------
#define sp(x) Serial.println(x)
#define spn(x) Serial.print(x)
#define spp(k,v) Serial.println(String(k)+"="+String(v))
#define spf(fmt, ...) Serial.printf(fmt, __VA_ARGS__)
#define array_length(x) (sizeof(x) / sizeof(x[0]))

class AnyButton {
public:
  enum InputType { TypePush, TypeToggle };
  enum OutputMode { ModeDirect, ModeSelect };
  enum OutSpan { SpanEver, SpanOneshot };

  InputType _type = TypePush;
  OutputMode _mode = ModeDirect;
  OutSpan _span = SpanEver;
  uint16_t _selmax = 2;
  uint16_t _spanms = 200;

  int lastState = 0;
  int nowState = 0;
  bool changed = false;
  bool autoClose = false;
  int outputState = 0;
  uint16_t closeRemain = 0;
  uint16_t lastCheck = 0;
  int lastResShot = -1;
  uint16_t antiChatteringTime = 5;  // チャタリング回避 5ms

public:
  AnyButton() { configButton(TypePush, ModeDirect, SpanEver); };
  ~AnyButton() = default;

public:
  void configButton(InputType type=TypePush, OutputMode mode=ModeDirect, OutSpan span=SpanEver);
  void clear();
  void loadState(int state=-1);
  void loadState(bool state) { loadState(state ? 1 : 0); };
  int getStateChanged();
  int getStateValue();
  void setSelectMax(uint16_t num) { _selmax = num; };
  void setOneshotTime(uint16_t num) { _spanms = num; };

private:
  void changeState();
};
