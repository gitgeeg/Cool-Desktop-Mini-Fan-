/**
 * 智能风扇控制器 - ESP12F
 * ===========================
 * 功能:
 *   - PT6302 VFD 8位显示屏驱动
 *   - EC11 旋转编码器 (旋转+按压+长按+双击)
 *   - PWM 风扇速度控制
 *   - WS2812B ARGB 灯光控制
 *   - 光电二极管自动亮度调节
 *   - 自然风模式 (山谷回风)
 *   - 设置菜单 (省电模式 / 手动亮度)
 */

#include <Arduino.h>
#include <FastLED.h>
#include <ESP8266WiFi.h>
#include <time.h>

// ============================================================
// 1. 引脚定义
// ============================================================
// VFD 显示屏 (PT6302)
#define VFD_DIN   14   // DA 数据输入
#define VFD_CLK   16   // CK 时钟
#define VFD_CS    15   // CS 片选
#define VFD_RST   13   // RS 复位

// EC11 旋转编码器 (按键通过 A/B 序列超时检测, 无独立GPIO)
#define EC11_A    4    // GA  A相 (GPIO4)
#define EC11_B    12   // BB  B相 (GPIO12)

// 风扇
#define FAN_PWM   2    // PWM 风扇调速
#define FAN_ARGB  5    // ARGB 灯控 (WS2812B)

// 光电二极管 (ESP12F ADC / TOUT)
#define PHOTO_ADC A0   // 模拟输入 0-1V

// GPIO0 按键 (默认高电平, 低电平=按下, 用于开关风扇+RGB)
#define BTN_GPIO0 0

// ============================================================
// 2. 常量定义
// ============================================================

// --- VFD 显示位数 ---
#define VFD_DIGITS 8

// --- VFD 自定义字符 (PT6302 CGRAM / ROM) ---
// 风速档位条: 1竖 ~ 5竖
#define VFD_CHAR_BAR1   0x11
#define VFD_CHAR_BAR2   0x12
#define VFD_CHAR_BAR3   0x13
#define VFD_CHAR_BAR4   0x14
#define VFD_CHAR_BAR5   0x15
// 箭头
#define VFD_CHAR_LARROW 0x0A  // 左箭头
#define VFD_CHAR_RARROW 0x0B  // 右箭头
// 最小风速全亮
#define VFD_CHAR_ALLON  0x1D
// 风车动画帧
#define VFD_CHAR_PIPE   0x7C  // '|'
#define VFD_CHAR_SLASH  0x2F  // '/'
#define VFD_CHAR_DASH   0x2D  // '-'
#define VFD_CHAR_BSLASH 0x5C  // '\'
// 空格与空白
#define VFD_CHAR_SPACE  0x20
#define VFD_CHAR_BLANK  0x10

// --- 风速档位 (13档: 0关, 1=2格, 2~12每档+3格, 12级满35格) ---
#define FAN_LEVELS      13    // 0~12档
#define FAN_DISPLAY_POS 7     // 显示档位条的位置数 (1-7位)
#define FAN_BARS_PER_POS 5    // 每位置5段 (BAR1~BAR5)
// 13档位→格子数: level0=0, level1=2, level_n(2~12)=2+3*(n-1)
#define FAN_LEVEL1_BARS 2     // 第一档格子数
#define FAN_BARS_PER_STEP 3   // 之后每档+3格

// --- ARGB 灯光颜色模式 ---
enum ArgbMode : uint8_t {
    ARGB_SPEED_COLOR = 0,  // 随风扇档位变速 (蓝→黄→红)
    ARGB_BLUE,             // 浅蓝
    ARGB_WHITE,            // 白
    ARGB_ORANGE,           // 亮橙
    ARGB_RED,              // 红
    ARGB_RGB_CYCLE,        // 转圈RGB(逐灯珠)
    ARGB_RGB_ALL_CYCLE,    // 全体一起变RGB
    ARGB_OFF,              // 关闭
    ARGB_MODE_COUNT
};

// --- ARGB 灯光电源状态 ---
enum ArgbPower : uint8_t {
    ARGB_POWER_ON = 0,
    ARGB_POWER_OFF
};

// --- ARGB 亮度模式 ---
enum ArgbBrightMode : uint8_t {
    ARGB_BRIGHT_AUTO = 0,  // 跟随环境光
    ARGB_BRIGHT_MANUAL     // 手动
};

// --- 菜单/页面状态 ---
enum Page : uint8_t {
    PAGE_HOME = 0,          // 首页 - 风扇速度显示
    PAGE_ARGB_CONTROL,      // ARGB 灯光控制
    PAGE_SETTINGS,          // 设置菜单
    PAGE_POWER_SAVE,        // 省电模式设置
    PAGE_BRIGHTNESS_MANUAL, // 手动亮度调节
    PAGE_TIME               // 时间显示页面
};

// --- 设置项索引 ---
#define SETTING_POWER_SAVE      0
#define SETTING_BRIGHTNESS      1
#define SETTING_TIME            2
#define SETTING_COUNT           3

// --- 风扇模式 ---
enum FanMode : uint8_t {
    FAN_CONSTANT = 0,   // 定速模式
    FAN_NATURAL         // 自然风模式 (仅山谷回风)
};

// --- 亮度模式 ---
enum BrightnessMode : uint8_t {
    BRIGHT_AUTO = 0,    // 自动亮度
    BRIGHT_MANUAL       // 手动亮度
};

// --- 时间常量 (ms) ---
#define LONG_PRESS_MS       200   // ≥500ms=长按, <500ms=单点
#define DOUBLE_CLICK_MS     400
#define IDLE_TIMEOUT_MS     6000  // 6s无操作回首页
#define POWER_SAVE_IDLE_MS  5000
#define SPINNER_INTERVAL_MS 200
#define FAN_ADJ_TIMEOUT_MS  550   // 调速箭头快速恢复为风车动画

// --- 光电二极管 ADC 阈值 (ESP8266 10-bit ADC: 0-1023 对应 0-1V) ---
// 380mV→389 | 310mV→317 | 280mV→287 | 50mV→51
#define ADC_DARKEST    51
#define ADC_BRIGHTEST  389   // 380mV时屏幕亮度已达最大

// --- 亮度映射 ---
#define VFD_BRIGHT_MIN  40
#define VFD_BRIGHT_MAX  255

// --- ARGB LED 数量 (共15个灯珠) ---
#define ARGB_NUM_LEDS 15

// ============================================================
// 3. 全局变量
// ============================================================

// --- VFD 显示缓冲区 ---
uint8_t vfdPrevBuffer[VFD_DIGITS];

// --- EC11 编码器状态 (双下降沿 + 先后顺序) ---
volatile int32_t  encPosition = 0;        // 累计旋转位置
volatile int8_t   encSeqState = 0;        // 0=空闲 1=GA先低 2=BB先低
volatile unsigned long encGaFallMs = 0;   // GA下降时刻(ms)
volatile unsigned long encPressDuration = 0; // 按键持续时间ms (0=无)
int32_t  encLastRead = 0;

// 按键处理 (loop中消费 encPressDuration)
unsigned long encLastClickTime = 0;
uint8_t  encClickCount = 0;
bool     encClickPending = false;

// --- 编码器事件 ---
enum EncEvent : uint8_t {
    ENC_NONE = 0,
    ENC_ROTATE_CW,
    ENC_ROTATE_CCW,
    ENC_SINGLE_CLICK,
    ENC_DOUBLE_CLICK,
    ENC_LONG_PRESS
};

