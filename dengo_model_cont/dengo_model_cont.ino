//====================================================================
//  電車でGO!マスコンで鉄道模型を運転しよう！
//  2021.10.01 7M4MON, License:GPLv2
//  http://gihtub.com/7m4mon/train_model_controller_with_dengo_mascon/
//====================================================================

// Gyokimae氏の自作ライブラリ(PSコントローラ用)
// http://pspunch.com/pd/article/arduino_lib_gpsx.html
#include <GPSXClass.h>
#include <MsTimer2.h>

//====================================================================
//  ピンの定義
//====================================================================
#define PIN_PWM1 10
#define PIN_PWM2 9
#define PIN_MODE_ADJ_MIN_SPEED A5
#define PIN_VOL_ADJ_MIN_SPEED  A6

#define PIN_PLAY1 A4    // デジトラを介しJQ6500のHotKeyへ
#define PIN_PLAY2 A3    // 
#define PIN_PLAY3 A2    // 
#define PIN_PLAY4 A1    // 
#define PIN_PLAY5 A0    // 

//====================================================================
//  電車でGO!マスコンの定義
//  KeKe115sさんの電GOコントローラーをArduinoで読み取るサンプル
//  https://github.com/KeKe115/Densya_de_go
//====================================================================
#define DIR_STOP 0
#define DIR_FWD  1
#define DIR_REV  2
const uint8_t reverser_state [4] = {DIR_STOP, DIR_FWD, DIR_STOP, DIR_REV};    // スタートボタンで状態遷移 中立→前進→中立→後退

#define MODE_MASCON     0
#define MODE_POWER_PACK 1

typedef struct
{
    uint8_t notch;
    uint8_t brake;
    uint8_t handle;
    bool button_a;
    bool button_b;
    bool button_c;
    bool button_start;
    bool button_select;
} MasconState_t;

//====================================================================
//  ７セグLED表示関連
//====================================================================
#include <TM1637Display.h>
#define PIN_SEG_CLK 12  // 外部Pullup必要
#define PIN_SEG_DIO 11  // 外部Pullup必要
TM1637Display display(PIN_SEG_CLK, PIN_SEG_DIO, 20);    // 第3引数[bitDelay]を何も指定しない(default:100) だと22msかかるので、小さめの値にする(MAX 500kHz なので min =2)

void set_7seg_disp(uint8_t hex_speed, bool p_mode){
    uint16_t percent = ((uint16_t)hex_speed * 201) >> 9;    // 255 -> 100 の固定小数点計算
    uint8_t colon = p_mode ? 0x40 : 0;
    display.showNumberDecEx(percent, colon);                // 0 の時、colonを表示しないけど気にしない。
}


//====================================================================
//  NeoPixel 12LEDs Ring 関連
//====================================================================
#include <FastLED.h>
#define PIN_RING_DATA 8
#define NUM_LEDS 12
#define OFFSET_LEDS 6
CRGB leds[NUM_LEDS];
// スピードの段階を８色で表現する。満了したら白にして、次のLEDを点灯して色を変えていく。速度ゼロのときは赤。
const CRGB color_palette[8] = {CRGB::Red, CRGB::Orange, CRGB::Yellow, CRGB::LawnGreen, CRGB::Green, CRGB::Aqua, CRGB::Pink, CRGB::White};

void set_led_ring(uint8_t duty, uint8_t dir){
    CRGB wleds[NUM_LEDS];
    memset(wleds, 0 , sizeof(CRGB) * NUM_LEDS);     // sizeof(CRGB) = 3
    switch (dir){
        case DIR_STOP: wleds[0] = CRGB::Red; break;     // 停止は最下LEDが赤
        case DIR_FWD:  wleds[0] = CRGB::Green; break;   // 前進は最下LEDが緑
        case DIR_REV:  wleds[0] = CRGB::Blue; break;    // 後退は最下LEDが青
        default :wleds[0] = CRGB::Black; break;
    }
    uint8_t msb_led = duty >> 5;                // LEDの最大点灯数は８ビットのうち上位３ビット(0~8)
    msb_led += 2;                               // 2個目のLEDから開始する。

    //満了分は白で満たす
    for (uint8_t i = 2; i < msb_led ; i++){
        wleds[i] = CRGB::White;
    }
    uint8_t msb_color_index = (duty & 0b00011100) >> 2; //中位３ビットに８色を割り当てる
    wleds[msb_led] = color_palette[msb_color_index];
    // LEDリングを時計と共用したので、一番下が[6]になる。
    memcpy(&leds[0], &wleds[OFFSET_LEDS], (NUM_LEDS - OFFSET_LEDS) * sizeof(CRGB));
    memcpy(&leds[OFFSET_LEDS], &wleds[0], (OFFSET_LEDS) * sizeof(CRGB));
    FastLED.show();
    Serial.print(", RING:");
    Serial.print(msb_led);
    Serial.print("+");
    Serial.print(msb_color_index);
}

