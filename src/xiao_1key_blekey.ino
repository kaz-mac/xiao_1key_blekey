/*
  xiao_1key_blekey.ino
  Bluetoothミニキーボード for XIAO BLE nRF52840

  Copyright (c) 2024 Kaz  (https://akibabara.com/blog/)
  Released under the MIT license.
  see https://opensource.org/licenses/MIT

基本動作
  電源投入すると30秒間アドバタイズする（高速10秒＋低速20秒）
  ペアリングおよび接続が行われるとHIDキーボードとして動作する。30秒過ぎると電源オフ(deep sleep)
  アイドル状態が30分続くと切断して電源オフ(deep sleep)
  deep sleep中にキーを押すと復帰

参考: 電流測定結果 Vcc3.0V TX=4dB時
  高速アドバタイズ中(20ms)  1460uA
  高速アドバタイズ中(152ms)  245uA
  BLE接続中 待機時           617uA
  BLE接続中 キー押下2回/s    714uA
  deep Sleep中                4uA (最小時 1.967uA)
  https://akibabara.com/blog/7539.html

TODO
  アドバタイズ終了時のDeep Sleep移行をコールバック関数内でやるのは良くない気がする
*/
#include <Arduino.h>
#include <Adafruit_TinyUSB.h>   // for Serial

// 基本動作設定
#define DEBUG_USB 0   // デバッグ　USB Serialでデバッグする
const uint32_t ILDE_TIME = 60*30;   // 自動電源オフするまでの秒数 [s]
const uint16_t ADV_BETWEEN = 30;    // アドバタイズする秒数 [s]、その間に接続しない場合はdeep sleepする
const uint16_t ADV_SLOW_AFTER = 10; // 低速アドバタイズに切り替わるまでの秒数 [s]
#define BLE_DEVICE_NAME  "1-Ctrl Button"
#define BLE_MANUFACTURER "akibabara.com"
#define BATTERY_CR2032  // バッテリー種別 CR2032

// GPIOの設定
#define GPIO_BTN1 0
#define GPIO_BTN2 1
#define GPIO_BTN3 2
#define GPIO_BTN4 3
#define PIN_HICHG   22  // 高速充電(in)
#define PIN_INVCHG  23  // 充電状態の確認用(out)

// 外部QSPI Flash Memory（省電力化のために使用）
#include <Adafruit_SPIFlash.h>
Adafruit_FlashTransport_QSPI flashTransport;
Adafruit_SPIFlash flash(&flashTransport);

// BLE関連
#include <bluefruit.h>
BLEConnection* connection;
BLEDis bledis;
BLEHidAdafruit blehid;  // https://github.com/adafruit/Adafruit_nRF52_Arduino/blob/master/libraries/Bluefruit52Lib/src/services/BLEHidAdafruit.h
BLEBas blebas;
// 参考 HID Adafruit_TinyUSB_Arduino https://github.com/adafruit/Adafruit_TinyUSB_Arduino/blob/master/src/class/hid/hid.h

// ボタンライブラリ
#include "AnyButton.h"
AnyButton btn1, btn2, btn3, btn4; 

// LED点滅
#include "LedIndicator.h"

// デバッグに便利なマクロ定義 --------
#define sp(x) Serial.println(x)
#define spn(x) Serial.print(x)
#define spp(k,v) Serial.println(String(k)+"="+String(v))
#define spf(fmt, ...) Serial.printf(fmt, __VA_ARGS__)
#define array_length(x) (sizeof(x) / sizeof(x[0]))


// バッテリー情報
typedef struct {
  uint16_t mv;
  uint8_t per;
} BatteryInfo;
BatteryInfo batt = { 0, 0 };
uint16_t battMvHist[10];

// キーボードLEDのコールバック関数
void set_keyboard_led(uint16_t conn_handle, uint8_t led_bitmap) {
  (void) conn_handle;
  // led_bitmap: Kana (4) | Compose (3) | ScrollLock (2) | CapsLock (1) | Numlock (0)
  spf("KEY-LED Callback: %05b\n", led_bitmap);
}