// --- 系统状态 ---
Page            currentPage = PAGE_HOME;
Page            prevPage = PAGE_HOME;
FanMode         fanMode = FAN_CONSTANT;
uint8_t         fanSpeed = 4;           // 当前档位 0~12 (默认中档)
uint8_t         fanDisplaySpeed = 6;    // 定速模式显示用档位
uint8_t         fanDisplaySmoothed = 0; // 自然风平滑显示用的竖格数 (0~35)
uint8_t         fanCurrentPWM = 0;      // 当前实际 PWM (平滑过渡)
uint8_t         fanTargetPWM = 0;       // 目标 PWM
bool            fanAdjusting = false;
bool            fanAdjustDir = true;

// --- ARGB 状态 ---
ArgbPower      argbPower = ARGB_POWER_ON;
ArgbMode       argbCurrentMode = ARGB_SPEED_COLOR;
ArgbMode       argbPreviewMode = ARGB_SPEED_COLOR;
ArgbBrightMode argbBrightMode = ARGB_BRIGHT_AUTO;
uint8_t        argbBrightness = 28;
uint8_t        argbManualBrightness = 128;
uint8_t        argbPreviewBrightness = 128;
uint16_t       argbCountIndex = 0;
unsigned long  argbCycleLastMs = 0;
bool           argbBrightnessMode = false;
uint8_t        argbRenderHue = 0;
uint8_t        argbSpeedHue = 160;        // SPEED_COLOR平滑目标色相
bool           argbInArgbPage = false;
CRGB           argbLeds[ARGB_NUM_LEDS];

// --- 亮度控制 ---
BrightnessMode  brightMode = BRIGHT_AUTO;
uint8_t         vfdBrightness = 200;
uint8_t         vfdManualBrightness = 200;
bool            powerSaveEnabled = true;

// --- GPIO0 按键状态 (开关风扇+RGB) ---
bool            gpio0PowerOn = true;       // 当前电源状态
bool            gpio0LastState = true;     // 上次GPIO0电平 (默认高)
unsigned long   gpio0DebounceMs = 0;       // 防抖计时
uint8_t         gpio0SavedFanSpeed = 6;    // 保存的风扇档位 0~12
FanMode         gpio0SavedFanMode = FAN_CONSTANT; // 保存的风扇模式
ArgbMode       gpio0SavedArgbMode = ARGB_SPEED_COLOR; // 保存的ARGB模式
uint8_t        gpio0SavedArgbBrightness = 128;      // 保存的ARGB亮度

// --- 自然风状态 (仅山谷回风) ---
unsigned long   naturalCycleStart = 0;
unsigned long   valleyGustNext = 0;
unsigned long   valleyCalmNext = 0;
bool            valleyInGust = false;
bool            valleyInCalm = false;

// --- 计时器 ---
unsigned long   lastUserAction = 0;
unsigned long   lastSpinnerUpdate = 0;
uint8_t         spinnerFrame = 0;

// --- 设置菜单 ---
bool            timeDisplayEnabled = true;  // 时间显示开关
uint8_t         settingIndex = 0;

// ============================================================
// 4. VFD 显示驱动
// ============================================================

void vfdWriteByte(uint8_t data) {
    for (uint8_t i = 0; i < 8; i++) {
        digitalWrite(VFD_CLK, LOW);
        digitalWrite(VFD_DIN, (data & 0x01) ? HIGH : LOW);
        data >>= 1;
        digitalWrite(VFD_CLK, HIGH);
    }
}

void vfdShow() {
    digitalWrite(VFD_CS, LOW);
    vfdWriteByte(0xE8);
    digitalWrite(VFD_CS, HIGH);
}

void vfdInit() {
    digitalWrite(VFD_CS, LOW);
    vfdWriteByte(0xE0);
    delayMicroseconds(5);
    vfdWriteByte(0x07);  // 8位
    digitalWrite(VFD_CS, HIGH);
    delayMicroseconds(5);

    digitalWrite(VFD_CS, LOW);
    vfdWriteByte(0xE4);
    delayMicroseconds(5);
    vfdWriteByte(200);
    digitalWrite(VFD_CS, HIGH);
    delayMicroseconds(5);
}

void vfdSetBrightness(uint8_t brightness) {
    digitalWrite(VFD_CS, LOW);
    vfdWriteByte(0xE4);
    delayMicroseconds(5);
    vfdWriteByte(brightness);
    digitalWrite(VFD_CS, HIGH);
    vfdBrightness = brightness;
}

void vfdWriteChar(uint8_t pos, uint8_t chr) {
    digitalWrite(VFD_CS, LOW);
    vfdWriteByte(0x20 + pos);
    vfdWriteByte(chr);
    digitalWrite(VFD_CS, HIGH);
}

void vfdClear() {
    for (uint8_t i = 0; i < VFD_DIGITS; i++) {
        vfdWriteChar(i, VFD_CHAR_BLANK);
        vfdPrevBuffer[i] = 0xFF;
    }
    vfdShow();
}

void vfdSetBuffer(const uint8_t* buf) {
    bool changed = false;
    for (uint8_t i = 0; i < VFD_DIGITS; i++) {
        if (buf[i] != vfdPrevBuffer[i]) {
            vfdWriteChar(i, buf[i]);
            vfdPrevBuffer[i] = buf[i];
            changed = true;
        }
    }
    if (changed) vfdShow();
}

// ============================================================
// 5. EC11 编码器中断处理
// ============================================================
// 同时检测 GA/BB 下降沿, 按先后顺序判定旋转
//   右旋 CW: BB↓先 → GA↓后 (BB先低)
//   左旋 CCW: GA↓先 → BB↓后 (GA先低)
// 按键: GA↓后 BB始终为HIGH, GA↑时记录持续时间
//        < 500ms → 单点 | ≥ 500ms → 长按
//
// 状态: 0=空闲 1=GA先低 2=BB先低
// 旋转: BB↓先→GA↓后=CW | GA↓先→BB↓后=CCW
// 按键: GA↓→GA↑且BB始终HIGH, 时长<100ms单点 ≥100ms长按

void IRAM_ATTR encISR() {
    uint8_t a = digitalRead(EC11_A);
    uint8_t b = digitalRead(EC11_B);

    if (a == LOW && b == LOW) {
        if (encSeqState == 1) encPosition--;
        if (encSeqState == 2) encPosition++;
        encSeqState = 0;
    }
    else if (a == LOW && b == HIGH) {
        if (encSeqState == 0) { encSeqState = 1; encGaFallMs = millis(); }
    }
    else if (a == HIGH && b == LOW) {
        if (encSeqState == 0) encSeqState = 2;
        if (encSeqState == 1) encSeqState = 0;
    }
    else {
        if (encSeqState == 1) {
            unsigned long dur = millis() - encGaFallMs;
            if (dur >= 80) encPressDuration = dur;
        }
        encSeqState = 0;
    }
}

void encInit() {
    pinMode(EC11_A, INPUT_PULLUP);
    pinMode(EC11_B, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(EC11_A), encISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(EC11_B), encISR, CHANGE);
}