//====================================================================
//  走行スピード関連
//====================================================================
#define SPEED_SHIFT 3
#define PRESCALER 8     // (2^3)
#define SPEED_MAX (1<<(SPEED_SHIFT+PRESCALER))  // 2048

/* 最小Duty決定用の半固定抵抗を読む */
int16_t get_minimum_speed(void){
    uint16_t minimum_duty = analogRead(PIN_VOL_ADJ_MIN_SPEED);  // 最小DUTYのボリュームを読む
    minimum_duty >>= 1; // 1024 -> 512 (SPEED_MAX の1/4にする。)    
    return minimum_duty;
}

/* 走行中なら最小Dutyをオフセットする。調整時はそのままセット。 */
int16_t add_minimum_speed(uint16_t highres_speed, uint16_t offset){
    int16_t set_speed;             
    if (digitalRead(PIN_MODE_ADJ_MIN_SPEED) == LOW){        //調整モード時は調整ボリュームの値を入れる
        set_speed = offset;
    }else if(highres_speed > 0 ){                           //通常モード時で走行中だったら
        set_speed = highres_speed + offset;
    }else{
        set_speed = 0;
    }
    return set_speed;
}

/* 20ms毎に計算していた速度を、分周して255の範囲にシフトする。*/
uint8_t get_speed_duty(int16_t highres_speed){
    Serial.print("SPEED:");
    Serial.print(highres_speed);
    highres_speed >>= SPEED_SHIFT;      // 0～255の範囲にする。

    /* 範囲確認 */
    if (highres_speed > 255) highres_speed = 255;
    if (highres_speed < 0 ) highres_speed = 0;
    uint8_t byte_duty = (uint8_t)highres_speed;
    return byte_duty;
}

/* 20ms毎に速度を計算する */
int16_t calc_speed(int16_t current_speed, uint8_t handle, uint8_t p_mode){
    int16_t speed_power_pack_mode[6] = {0,16,32,64,128,255};
    int8_t pos = handle - 9;
    int16_t next_speed;
    if (p_mode == MODE_MASCON){
        /* ハンドル位置がノッチ5だったら+5, ブレーキ8だったら -8, EB (-9)だったら停止。 */
        next_speed = (pos < -8) ? 0 : current_speed + pos;       // [ToDo]速度に応じて加速度を変える。
    }else{
        next_speed = ( pos > 0 ) ? speed_power_pack_mode[pos] * PRESCALER : 0;
    }
    if (next_speed > SPEED_MAX) next_speed = SPEED_MAX;                  
    if (next_speed < 0) next_speed = 0;
    current_speed = next_speed;
    return next_speed;
}


void set_motor_pwm(uint8_t duty, uint8_t dir){
    switch (dir){
        case DIR_FWD :
            analogWrite(PIN_PWM1, duty);
            analogWrite(PIN_PWM2, 0);
            break;
        case DIR_REV :
            analogWrite(PIN_PWM1, 0);
            analogWrite(PIN_PWM2, duty);
            break;
        case DIR_STOP :
            analogWrite(PIN_PWM1, 0);
            analogWrite(PIN_PWM2, 0);
            break;
        default: break;
    }
    Serial.print(", Duty:");
    Serial.print(duty);
    Serial.print(", Dir:");
    Serial.print(dir);
}


//====================================================================
//  マスコン状態の取得関数
//  https://github.com/7m4mon/dengo_ts_emu
//====================================================================

#define HANDLE_CONTACT_1 0b0001
#define HANDLE_CONTACT_2 0b0010
#define HANDLE_CONTACT_3 0b0100
#define HANDLE_CONTACT_4 0b1000

#define MAP_NOTCH_OFF   0b0111
#define MAP_NOTCH_1   0b1110
#define MAP_NOTCH_2   0b0110
#define MAP_NOTCH_3   0b1011
#define MAP_NOTCH_4   0b0011
#define MAP_NOTCH_5   0b1010

#define MAP_BRAKE_OFF   0b1101
#define MAP_BRAKE_1   0b0111
#define MAP_BRAKE_2   0b0101
#define MAP_BRAKE_3   0b1110
#define MAP_BRAKE_4   0b1100
#define MAP_BRAKE_5   0b0110
#define MAP_BRAKE_6   0b0100
#define MAP_BRAKE_7   0b1011
#define MAP_BRAKE_8   0b1001
#define MAP_BRAKE_EMER    0b0000