// BLE接続時のコールバック関数
uint16_t ble_conn_handle = 0;   // connection handleを保存しておくグローバル変数
void connectCallback(uint16_t conn_handle) {
  ble_conn_handle = conn_handle;
  // デバッグ　接続元を表示
  connection = Bluefruit.Connection(conn_handle);
  char central_name[32] = { 0 };
  connection->getPeerName(central_name, sizeof(central_name));
  sp("connected to "+String(central_name));
  sp("Connection Interval [ms] "+String(connection->getConnectionInterval() * 1.25));
  // BLEの接続が確立されたらアドバタイズを停止する
  Bluefruit.Advertising.stop();
  blinkLED(GREEN, 10, 4000);    // LED 緑点滅(slow)する
}

// BLE切断時のコールバック関数
void disconnectCallback(uint16_t conn_handle, uint8_t reason) {
  (void) conn_handle;
  (void) reason;
  spf("Disconnected, reason=%02x\n", reason);
  Bluefruit.Advertising.start(ADV_BETWEEN);  // アドバタイズを開始する。nn秒後にアドバタイズ停止
  blinkLED(BLUE, 10, 500);    // LED 青点滅(fast)する
}

// アドバタイズ終了時のコールバック関数　アドバタイズ終了時に未接続ならdeep sleepする
bool nowDeepSleeping = false;
void finishAdvertisingCallback(void) {
  sp("Advertising finished");
  if (Bluefruit.Periph.connected()) {   // BLE接続中
    sp("now established");
  } else {  // BLE未接続
    if (!nowDeepSleeping) enterDeepSleep();
  }
}

// deep sleepモードに入る（復帰はリスタートになる）
void enterDeepSleep() {
  nowDeepSleeping = true;
  // BLE接続を切断してアドバタイズも停止する
  Bluefruit.disconnect(ble_conn_handle);
  Bluefruit.Advertising.restartOnDisconnect(false); 
  Bluefruit.Advertising.stop();
  // 終了の合図
  blinkLED(RED, 100,200);
  delay(2000);
  stopBlink();  // LED停止
  delay(100);
  // wake upの設定、およびdeep sleep
  nrf_gpio_cfg_sense_set(NRF_GPIO_PIN_MAP(0,2), NRF_GPIO_PIN_SENSE_HIGH);  // P0.02 = D0ピンでdeep sleepから復帰
  sd_power_system_off();
  // NRF_POWER->SYSTEMOFF = 1;
}

// キー入力　単独
void keyPush(char chr, uint32_t wait=20) {
  blehid.keyPress(chr);
  delay(wait);
  blehid.keyRelease();
  delay(wait);
}

// キー入力　複数
void keyPushModifer(uint8_t key, uint8_t modifier, uint32_t wait=20) {
  hid_keyboard_report_t report = {0};
  report.modifier = modifier;
  report.keycode[0] = key;
  blehid.keyboardReport(&report);
  delay(wait);
  blehid.keyRelease();
  delay(wait);
}

// バッテリー残量を取得する
void measureBatteryLevel(bool initsma=false) {
  uint8_t per = 0;
  static uint8_t smaIdx = 0;
  uint16_t histNum = sizeof(battMvHist) / sizeof(battMvHist[0]);
  uint16_t mv;

  // バッテリー電圧を測定
  for (int i=0; i<histNum; i++) {
    uint16_t vbatRaw = analogRead(PIN_VBAT);
    mv = vbatRaw * 2400 / 1023; // VREF = 2.4V, 10bit A/D
    mv = mv * 1510 / 510;       // 1M + 510k / 510k
    spp("Battery[mV]", mv);
    battMvHist[smaIdx++] = mv;
    if (smaIdx >= histNum) smaIdx = 0;
    if (!initsma) break;
  }

  // 移動平均を求める
  uint16_t mvSum = 0;
  for (int i=0; i<histNum; i++) mvSum += battMvHist[i];
  mv = mvSum / histNum;
  spp("Ave.Battery[mV]", mv);
  batt.mv = mv;

  // バッテリー残量%を推定する
  #ifdef BATTERY_CR2032
  // CR2032放電特性テーブル
  // per = map(mv, 1700, 3000, 0, 100);  // 範囲 1.7V～3.0V
  // per = constrain(per, 0, 100);
  mv = constrain(mv, 1700, 3000);
  uint16_t tableMv[]  = { 3000, 2700, 2600, 2500, 2400, 2300, 2250, 2200 };
  uint16_t tablePer[] = {  100,   98,   42,   22,   12,    4,    2,    0 };
  size_t tableNum = sizeof(tableMv) / sizeof(tableMv[0]);
  for (int i=0; i<tableNum-1; i++) {
    if (mv <= tableMv[i] && mv > tableMv[i+1]) {
      per = map(mv, tableMv[i+1], tableMv[i], tablePer[i+1], tablePer[i]);
      break;
    }
  }
  #endif

  spp("Battery[%]", per);
  batt.per = per;
}