EncEvent encPoll() {
    unsigned long now = millis();
    static unsigned long lastEncEvent = 0;  // 统一防抖: 100ms内只记录首次操作

    // --- 1. 处理旋转 (位置变化) ---
    int32_t pos;
    noInterrupts();
    pos = encPosition;
    interrupts();

    if (pos != encLastRead) {
        int32_t old = encLastRead;
        encLastRead = pos; // 更新位置, 丢弃中间步数
        if (now - lastEncEvent >= 100) {
            EncEvent evt = (pos > old) ? ENC_ROTATE_CW : ENC_ROTATE_CCW;
            lastEncEvent = now;
            lastUserAction = now;
            return evt;
        }
    }

    // --- 2. 处理按键 (ISR记录的持续时间) ---
    unsigned long pressDur;
    noInterrupts();
    pressDur = encPressDuration;
    encPressDuration = 0;
    interrupts();

    if (pressDur > 0 && (now - lastEncEvent >= 100)) {
        lastEncEvent = now;
        lastUserAction = now;
        if (pressDur >= LONG_PRESS_MS) {
            encClickPending = false;
            encClickCount = 0;
            return ENC_LONG_PRESS;
        } else {
            encClickCount++;
            if (encClickCount == 1) {
                encLastClickTime = now;
                encClickPending = true;
            } else if (encClickCount >= 2) {
                encClickPending = false;
                encClickCount = 0;
                return ENC_DOUBLE_CLICK;
            }
        }
    }

    // --- 3. 单击超时判定 ---
    if (encClickPending && (now - encLastClickTime > DOUBLE_CLICK_MS)) {
        encClickPending = false;
        encClickCount = 0;
        lastEncEvent = now;
        lastUserAction = now;
        return ENC_SINGLE_CLICK;
    }

    return ENC_NONE;
}

// ============================================================
// 6. 风扇 PWM 控制 (35档 + 平滑变速)
// ============================================================

void fanInit() {
    pinMode(FAN_PWM, OUTPUT);
    analogWriteFreq(18000);  // 25kHz 静音 ******
    analogWrite(FAN_PWM, 0);
    fanCurrentPWM = 0;
    fanTargetPWM = 0;
}

/**
 * 设置风扇目标档位 (0-11), PWM 平滑过渡在 fanSmoothUpdate() 中完成
 */
void fanSetSpeed(uint8_t level) {
    if (level > 12) level = 12;
    fanTargetPWM = (level == 0) ? 0 : map(level, 1, 12, 20, 255);
    fanDisplaySpeed = level;
}

/**
 * 设置目标百分比 (0-100), 用于自然风模式
 * 显示档位由 fanCurrentPWM 实时反算, 确保平滑
 */
void fanSetTargetPercent(uint8_t percent) {
    if (percent > 100) percent = 100;
    fanTargetPWM = (percent == 0) ? 0 : map(percent, 1, 100, 20, 255);
}

/**
 * 将当前 PWM 反算为显示档位 (0-11)
 */
uint8_t pwmToDisplayLevel(uint8_t pwm) {
    if (pwm == 0) return 0;
    return constrain(map(pwm, 20, 255, 1, 12), 1, 12);
}

/**
 * 平滑过渡 PWM (每帧调用, 渐进式变速)
 */
void fanSmoothUpdate() {
    if (fanCurrentPWM == fanTargetPWM) return;

    int16_t diff = (int16_t)fanTargetPWM - (int16_t)fanCurrentPWM;
    int16_t step;

    // 步长: 加速快一些, 减速慢一些(更自然)
    if (abs(diff) > 50) {
        step = (diff > 0) ? 8 : -5;   // 大步快调
    } else if (abs(diff) > 10) {
        step = (diff > 0) ? 4 : -2;   // 中步
    } else {
        step = (diff > 0) ? 1 : -1;   // 微步精调
    }

    fanCurrentPWM = constrain(fanCurrentPWM + step, 0, 255);
    analogWrite(FAN_PWM, fanCurrentPWM);
}

/**
 * 立即设置 PWM (跳过平滑)
 */
void fanSetRawSpeed(uint8_t speed) {
    fanCurrentPWM = speed;
    fanTargetPWM = speed;
    analogWrite(FAN_PWM, speed);
}

/**
 * 设置目标百分比 (已废弃, 用 fanSetTargetPercent)
 */
void fanSetPercent(uint8_t percent) {
    fanSetTargetPercent(percent);
}

// ============================================================
// 7. ARGB 灯光控制 (WS2812B)
// ============================================================
// 前向声明 (第8节定义)
uint16_t photoRead();
uint8_t photoToBrightness(uint16_t adc);

void argbInit() {
    FastLED.addLeds<WS2812B, FAN_ARGB, GRB>(argbLeds, ARGB_NUM_LEDS);
    FastLED.setBrightness(128);
    FastLED.clear();
    ets_intr_lock();
    FastLED.show();
    delayMicroseconds(100);
    FastLED.show();
    ets_intr_unlock();
}

/** 全部熄灭 (硬件级中断锁+双重发送) */
void argbAllOff() {
    FastLED.clear();
    uint8_t pwmVal = fanCurrentPWM;
    digitalWrite(FAN_PWM, LOW);
    ets_intr_lock();
    FastLED.show();
    delayMicroseconds(100);
    FastLED.show();
    ets_intr_unlock();
    analogWrite(FAN_PWM, pwmVal);
}

/** 根据模式刷新所有灯珠 */
void argbRender(ArgbMode mode, uint8_t brightness) {
    FastLED.setBrightness(brightness);
    FastLED.clear();

    switch (mode) {
        case ARGB_SPEED_COLOR:
            // 风扇档位→颜色: 低=蓝(160°) 中=黄(64°) 高=红(0°), 3s完成全程平滑
            {
                uint8_t target = map(fanCurrentPWM, 0, 255, 160, 0);
                // 每200ms步进10, 全程160/10=16步=3.2s≈3s
                int16_t diff = (int16_t)target - (int16_t)argbSpeedHue;
                if (diff > 0) { argbSpeedHue += (diff > 10) ? 10 : diff; }
                else if (diff < 0) { argbSpeedHue -= ((-diff) > 10) ? 10 : (-diff); }
                fill_solid(argbLeds, ARGB_NUM_LEDS, CHSV(argbSpeedHue, 255, 255));
            }
            break;
        case ARGB_BLUE:
            fill_solid(argbLeds, ARGB_NUM_LEDS, CRGB(0, 255, 200)); 
            break;
        case ARGB_ORANGE:
            fill_solid(argbLeds, ARGB_NUM_LEDS, CRGB(255, 60, 0)); 
            break;
        case ARGB_WHITE:
            fill_solid(argbLeds, ARGB_NUM_LEDS, CRGB::White);
            break;
        case ARGB_RED:
            fill_solid(argbLeds, ARGB_NUM_LEDS, CRGB::Red);
            break;
        case ARGB_RGB_CYCLE:
            for (uint16_t i = 0; i < ARGB_NUM_LEDS; i++) {
                argbLeds[i] = CHSV((uint8_t)(argbRenderHue + i * (255 / ARGB_NUM_LEDS)), 255, 255);
            }
            break;
        case ARGB_RGB_ALL_CYCLE:
            fill_solid(argbLeds, ARGB_NUM_LEDS, CHSV(argbRenderHue, 255, 255));
            break;
        case ARGB_OFF:
            break;
        default: break;
    }
    // 硬件级中断锁 + 临时静默PWM引脚 + 双重发送抗噪声
    uint8_t pwmVal = fanCurrentPWM;            // 保存当前PWM
    digitalWrite(FAN_PWM, LOW);                // PWM引脚拉低, 减少噪声
    ets_intr_lock();                           // 硬件级关全部中断
    FastLED.show();                            // 第一次发送
    delayMicroseconds(100);                    // 等待噪声消退
    FastLED.show();                            // 第二次发送 (覆盖残留错误)
    ets_intr_unlock();                         // 恢复中断
    analogWrite(FAN_PWM, pwmVal);              // 恢复PWM
}