uint8_t get_notch_state(uint8_t notch_position){
    uint8_t notch_state = 0xff;
    switch (notch_position){
        case MAP_NOTCH_OFF : notch_state = 0; break;
        case MAP_NOTCH_1 :  notch_state = 1; break;
        case MAP_NOTCH_2 :  notch_state = 2; break;
        case MAP_NOTCH_3 :  notch_state = 3; break;
        case MAP_NOTCH_4 :  notch_state = 4; break;
        case MAP_NOTCH_5 :  notch_state = 5; break;
        default : break;    //中途半端な状態は取得しない。
    }
    return notch_state;
}

uint8_t get_brake_state(uint8_t brake_position){
    uint8_t brake_state = 0xff;
    switch (brake_position){
        case MAP_BRAKE_OFF :  brake_state = 0; break;
        case MAP_BRAKE_1 :  brake_state = 1; break;
        case MAP_BRAKE_2 :  brake_state = 2; break;
        case MAP_BRAKE_3 :  brake_state = 3; break;
        case MAP_BRAKE_4 :  brake_state = 4; break;
        case MAP_BRAKE_5 :  brake_state = 5; break;
        case MAP_BRAKE_6 :  brake_state = 6; break;
        case MAP_BRAKE_7 :  brake_state = 7; break;
        case MAP_BRAKE_8 :  brake_state = 8; break;
        case MAP_BRAKE_EMER : brake_state = 9; break;
        default : break;    //中途半端な状態は取得しない。
    }
    return brake_state;
}

MasconState_t get_mascon_state(){
    // 戻り値を初期化
    MasconState_t current_mascon_state = {0xff,0xff,0xff,false,false,false,false,false};

    // PSコントローラの状態更新
    PSX.updateState(PSX_PAD1);

    // ノッチ状態の取得
    uint8_t notch_position = 0;
    if (IS_DOWN_LEFT(PSX_PAD1)) notch_position |= HANDLE_CONTACT_1;
    if (IS_DOWN_DOWN(PSX_PAD1)) notch_position |= HANDLE_CONTACT_2;
    if (IS_DOWN_RIGHT(PSX_PAD1)) notch_position |= HANDLE_CONTACT_3;
    if (IS_DOWN_TRIANGLE(PSX_PAD1)) notch_position |= HANDLE_CONTACT_4;
    current_mascon_state.notch = get_notch_state(notch_position);

    // ブレーキ状態の取得
    uint8_t brake_position = 0;
    if (IS_DOWN_R1(PSX_PAD1)) brake_position |= HANDLE_CONTACT_1;
    if (IS_DOWN_L1(PSX_PAD1)) brake_position |= HANDLE_CONTACT_2;
    if (IS_DOWN_R2(PSX_PAD1)) brake_position |= HANDLE_CONTACT_3;
    if (IS_DOWN_L2(PSX_PAD1)) brake_position |= HANDLE_CONTACT_4;
    current_mascon_state.brake = get_brake_state(brake_position);

    // ハンドルポジションは正常に取得できた場合のみ更新。
    if (current_mascon_state.brake != 0xff && current_mascon_state.notch != 0xff ){
        // ブレーキが入っていない場合のみノッチを返す。ブレーキ解除が9なのでオフセットする。
        current_mascon_state.handle = current_mascon_state.brake ? 9 - current_mascon_state.brake : current_mascon_state.notch + 9;
    }else{
        current_mascon_state.handle = 0xff;
    }

    //　ボタン状態の取得
    if (IS_DOWN_SQUARE(PSX_PAD1)) current_mascon_state.button_a = true;
    if (IS_DOWN_CROSS(PSX_PAD1)) current_mascon_state.button_b = true;
    if (IS_DOWN_CIRCLE(PSX_PAD1)) current_mascon_state.button_c = true;
    if (IS_DOWN_START(PSX_PAD1)) current_mascon_state.button_start = true;
    if (IS_DOWN_SELECT(PSX_PAD1)) current_mascon_state.button_select = true;

    return current_mascon_state;
}

//====================================================================
//  初期化処理
//====================================================================
void setup()
{
    // debug用出力
    Serial.begin(115200);

    // PSコントローラライブラリの初期化
    PSX.mode(PSX_PAD1, MODE_DIGITAL, MODE_LOCK);
    PSX.motorEnable(PSX_PAD1, MOTOR1_DISABLE, MOTOR2_DISABLE);

    //20msごとにコントローラーを読み取り
    MsTimer2::set(20, interrpt_proc); // 
    MsTimer2::start();

    // 7 Segment Displayの初期化
    display.setBrightness(0x0f);

    // LED RINGの初期化
    FastLED.addLeds<WS2812B, PIN_RING_DATA, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(32);

    // アナログピンをデジタル出力に設定（先にPull Upを無効にする)
    digitalWrite(PIN_PLAY1, LOW); 
    digitalWrite(PIN_PLAY2, LOW); 
    digitalWrite(PIN_PLAY3, LOW); 
    digitalWrite(PIN_PLAY4, LOW);
    digitalWrite(PIN_PLAY5, LOW); 
    pinMode(PIN_PLAY1, OUTPUT);
    pinMode(PIN_PLAY2, OUTPUT);
    pinMode(PIN_PLAY3, OUTPUT);
    pinMode(PIN_PLAY4, OUTPUT);
    pinMode(PIN_PLAY5, OUTPUT);
    pinMode(PIN_MODE_ADJ_MIN_SPEED, INPUT_PULLUP);
    pinMode(PIN_VOL_ADJ_MIN_SPEED, INPUT);

    // モーターの初期状態は停止
    set_motor_pwm(0,0);
}