// 初期化
void setup() {
  // USBシリアルポートが接続されるまで待機する（DEBUG_USBモード時のみ）
  Serial.begin(115200);
  if (DEBUG_USB) {
    while (!Serial) { // これをやるとバッテリー駆動時に進まなくなるのでUSB接続必須
      delay(100);   
      if (millis() > 5000) break;
    }
  }
  sp("Start!");

  // LED GPIOの設定
  initPinLED();
  oneshotLED(RED, 2000, DEBUG_USB);

  // 入力ボタンGPIOの設定
  pinMode(GPIO_BTN1, INPUT_PULLDOWN);
  pinMode(GPIO_BTN2, INPUT_PULLDOWN);
  pinMode(GPIO_BTN3, INPUT_PULLDOWN);
  pinMode(GPIO_BTN4, INPUT_PULLDOWN);

  // ボタンCLASS設定
  btn1.configButton(AnyButton::TypePush, AnyButton::ModeDirect, AnyButton::SpanEver);
  btn2.configButton(AnyButton::TypePush, AnyButton::ModeDirect, AnyButton::SpanEver);
  btn3.configButton(AnyButton::TypePush, AnyButton::ModeDirect, AnyButton::SpanEver);
  btn4.configButton(AnyButton::TypePush, AnyButton::ModeDirect, AnyButton::SpanEver);

  // オンボードQSPI Flash MemoryをDeep Power-downモードにして省電力化する
  flashTransport.begin();
  flashTransport.runCommand(0xB9);
  delayMicroseconds(5);
  flashTransport.end();

  // 充電モード選択（未設定だと600uA減るうえ、充電はされるのであえて設定しない）
  // pinMode(PIN_HICHG, OUTPUT);   // 充電モード
  // digitalWrite(PIN_HICHG, HIGH);  // HIGH=50mA充電モード LOW=100mA
  pinMode(PIN_INVCHG, INPUT_PULLUP);  // 充電中状態確認　プルアップにすると非充電時に60uA減る

  // バッテリー電圧測定の準備
  analogReference(AR_INTERNAL_2_4); // VREF = 2.4V
  analogReadResolution(10);         // 10bit A/D
  pinMode(VBAT_ENABLE, OUTPUT);
  digitalWrite(VBAT_ENABLE, LOW);   // VBAT_ENABLEをLOWにすると測定できる
  measureBatteryLevel(true);        // 電圧測定・残量計算

  // BLEデバイスの設定
  Bluefruit.autoConnLed(false);   // 本体LEDを使用しない（false=使用しない、true=使用する）
  Bluefruit.begin();
  Bluefruit.setTxPower(4);  // 送信強度　最小 -40, 最大 +8 dBm
  Bluefruit.setName(BLE_DEVICE_NAME);
  // Bluefruit.Periph.setConnInterval(24, 40); // 送信頻度 30～50ms間隔、ただし設定してもWindowsが従わなかった  unit 1.25ms min7.5ms max4s 
  Bluefruit.Periph.setConnectCallback(connectCallback);       // BLE接続時のコールバック関数
  Bluefruit.Periph.setDisconnectCallback(disconnectCallback); // BLE切断時のコールバック関数

  // BLEデバイスInformation Serviceの設定
  bledis.setManufacturer(BLE_MANUFACTURER);
  bledis.setModel("nRF52840");
  bledis.begin();

  // BLE HIDデバイスの設定
  blehid.begin();
  blehid.setKeyboardLedCallback(set_keyboard_led);  // キーLED点灯のコールバック

  // BLEバッテリーサービス設定
  blebas.begin();

  // BLEのアドバタイズの設定
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  // Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addAppearance(BLE_APPEARANCE_HID_KEYBOARD);
  Bluefruit.Advertising.addService(blehid);
  Bluefruit.Advertising.addService(blebas);
  Bluefruit.Advertising.addName();
  Bluefruit.Advertising.restartOnDisconnect(false);  // 接続切断後に再アドバタイズさせない（切断後はdeep sleepモードに入るから）
  Bluefruit.Advertising.setStopCallback(finishAdvertisingCallback);   // アドバタイズ終了時のコールバック
  Bluefruit.Advertising.setInterval(32, 244);  // 高頻度 32=20ms, 低頻度 244=152ms（nn*0.625ms）
  Bluefruit.Advertising.setFastTimeout(ADV_SLOW_AFTER);    // nn秒後に低速アドバタイズに移行
  Bluefruit.Advertising.start(ADV_BETWEEN);  // アドバタイズを開始する。nn秒後にアドバタイズ停止。0にすると無制限に継続
  blinkLED(BLUE, 10, 500);    // LED 青点滅(fast)する

  // WDTの設定
  // NRF_WDT->CONFIG         = 0x01;     // Configure WDT to run when CPU is asleep
  // NRF_WDT->CRV            = 1+32768*120;    // CRV = timeout * 32768 + 1
  // NRF_WDT->RREN           = 0x01;     // Enable the RR[0] reload register
  // NRF_WDT->TASKS_START    = 1;        // Start WDT       
}