static unsigned long argbLastRender = 0;  // 统一渲染计时

/** 切换模式/页面时重置渲染定时器, 确保立即响应 */
void argbResetRenderTimer() { argbLastRender = 0; }

/** RGB色相 + SPEED_COLOR定时刷新 (统一计时器, 无延迟) */
void argbCycleUpdate() {
    unsigned long now = millis();
    if (currentPage == PAGE_ARGB_CONTROL || argbPower != ARGB_POWER_ON) return;

    // 统一计时: RGB 50ms, SPEED_COLOR 200ms, 其余不刷
    uint16_t interval = 200;
    if (argbCurrentMode == ARGB_RGB_CYCLE || argbCurrentMode == ARGB_RGB_ALL_CYCLE) interval = 50;
    else if (argbCurrentMode != ARGB_SPEED_COLOR) return;

    if (now - argbLastRender < interval) return;
    argbLastRender = now;

    if (argbCurrentMode == ARGB_RGB_CYCLE || argbCurrentMode == ARGB_RGB_ALL_CYCLE) {
        argbRenderHue++;
    }
    argbRender(argbCurrentMode, argbBrightness);
}

/** ARGB 亮度自动更新 (跟随环境光, 关中断刷新) */
void argbBrightnessUpdate() {
    if (argbBrightMode != ARGB_BRIGHT_AUTO) return;
    static unsigned long lastUpdate = 0;
    unsigned long now = millis();
    if (now - lastUpdate < 300) return;  // 加快到300ms
    lastUpdate = now;

    uint8_t target = photoToBrightness(photoRead());
    // 步进3, 加速响应
    if (argbBrightness < target) argbBrightness += min(3, target - argbBrightness);
    else if (argbBrightness > target) argbBrightness -= min(3, argbBrightness - target);

    FastLED.setBrightness(argbBrightness);
    if (currentPage == PAGE_ARGB_CONTROL) {
        if (argbBrightnessMode) argbPreviewBrightness = argbBrightness;
    } else {
        uint8_t pwmVal = fanCurrentPWM;
        digitalWrite(FAN_PWM, LOW);
        ets_intr_lock();
        FastLED.show();
        delayMicroseconds(100);
        FastLED.show();
        ets_intr_unlock();
        analogWrite(FAN_PWM, pwmVal);
    }
}

// ============================================================
// 8. 光电二极管 & 自动亮度 (3秒滑动平均)
// ============================================================

#define ADC_SAMPLE_MS   1000  // 每1s采样一次
#define ADC_WINDOW      3     // 3s / 1s = 3个样本

static uint16_t adcBuffer[ADC_WINDOW] = {0};
static uint8_t  adcIndex = 0;
static uint16_t adcSum = 0;
static uint8_t  adcCount = 0;

/**
 * 读取光电二极管 ADC (3秒滑动平均)
 */
uint16_t photoRead() {
    static unsigned long lastSample = 0;
    unsigned long now = millis();
    if (now - lastSample >= ADC_SAMPLE_MS) {
        lastSample = now;
        uint16_t raw = analogRead(PHOTO_ADC);
        if (adcCount < ADC_WINDOW) {
            adcSum += raw;
            adcCount++;
        } else {
            adcSum = adcSum - adcBuffer[adcIndex] + raw;
        }
        adcBuffer[adcIndex] = raw;
        adcIndex = (adcIndex + 1) % ADC_WINDOW;
    }
    if (adcCount == 0) return analogRead(PHOTO_ADC);
    return adcSum / adcCount;
}

uint8_t photoToBrightness(uint16_t adc) {
    if (adc <= ADC_DARKEST)   return VFD_BRIGHT_MIN;
    if (adc >= ADC_BRIGHTEST) return VFD_BRIGHT_MAX;
    return map(adc, ADC_DARKEST, ADC_BRIGHTEST, VFD_BRIGHT_MIN, VFD_BRIGHT_MAX);
}

uint8_t getPowerSaveBrightness(uint16_t adc) {
    uint8_t b = (uint16_t)photoToBrightness(adc) * 40 / 100;  // 当前亮度40%
    return (b < 10) ? 10 : b;
}

void brightnessUpdate() {
    static unsigned long lastUpdate = 0;
    static uint8_t lastTarget = 0;
    unsigned long now = millis();
    if (now - lastUpdate < 200) return;
    lastUpdate = now;

    if (brightMode != BRIGHT_AUTO) return;

    uint16_t adc = photoRead();
    uint8_t target = (powerSaveEnabled && (now - lastUserAction > POWER_SAVE_IDLE_MS))
                     ? getPowerSaveBrightness(adc)
                     : photoToBrightness(adc);

    // 平滑过渡亮度 (渐进, 避免闪烁)
    if (target != lastTarget) {
        int16_t diff = (int16_t)target - (int16_t)vfdBrightness;
        uint8_t step;
        if (abs(diff) > 30)      step = 6;
        else if (abs(diff) > 10) step = 3;
        else if (abs(diff) > 2)  step = 1;
        else {
            vfdSetBrightness(target);
            lastTarget = target;
            return;
        }
        uint8_t newBright = (diff > 0) ? vfdBrightness + step
                                       : vfdBrightness - step;
        vfdSetBrightness(newBright);
    }
}

// ============================================================
// 9. 风车旋转动画
// ============================================================

const uint8_t SPINNER_CHARS[] = {
    VFD_CHAR_PIPE, VFD_CHAR_SLASH, VFD_CHAR_DASH, VFD_CHAR_BSLASH
};  // spinner chars: | / - backslash

void spinnerUpdate() {
    unsigned long now = millis();
    if (now - lastSpinnerUpdate >= SPINNER_INTERVAL_MS) {
        lastSpinnerUpdate = now;
        spinnerFrame = (spinnerFrame + 1) % 4;
    }
}

// ============================================================
// 10. 自然风模式算法 (仅山谷回风)
// ============================================================
uint8_t naturalValleyWind() {
    unsigned long now = millis();

    if (valleyGustNext == 0) {
        valleyGustNext = now + random(30000, 60000);
        valleyCalmNext = now + random(120000, 180000);
    }

    // 风歇期
    if (!valleyInCalm && now >= valleyCalmNext) {
        valleyInCalm = true;
        valleyCalmNext = now + random(120000, 180000);
    }
    if (valleyInCalm) {
        if (now - valleyCalmNext + random(120000, 180000) > 8000) valleyInCalm = false;
        else return 10;
    }

    // 阵风
    if (!valleyInGust && now >= valleyGustNext) {
        valleyInGust = true;
        valleyGustNext = now + random(30000, 60000);
    }
    if (valleyInGust) {
        if (now - valleyGustNext + random(30000, 60000) > 3000) valleyInGust = false;
        else return random(70, 81);
    }

    // 基础微调
    static unsigned long lastMicro = 0;
    static uint8_t microSpeed = 30;
    if (now - lastMicro > 3000) {
        lastMicro = now;
        microSpeed = constrain(30 + random(-15, 16), 15, 45);
    }
    return microSpeed;
}

uint8_t naturalWindGetSpeed() {
    return naturalValleyWind();
}

void naturalWindUpdate() {
    static unsigned long lastUpdate = 0;
    unsigned long now = millis();
    if (now - lastUpdate < 200) return;
    lastUpdate = now;

    uint8_t pct = naturalWindGetSpeed();
    fanSetTargetPercent(pct);
    // fanDisplaySpeed 已在 fanSetTargetPercent 中更新为 0-35 档位映射
}