//====================================================================
//  メインループ
//====================================================================
void loop()
{
    /* 全ての処理はタイマー割り込みで行う */
}

//====================================================================
//  20ms のタイマ割り込み
//====================================================================
#define BUTTON_HOLD_TIME 30 // *20ms
#define PIN_ONBOARD_LED 13
void interrpt_proc(){
    digitalWrite(PIN_ONBOARD_LED , HIGH);
    MasconState_t mascon_state = get_mascon_state();
    
    static int16_t highres_speed = 0;
    static uint8_t reverser_position = 0;
    static uint8_t reverser_direction = 0;
    static bool power_pack_mode = false;
    static uint8_t select_hold_counter = 0;
    static uint8_t start_hold_counter = 0;

    { /* マスコン状態関連 */ 
        /* ボタンが押されていたら連動してJQ6500からサウンドを再生 */
        digitalWrite(PIN_PLAY1, mascon_state.button_start);
        digitalWrite(PIN_PLAY2, mascon_state.button_select);
        digitalWrite(PIN_PLAY3, mascon_state.button_a);
        digitalWrite(PIN_PLAY4, mascon_state.button_b);
        digitalWrite(PIN_PLAY5, mascon_state.button_c);
        
        /* Directionとパワーパックモードは速度計算で使うので先に決定 */
        if (mascon_state.button_start){        // スタートは長押しでレバーサを変える（急に反転することを防止）
            start_hold_counter++;
            if (start_hold_counter == BUTTON_HOLD_TIME){
                reverser_position++;
                if (reverser_position > 3) reverser_position = 0;
                    reverser_direction = reverser_state[reverser_position];
                }else if (start_hold_counter > BUTTON_HOLD_TIME){
                    start_hold_counter = BUTTON_HOLD_TIME + 1; //オーバーフローの防止
            }else{
              ;
            }
        }else{  //押されていないとき
            start_hold_counter = 0;
        }

        if (mascon_state.button_select){        // セレクトは長押しでパワーパックモードに切り替え
            select_hold_counter++;
            if (select_hold_counter == BUTTON_HOLD_TIME){
                power_pack_mode = !power_pack_mode;
            }else if (select_hold_counter > BUTTON_HOLD_TIME){
                select_hold_counter = BUTTON_HOLD_TIME + 1; //オーバーフローの防止
            }else{
              ;
            }
        }else{  //押されていないとき
            select_hold_counter = 0;
        }
        
    }

    // マスコンハンドルの位置から速度を計算
    if (mascon_state.handle != 0xff && reverser_direction != DIR_STOP) {
        highres_speed = calc_speed(highres_speed, mascon_state.handle, power_pack_mode);
    }else if (reverser_direction == DIR_STOP || digitalRead(PIN_MODE_ADJ_MIN_SPEED) == LOW){  // 調整モード中は速度クリア。（調整モード明けで急加速しないように）
        highres_speed = 0;
    }


    { /* 処理を分散 */
        static uint8_t onetenth_counter = 0;
        static uint16_t pwm_duty = 0;
        int16_t offset_speed;
        /* 処理を分散する */
        if (onetenth_counter > 4) onetenth_counter = 0;
        switch (onetenth_counter){
            case 0 : offset_speed = get_minimum_speed();
                     if (highres_speed + offset_speed > SPEED_MAX)  highres_speed = SPEED_MAX - offset_speed;   //オフセットを足した時に、MAX SPEEDを大きく超えてしまい、減速を始めてもなかなか減り始めない。の対策
                     pwm_duty = get_speed_duty(add_minimum_speed(highres_speed, offset_speed)); 
                     break;
            case 1 : set_motor_pwm(pwm_duty, reverser_direction); break;
            case 2 : set_7seg_disp(pwm_duty, power_pack_mode); break; 
            case 3 : set_led_ring(pwm_duty, reverser_direction); break;
            case 4 :  Serial.print(", PMODE:");
                      Serial.print(power_pack_mode);
                      Serial.print(", Handle:"); 
                      Serial.println((int8_t)mascon_state.handle - 9); 
                      break;
            default : break;
        }
        onetenth_counter++;
    }
    digitalWrite(PIN_ONBOARD_LED , LOW);
}