// メイン
void loop() {
  int state;
  static uint32_t idlems = millis() + ILDE_TIME * 1000;
  bool active = false;

  // 接続されるまで待機する
  if (!Bluefruit.Periph.connected()) {
    delay(50);
    return;
  }

  // ボタンの状態を取得
  btn1.loadState(digitalRead(GPIO_BTN1));
  btn2.loadState(digitalRead(GPIO_BTN2));
  btn3.loadState(digitalRead(GPIO_BTN3));
  btn4.loadState(digitalRead(GPIO_BTN4));

  // ボタン1が押された場合の処理 CTRLキー
  hid_keyboard_report_t report;
  state = (btn1.getStateChanged());
  if (state == 2) {   // 押された
    sp("button 1 press");
    report = { KEYBOARD_MODIFIER_LEFTCTRL, 0, {0} };  // CTRL押しっぱなし
    blehid.keyboardReport(&report);
    active = true;
  } else if (state == 1) {    // 離した
    sp("button 1 release");
    blehid.keyRelease();
    active = true;
  }

  // ボタン2が押された場合の処理 CTRL+C
  if ((btn2.getStateChanged()) == 2) {
    sp("button 2");
    keyPushModifer(HID_KEY_C, KEYBOARD_MODIFIER_LEFTCTRL);
    active = true;
  }

  // ボタン3が押された場合の処理 CTRL+V
  if ((btn3.getStateChanged()) == 2) {
    sp("button 3");
    keyPushModifer(HID_KEY_V, KEYBOARD_MODIFIER_LEFTCTRL);
    active = true;
  }

  // ボタン4が押された場合の処理 くぁｗせｄｒｆｔｇｙふじこｌｐ
  if ((btn4.getStateChanged()) == 2) {
    sp("button 4");
    // keyPushModifer(HID_KEY_GRAVE, KEYBOARD_MODIFIER_LEFTALT);
    // blehid.keySequence("qawsedrftgyhujikolp\n", 10);
    // keyPushModifer(HID_KEY_GRAVE, KEYBOARD_MODIFIER_LEFTALT);
    String str = "Battery "+String(batt.mv)+"mV "+String(batt.per)+"%\n";
    blehid.keySequence(str.c_str(), 10);
    active = true;
    // enterDeepSleep();
  }

  // バッテリー残量を更新する　キーを押したときまたは30秒経過後、ただし15秒以内の更新はしない
  static uint32_t tm = 0;
  static uint32_t tmLast = 0;
  if ((active || tm < millis()) && millis()-tmLast > 15000) {
    measureBatteryLevel();
    blebas.notify(batt.per);
    tm = millis() + 1000 * 30;
    tmLast = millis();
  }

  // アイドル状態が続いたらdeep sleepする
  if (active) {
    idlems = millis() + ILDE_TIME * 1000;
  }
  if (idlems < millis()) {
    sp("Good night...");
    enterDeepSleep();
  }

  delay(25);
}