// ============================================================
// 11. 显示更新函数
// ============================================================

/**
 * 将档位(0-11)映射到7位VFD档位条显示
 * level0=全亮(最小风), level1=2格, level2~11每档+3格
 * 每位有5段(BAR1~BAR5)
 */
void fillBarBuffer(uint8_t* buf, uint8_t level) {
    // 复位显示位置1-7为空格
    for (uint8_t i = 1; i < VFD_DIGITS; i++) buf[i] = VFD_CHAR_SPACE;

    if (level == 0) {
        // 最小风速: 全部显示全亮字符
        for (uint8_t i = 0; i < VFD_DIGITS; i++) buf[i] = VFD_CHAR_ALLON;
        return;
    }

    // 计算总格子数: level1=2格, level_n(2~11)=2+3*(n-1)
    uint8_t totalBars = FAN_LEVEL1_BARS + (level - 1) * FAN_BARS_PER_STEP;
    if (totalBars > FAN_DISPLAY_POS * FAN_BARS_PER_POS) totalBars = FAN_DISPLAY_POS * FAN_BARS_PER_POS;

    uint8_t fullPos = totalBars / FAN_BARS_PER_POS;    // 完整填满的位置数
    uint8_t partial = totalBars % FAN_BARS_PER_POS;    // 部分填充 (0~5)

    for (uint8_t i = 0; i < FAN_DISPLAY_POS; i++) {
        uint8_t pos = i + 1;  // 显示位置 1-7
        if (i < fullPos) {
            buf[pos] = VFD_CHAR_BAR5;                  // 满格
        } else if (i == fullPos && partial > 0) {
            buf[pos] = VFD_CHAR_BAR1 + partial - 1;    // 部分填充 BAR1~BAR5
        }
        // else: 已初始化为空格
    }
}

/** 直接填充N个竖格 (0~35), 用于自然风平滑显示 */
void fillBarCount(uint8_t* buf, uint8_t count) {
    for (uint8_t i = 1; i < VFD_DIGITS; i++) buf[i] = VFD_CHAR_SPACE;
    if (count == 0) {
        for (uint8_t i = 0; i < VFD_DIGITS; i++) buf[i] = VFD_CHAR_ALLON;
        return;
    }
    if (count > 35) count = 35;
    uint8_t fullPos = count / FAN_BARS_PER_POS;
    uint8_t partial = count % FAN_BARS_PER_POS;
    for (uint8_t i = 0; i < FAN_DISPLAY_POS; i++) {
        uint8_t pos = i + 1;
        if (i < fullPos) {
            buf[pos] = VFD_CHAR_BAR5;
        } else if (i == fullPos && partial > 0) {
            buf[pos] = VFD_CHAR_BAR1 + partial - 1;
        }
    }
}

void displayHome() {
    uint8_t buf[VFD_DIGITS];

    if (!gpio0PowerOn) {
        // 关闭状态: 8位全显示0x1D
        memset(buf, VFD_CHAR_ALLON, VFD_DIGITS);
        vfdSetBuffer(buf);
        return;
    }

    memset(buf, VFD_CHAR_SPACE, VFD_DIGITS);
    spinnerUpdate();

    if (fanMode == FAN_CONSTANT) {
        // 定速模式: 第0位显示箭头或风车, 13档分组显示
        buf[0] = fanAdjusting ? (fanAdjustDir ? VFD_CHAR_RARROW : VFD_CHAR_LARROW)
                              : SPINNER_CHARS[spinnerFrame];
        fillBarBuffer(buf, fanDisplaySpeed);
    } else {
        // 自然风模式: 第0位显示V, 一竖一档平滑变化
        buf[0] = 'V';
        // 将PWM(0~255)直接映射到0~35格
        uint8_t barTarget = (fanCurrentPWM == 0) ? 0 : map(fanCurrentPWM, 20, 255, 1, 35);
        if (barTarget > 35) barTarget = 35;

        // 平滑追随 (每100ms步进1格)
        static unsigned long lastNatSmooth = 0;
        if (millis() - lastNatSmooth >= 100) {
            lastNatSmooth = millis();
            if (fanDisplaySmoothed < barTarget) fanDisplaySmoothed++;
            else if (fanDisplaySmoothed > barTarget) fanDisplaySmoothed--;
        }
        fillBarCount(buf, fanDisplaySmoothed);
    }
    vfdSetBuffer(buf);
}

void displayArgbControl() {
    uint8_t buf[VFD_DIGITS];
    memset(buf, VFD_CHAR_SPACE, VFD_DIGITS);

    if (argbBrightnessMode) {
        // 亮度设置: "br XX" + A/M (自动模式实时跟随环境光)
        buf[0] = 'b'; buf[1] = 'r'; buf[2] = ' ';
        uint8_t pct = ((argbBrightMode == ARGB_BRIGHT_AUTO) ? argbBrightness : argbPreviewBrightness) * 100 / 255;
        if (pct >= 100) {
            buf[3] = '1'; buf[4] = '0'; buf[5] = '0';
        } else if (pct >= 10) {
            buf[3] = '0' + (pct / 10); buf[4] = '0' + (pct % 10);
        } else {
            buf[3] = ' '; buf[4] = '0' + pct;
        }
        buf[6] = ' ';
        buf[7] = (argbBrightMode == ARGB_BRIGHT_AUTO) ? 'A' : 'M';
    } else {
        // 模式选择
        const char* names[] = {"SPED", "BLUE", "WHTE", "ORGE", "RED", "RGB", "ALLR", "OFF"};
        uint8_t len = strlen(names[argbPreviewMode]);
        memcpy(buf, names[argbPreviewMode], len);
        buf[6] = 'b';
        buf[7] = '0' + (argbPreviewBrightness / 26);
    }
    vfdSetBuffer(buf);
}

void displaySettings() {
    uint8_t buf[VFD_DIGITS];
    memset(buf, VFD_CHAR_SPACE, VFD_DIGITS);
    if (settingIndex == SETTING_POWER_SAVE) {
        buf[0] = 'P'; buf[1] = 'S'; buf[2] = ' '; buf[3] = ' ';
        if (powerSaveEnabled) { buf[4] = 'O'; buf[5] = 'N'; }
        else { buf[4] = 'O'; buf[5] = 'F'; buf[6] = 'F'; }
    } else if (settingIndex == SETTING_BRIGHTNESS) {
        buf[0] = 'B'; buf[1] = 'r'; buf[2] = 'i'; buf[3] = ' ';
        if (brightMode == BRIGHT_AUTO) { buf[4] = 'A'; buf[5] = 'u'; buf[6] = 't'; buf[7] = 'o'; }
        else { buf[4] = 'M'; buf[5] = 'a'; buf[6] = 'n'; buf[7] = 'u'; }
    } else {
        // "t On" / "t OFF"
        buf[0] = 'T'; buf[1] = 'I';buf[2] = 'M';buf[3] = 'E';buf[4] = ' ';
        if (timeDisplayEnabled) { buf[6] = 'O'; buf[7] = 'N'; }
        else { buf[5] = 'O'; buf[6] = 'F'; buf[7] = 'F'; }
    }
    vfdSetBuffer(buf);
}

void displayPowerSave() {
    uint8_t buf[VFD_DIGITS];
    memset(buf, VFD_CHAR_SPACE, VFD_DIGITS);
    buf[0] = 'P'; buf[1] = 'S'; buf[2] = ' ';
    if (powerSaveEnabled) {
        buf[3] = 'O'; buf[4] = 'N';
    } else {
        buf[3] = 'O'; buf[4] = 'F'; buf[5] = 'F';
    }
    vfdSetBuffer(buf);
}

void displayBrightnessManual() {
    uint8_t buf[VFD_DIGITS];
    memset(buf, VFD_CHAR_SPACE, VFD_DIGITS);
    buf[0] = 'b'; buf[1] = 'r'; buf[2] = ':';
    // 自动模式用实时亮度, 手动模式用手动设定值
    uint8_t pct = (brightMode == BRIGHT_AUTO ? vfdBrightness : vfdManualBrightness) * 100 / 255;
    if (pct >= 100) {
        buf[3] = '1'; buf[4] = '0'; buf[5] = '0'; buf[6] = '%';
    } else if (pct >= 10) {
        buf[3] = ' '; buf[4] = '0' + (pct / 10); buf[5] = '0' + (pct % 10); buf[6] = '%';
    } else {
        buf[3] = ' '; buf[4] = ' '; buf[5] = '0' + pct; buf[6] = '%';
    }
    buf[7] = (brightMode == BRIGHT_AUTO) ? 'A' : 'M';
    vfdSetBuffer(buf);
}

void displayTime() {
    uint8_t buf[VFD_DIGITS];
    memset(buf, VFD_CHAR_SPACE, VFD_DIGITS);
    time_t now = time(nullptr);
    struct tm* info = localtime(&now);
    if (info->tm_year > 100) {  // 时间已同步
        buf[0] = '0' + (info->tm_hour / 10);
        buf[1] = '0' + (info->tm_hour % 10);
        buf[2] = 0x3A;  // 冒号 ":"
        buf[3] = '0' + (info->tm_min / 10);
        buf[4] = '0' + (info->tm_min % 10);
        buf[5] = 0x3A;
        buf[6] = '0' + (info->tm_sec / 10);
        buf[7] = '0' + (info->tm_sec % 10);
    } else {
        buf[0] = '-'; buf[1] = '-'; buf[2] = ':'; buf[3] = '-';
        buf[4] = '-'; buf[5] = ':'; buf[6] = '-'; buf[7] = '-';
    }
    vfdSetBuffer(buf);
}

void displayUpdate() {
    switch (currentPage) {
        case PAGE_HOME:              displayHome(); break;
        case PAGE_ARGB_CONTROL:      displayArgbControl(); break;
        case PAGE_SETTINGS:          displaySettings(); break;
        case PAGE_POWER_SAVE:        displayPowerSave(); break;
        case PAGE_BRIGHTNESS_MANUAL: displayBrightnessManual(); break;
        case PAGE_TIME:              displayTime(); break;
    }
}

// ============================================================
// 12. 菜单逻辑处理
// ============================================================

void switchPage(Page newPage) {
    prevPage = currentPage;
    currentPage = newPage;
    memset(vfdPrevBuffer, 0xFF, VFD_DIGITS);

    if (newPage == PAGE_ARGB_CONTROL) {
        argbPreviewMode = argbCurrentMode;
        argbPreviewBrightness = argbBrightness;
        argbBrightnessMode = false;
        argbInArgbPage = true;
    } else {
        argbInArgbPage = false;
        // 离开ARGB页时重置渲染定时器, 立即生效
        argbResetRenderTimer();
    }
}

void handleHomeEvent(EncEvent event) {
    if (!gpio0PowerOn) {
        // 关闭状态: 旋转/单点无效, 双击/长按可进入菜单
        switch (event) {
            case ENC_DOUBLE_CLICK:
                switchPage(PAGE_ARGB_CONTROL);
                break;
            case ENC_LONG_PRESS:
                settingIndex = 0;
                switchPage(PAGE_SETTINGS);
                break;
            default: break;
        }
        return;
    }

    switch (event) {
        case ENC_ROTATE_CW:
            if (fanMode == FAN_CONSTANT) {
                fanAdjusting = true; fanAdjustDir = true;
                if (fanSpeed < 12) { fanSpeed++; fanSetSpeed(fanSpeed); }
            }
            break;
        case ENC_ROTATE_CCW:
            if (fanMode == FAN_CONSTANT) {
                fanAdjusting = true; fanAdjustDir = false;
                if (fanSpeed > 0) { fanSpeed--; fanSetSpeed(fanSpeed); }
            }
            break;
        case ENC_SINGLE_CLICK:
            if (fanMode == FAN_CONSTANT) {
                fanMode = FAN_NATURAL;
                naturalCycleStart = millis();
                valleyGustNext = valleyCalmNext = 0;
                valleyInGust = valleyInCalm = false;
                fanDisplaySmoothed = (fanCurrentPWM == 0) ? 0 : map(fanCurrentPWM, 20, 255, 1, 35);
            } else {
                fanMode = FAN_CONSTANT;
                fanSetSpeed(fanSpeed);
            }
            break;
        case ENC_DOUBLE_CLICK:
            switchPage(PAGE_ARGB_CONTROL);
            break;
        case ENC_LONG_PRESS:
            settingIndex = 0;
            switchPage(PAGE_SETTINGS);
            break;
        default: break;
    }
}

void handleArgbEvent(EncEvent event) {
    if (argbBrightnessMode) {
        // --- 亮度设置子模式 ---
        switch (event) {
            case ENC_ROTATE_CW:
                if (argbBrightMode == ARGB_BRIGHT_MANUAL) {
                    argbPreviewBrightness = min(255, argbPreviewBrightness + 8);
                }
                argbRender(argbPreviewMode, argbPreviewBrightness);
                break;
            case ENC_ROTATE_CCW:
                if (argbBrightMode == ARGB_BRIGHT_MANUAL) {
                    argbPreviewBrightness = max(5, (int)argbPreviewBrightness - 8);
                }
                argbRender(argbPreviewMode, argbPreviewBrightness);
                break;
            case ENC_SINGLE_CLICK:
                // 单点: 切换自动/手动亮度
                argbBrightMode = (argbBrightMode == ARGB_BRIGHT_AUTO) ? ARGB_BRIGHT_MANUAL : ARGB_BRIGHT_AUTO;
                if (argbBrightMode == ARGB_BRIGHT_MANUAL) {
                    argbPreviewBrightness = argbBrightness;
                }
                break;
            case ENC_LONG_PRESS:
                // 长按: 保存亮度退出
                // 保存前重置渲染定时器
                argbResetRenderTimer();
                argbBrightnessMode = false;
                if (argbBrightMode == ARGB_BRIGHT_MANUAL) {
                    argbManualBrightness = argbPreviewBrightness;
                }
                argbBrightness = argbPreviewBrightness;
                argbCurrentMode = argbPreviewMode;
                argbRender(argbCurrentMode, argbBrightness);
                switchPage(PAGE_HOME);
                break;
            default: break;
        }
    } else {
        // --- 模式选择 (仅长按可退出) ---
        switch (event) {
            case ENC_ROTATE_CW:
                argbPreviewMode = (ArgbMode)((argbPreviewMode + 1) % ARGB_MODE_COUNT);
                argbRender(argbPreviewMode, argbPreviewBrightness);
                break;
            case ENC_ROTATE_CCW:
                argbPreviewMode = (ArgbMode)((argbPreviewMode == 0) ? ARGB_MODE_COUNT - 1
                                                                        : argbPreviewMode - 1);
                argbRender(argbPreviewMode, argbPreviewBrightness);
                break;
            case ENC_SINGLE_CLICK:
                argbBrightnessMode = true;
                argbPreviewBrightness = argbBrightness;
                break;
            case ENC_LONG_PRESS:
                argbResetRenderTimer();
                argbCurrentMode = argbPreviewMode;
                argbBrightness = argbPreviewBrightness;
                argbRender(argbCurrentMode, argbBrightness);
                switchPage(PAGE_HOME);
                break;
            default: break;
        }
    }
}

void handleSettingsEvent(EncEvent event) {
    switch (event) {
        case ENC_ROTATE_CW:
            settingIndex = (settingIndex + 1) % SETTING_COUNT;
            break;
        case ENC_ROTATE_CCW:
            settingIndex = (settingIndex == 0) ? SETTING_COUNT - 1 : settingIndex - 1;
            break;
        case ENC_SINGLE_CLICK:
            if (settingIndex == SETTING_POWER_SAVE) switchPage(PAGE_POWER_SAVE);
            else if (settingIndex == SETTING_BRIGHTNESS) switchPage(PAGE_BRIGHTNESS_MANUAL);
            else timeDisplayEnabled = !timeDisplayEnabled;  // 时间开关: 单点切换
            break;
        case ENC_LONG_PRESS:
            switchPage(PAGE_HOME);
            break;
        default: break;
    }
}

void handlePowerSaveEvent(EncEvent event) {
    switch (event) {
        case ENC_ROTATE_CW:
        case ENC_ROTATE_CCW:
            powerSaveEnabled = !powerSaveEnabled;
            break;
        case ENC_SINGLE_CLICK:
        case ENC_LONG_PRESS:
            switchPage(PAGE_SETTINGS);
            break;
        default: break;
    }
}

void handleBrightnessManualEvent(EncEvent event) {
    switch (event) {
        case ENC_ROTATE_CW:
            if (brightMode == BRIGHT_MANUAL) {
                vfdManualBrightness = min(255, vfdManualBrightness + 5);
                vfdSetBrightness(vfdManualBrightness);
            }
            break;
        case ENC_ROTATE_CCW:
            if (brightMode == BRIGHT_MANUAL) {
                vfdManualBrightness = max(5, (int)vfdManualBrightness - 5);
                vfdSetBrightness(vfdManualBrightness);
            }
            break;
        case ENC_SINGLE_CLICK:
            // 单点: 切换回自动亮度模式, 返回设置菜单
            brightMode = BRIGHT_AUTO;
            switchPage(PAGE_SETTINGS);
            break;
        case ENC_LONG_PRESS:
            // 长按: 进入手动模式 / 确认手动亮度并退出
            if (brightMode == BRIGHT_AUTO) {
                // 进入手动模式, 开始调节
                brightMode = BRIGHT_MANUAL;
                vfdManualBrightness = vfdBrightness;
                vfdSetBrightness(vfdManualBrightness);
            } else {
                // 确认手动亮度, 返回设置菜单
                switchPage(PAGE_SETTINGS);
            }
            break;
        default: break;
    }
}

void handleEncEvent(EncEvent event) {
    if (event == ENC_NONE) return;

    // 打印操作事件
    switch (event) {
        case ENC_ROTATE_CW:   Serial.println("[ENC] 右旋"); break;
        case ENC_ROTATE_CCW:  Serial.println("[ENC] 左旋"); break;
        case ENC_SINGLE_CLICK:Serial.println("[ENC] 单点"); break;
        case ENC_DOUBLE_CLICK:Serial.println("[ENC] 双击"); break;
        case ENC_LONG_PRESS:  Serial.println("[ENC] 长按"); break;
        default: break;
    }

    if (currentPage == PAGE_HOME && fanAdjusting &&
        (event == ENC_SINGLE_CLICK || event == ENC_DOUBLE_CLICK || event == ENC_LONG_PRESS)) {
        fanAdjusting = false;
    }
    // 时间页面: 任何操作返回主页
    if (currentPage == PAGE_TIME) {
        lastUserAction = millis();
        switchPage(PAGE_HOME);
        return;
    }

    switch (currentPage) {
        case PAGE_HOME:              handleHomeEvent(event); break;
        case PAGE_ARGB_CONTROL:      handleArgbEvent(event); break;
        case PAGE_SETTINGS:          handleSettingsEvent(event); break;
        case PAGE_POWER_SAVE:        handlePowerSaveEvent(event); break;
        case PAGE_BRIGHTNESS_MANUAL: handleBrightnessManualEvent(event); break;
        default: break;
    }
}

void checkIdleTimeout() {
    unsigned long now = millis();

    // 首页/时间页面: 不自动切换
    if (currentPage == PAGE_HOME || currentPage == PAGE_TIME) return;
    // 亮度页面不自动返回 (需要长时间观察数值)
    if (currentPage == PAGE_BRIGHTNESS_MANUAL) return;

    if (now - lastUserAction >= IDLE_TIMEOUT_MS) {
        if (currentPage == PAGE_ARGB_CONTROL) argbRender(argbCurrentMode, argbBrightness);
        switchPage(PAGE_HOME);
    }
}

/** 检查是否要从首页切换到时间页面 */
void checkTimeSwitch() {
    static unsigned long timeSwitchStart = 0;
    static bool timeSwitchArmed = false;
    unsigned long now = millis();

    if (currentPage != PAGE_HOME) {
        timeSwitchArmed = false;
        return;
    }

    // 关闭状态: 2s显示时间; 正常状态: 6s
    unsigned long timeout = gpio0PowerOn ? IDLE_TIMEOUT_MS : 2000;

    if (now - lastUserAction >= timeout && timeDisplayEnabled) {
        if (!timeSwitchArmed) {
            timeSwitchStart = now;
            timeSwitchArmed = true;
        }
        // NTP已同步则显示时间 (WiFi已关, 时间由ESP内部RTC维持)
        if (time(nullptr) > 100000) {
            switchPage(PAGE_TIME);
            timeSwitchArmed = false;
        }
    } else {
        timeSwitchArmed = false;
    }
}

// ============================================================
// 12b. 实时调试串口输出 (1s周期, ADC为3s滑动平均)
// ============================================================
#define DEBUG_INTERVAL_MS 1000

void debugPrint() {
    static unsigned long lastDebug = 0;
    unsigned long now = millis();
    if (now - lastDebug < DEBUG_INTERVAL_MS) return;
    lastDebug = now;

    uint16_t adc = photoRead();
    uint8_t bright = vfdBrightness;

    Serial.print("ADC:");
    Serial.print(adc);
    Serial.print("mV~");
    Serial.print(adc * 1000 / 1024);
    Serial.print(" 亮:");
    Serial.print(bright);
    Serial.print(" 档:");
    Serial.print(fanDisplaySpeed);
    Serial.print("/12 PWM:");
    Serial.print(fanCurrentPWM);
    Serial.print("→");
    Serial.print(fanTargetPWM);
    Serial.print(" ");
    Serial.print(fanMode == FAN_CONSTANT ? "定" : "自V");
    Serial.print(" ps:");
    Serial.print(powerSaveEnabled ? "1" : "0");
    Serial.print(" br:");
    Serial.print(brightMode == BRIGHT_AUTO ? "A" : "M");
    Serial.println();
}

// ============================================================
// 13. Setup & Loop
// ============================================================

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== 智能风扇控制器 v1.0 ===");

    pinMode(VFD_DIN, OUTPUT);
    pinMode(VFD_CLK, OUTPUT);
    pinMode(VFD_CS, OUTPUT);
    pinMode(VFD_RST, OUTPUT);
    digitalWrite(VFD_RST, LOW);
    delayMicroseconds(5);
    digitalWrite(VFD_RST, HIGH);
    vfdInit();
    vfdClear();
    vfdSetBrightness(200);

    encInit();
    pinMode(BTN_GPIO0, INPUT_PULLUP);  // GPIO0 默认上拉高电平
    gpio0LastState = digitalRead(BTN_GPIO0); // 记录初始状态
    fanInit();
    fanSetSpeed(fanSpeed);
    argbInit();
    argbRender(argbCurrentMode, argbBrightness);
    argbCycleLastMs = millis();
    Serial.println("[ARGB] 初始化完成");

    // --- WiFi 连接: 先扫描, 按优先级连接 (Home > OrangePi_AP) ---
    Serial.print("[WiFi] 扫描中...");
    WiFi.mode(WIFI_STA);
    WiFi.persistent(false);

    int n = WiFi.scanNetworks();
    Serial.printf("\n[WiFi] 扫描完成, 发现 %d 个网络\n", n);

    bool homeFound = false;
    bool apFound = false;
    for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        Serial.printf("  [%d] %s (%ddBm)\n", i, ssid.c_str(), WiFi.RSSI(i));
        if (ssid == "Home")       homeFound = true;
        if (ssid == "OrangePi_AP") apFound = true;
    }

    bool wifiConnected = false;
    if (homeFound) {
        Serial.println("[WiFi] 发现 Home, 优先连接...");
        WiFi.begin("Home", "197400qQ");
        int wcnt = 0;
        while (WiFi.status() != WL_CONNECTED && wcnt < 40) {  // 4s超时
            delay(100);
            wcnt++;
            if (wcnt % 10 == 0) Serial.print(".");
        }
        wifiConnected = (WiFi.status() == WL_CONNECTED);
    }
    if (!wifiConnected && apFound) {
        Serial.println("\n[WiFi] 发现 OrangePi_AP, 正在连接...");
        WiFi.begin("OrangePi_AP", "12345678");
        int wcnt = 0;
        while (WiFi.status() != WL_CONNECTED && wcnt < 40) {  // 4s超时
            delay(100);
            wcnt++;
            if (wcnt % 10 == 0) Serial.print(".");
        }
        wifiConnected = (WiFi.status() == WL_CONNECTED);
    }
    if (!wifiConnected) {
        Serial.println("\n[WiFi] 未发现已知网络或连接失败, 跳过");
    }
    if (wifiConnected) {
        Serial.print("\n[WiFi] 已连接 IP:");
        Serial.println(WiFi.localIP());
        // NTP 同步 (国内NTP服务器)
        configTime(8 * 3600, 0, "ntp.aliyun.com", "cn.ntp.org.cn", "ntp1.aliyun.com");
        Serial.println("[NTP] 正在同步...");
        time_t now = time(nullptr);
        int ncnt = 0;
        while (now < 100000 && ncnt < 40) {  // 4s超时
            delay(100);
            now = time(nullptr);
            ncnt++;
        }
        if (now > 100000) {
            struct tm* info = localtime(&now);
            Serial.printf("[NTP] 同步成功: %04d-%02d-%02d %02d:%02d:%02d\n",
                info->tm_year + 1900, info->tm_mon + 1, info->tm_mday,
                info->tm_hour, info->tm_min, info->tm_sec);
            // NTP完毕关闭WiFi, 避免中断干扰WS2812B时序
            WiFi.disconnect(true);
            WiFi.forceSleepBegin();
            WiFi.mode(WIFI_OFF);
            delay(1);
            Serial.println("[WiFi] 已关闭(防中断干扰)");
        } else {
            Serial.println("[NTP] 同步超时");
            WiFi.disconnect(true);
            WiFi.forceSleepBegin();
            WiFi.mode(WIFI_OFF);
        }
    }

    lastUserAction = millis();
    naturalCycleStart = millis();
    Serial.println("初始化完成!");
}

void loop() {
    unsigned long now = millis();

    // 1. 编码器事件
    EncEvent event = encPoll();
    if (event != ENC_NONE) {
        handleEncEvent(event);
        lastUserAction = now;
    }

    // 1b. GPIO0 按键检测 (低电平=按下, 防抖50ms)
    bool gpio0Curr = digitalRead(BTN_GPIO0);
    if (gpio0Curr == LOW && gpio0LastState == HIGH) {
        gpio0DebounceMs = now;
    }
    if (gpio0Curr == HIGH && gpio0LastState == LOW && (now - gpio0DebounceMs >= 50)) {
        if (gpio0PowerOn) {
            // 关闭: 仅停风扇, ARGB保持开启, 保存模式状态
            gpio0SavedFanSpeed = fanSpeed;
            gpio0SavedFanMode = fanMode;
            gpio0SavedArgbMode = argbCurrentMode;
            gpio0SavedArgbBrightness = argbBrightness;
            fanSetRawSpeed(0);
            fanMode = FAN_CONSTANT;
            gpio0PowerOn = false;
            // 切换到首页显示 0x1D
            if (currentPage != PAGE_HOME) switchPage(PAGE_HOME);
            Serial.println("[GPIO0] 关闭风扇");
        } else {
            // 开启: 恢复风扇和模式
            fanSpeed = gpio0SavedFanSpeed;
            fanMode = gpio0SavedFanMode;
            if (fanMode == FAN_CONSTANT) {
                fanSetSpeed(fanSpeed);
            } else {
                naturalCycleStart = millis();
                valleyGustNext = valleyCalmNext = 0;
                valleyInGust = valleyInCalm = false;
                fanSetSpeed(fanSpeed);
            }
            gpio0PowerOn = true;
            if (currentPage != PAGE_HOME) switchPage(PAGE_HOME);
            Serial.println("[GPIO0] 开启风扇");
        }
    }
    gpio0LastState = gpio0Curr;

    // 2. 风扇 PWM 平滑变速 (每帧都执行)
    fanSmoothUpdate();

    // 3. 自然风更新 (通电时始终运行, 不限页面)
    if (gpio0PowerOn && fanMode == FAN_NATURAL) {
        naturalWindUpdate();
    }

    // 4. ARGB RGB循环更新 + 亮度自动
    argbCycleUpdate();
    argbBrightnessUpdate();

    // 5. 亮度控制
    if (brightMode == BRIGHT_AUTO) {
        brightnessUpdate();
    } else if (brightMode == BRIGHT_MANUAL && powerSaveEnabled) {
        // 手动亮度+省电模式: 5s无操作降为40%
        static unsigned long lastBrightUpdate = 0;
        if (now - lastBrightUpdate >= 500) {
            lastBrightUpdate = now;
            if (now - lastUserAction > POWER_SAVE_IDLE_MS) {
                uint8_t target = (uint16_t)vfdManualBrightness * 40 / 100;
                if (target < 5) target = 5;
                if (vfdBrightness != target) vfdSetBrightness(target);
            } else {
                if (vfdBrightness != vfdManualBrightness) vfdSetBrightness(vfdManualBrightness);
            }
        }
    }

    // 6. 调试串口输出
    debugPrint();

    // 7. 显示更新
    displayUpdate();

    // 8. 空闲超时 & 时间页面切换
    checkIdleTimeout();
    checkTimeSwitch();

    // 9. 调速箭头快速恢复为风车动画 (250ms)
    static unsigned long fanAdjTimeout = 0;
    if (fanAdjusting && currentPage == PAGE_HOME) {
        if (fanAdjTimeout == 0) fanAdjTimeout = now;
        if (now - fanAdjTimeout > FAN_ADJ_TIMEOUT_MS) { fanAdjusting = false; fanAdjTimeout = 0; }
    } else {
        fanAdjTimeout = 0;
    }
}