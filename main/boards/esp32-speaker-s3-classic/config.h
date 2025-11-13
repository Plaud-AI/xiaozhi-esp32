#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

// ========== 音频配置 ==========
// 采样率配置
#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 16000

// I2C 控制总线（ES8311 和 ES7210 共用）
#define AUDIO_CODEC_I2C_SDA_PIN  GPIO_NUM_5  // ⚠️ 待确认：需要从原理图查找 ES_SDA 连接的 GPIO
#define AUDIO_CODEC_I2C_SCL_PIN  GPIO_NUM_4  // ⚠️ 待确认：需要从原理图查找 ES_SCL 连接的 GPIO
#define AUDIO_CODEC_ES8311_ADDR  ES8311_CODEC_DEFAULT_ADDR  // 默认 0x30，可能需要改为 0x18
#define AUDIO_CODEC_ES7210_ADDR  0x40  // ✅ 已确认：AD0=GND, AD1=GND → 地址=0x40 (不是默认的 0x80)

// ES8311 (DAC/扬声器输出) I2S 引脚 - 使用 I2S0
#define AUDIO_ES8311_MCLK        GPIO_NUM_3
#define AUDIO_ES8311_BCLK        GPIO_NUM_40
#define AUDIO_ES8311_WS          GPIO_NUM_46
#define AUDIO_ES8311_DOUT        GPIO_NUM_38
#define AUDIO_ES8311_DIN         GPIO_NUM_39

// ES7210 (ADC/麦克风输入) I2S 引脚 - 使用 I2S1
#define AUDIO_ES7210_MCLK        GPIO_NUM_13
#define AUDIO_ES7210_BCLK        GPIO_NUM_10
#define AUDIO_ES7210_WS          GPIO_NUM_9
#define AUDIO_ES7210_DIN         GPIO_NUM_11
#define AUDIO_ES7210_INT         GPIO_NUM_12

// 功放配置（NS4150B 无需 GPIO 控制，硬件自动使能）
#define AUDIO_CODEC_PA_PIN       GPIO_NUM_NC

// 音频输入参考（用于 AEC 回声消除）
#define AUDIO_INPUT_REFERENCE    false

// ========== 显示屏配置 (ST7789P3, 200x320) ==========
// LCD 使用 SPI 接口
#define LCD_PIN_DC               GPIO_NUM_7   // L_RS (数据/命令选择)
#define LCD_PIN_CS               GPIO_NUM_18  // L_CS (片选)
#define LCD_PIN_CLK              GPIO_NUM_14  // L_CLK (时钟)
#define LCD_PIN_MOSI             GPIO_NUM_16  // L_DIN (数据)
#define LCD_PIN_RST              GPIO_NUM_15  // L_RES (复位)

#define DISPLAY_WIDTH            200
#define DISPLAY_HEIGHT           320
#define DISPLAY_MIRROR_X         false
#define DISPLAY_MIRROR_Y         false
#define DISPLAY_SWAP_XY          false
#define DISPLAY_OFFSET_X         0
#define DISPLAY_OFFSET_Y         0

// 背光控制（通过 Q3 三极管控制）
#define DISPLAY_BACKLIGHT_PIN            GPIO_NUM_17
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT  false

// ========== 按键配置 ==========
#define BOOT_BUTTON_GPIO         GPIO_NUM_0   // Boot/下载按键
#define KEY_BUTTON_GPIO          GPIO_NUM_45  // 功能按键
#define VOLUME_UP_GPIO           GPIO_NUM_48  // 音量加
#define VOLUME_DOWN_GPIO         GPIO_NUM_47  // 音量减

// ========== LED 配置 ==========
#define RGB_LED_GPIO             GPIO_NUM_21  // RGB LED (可能是 WS2812)

// ========== PWM 输出 ==========
#define PWM1_GPIO                GPIO_NUM_1   // PWM1
#define PWM2_GPIO                GPIO_NUM_2   // PWM2

// ========== 电池/电源管理 ==========
#define BATTERY_ADC_GPIO         GPIO_NUM_8   // 电池电压检测 ADC

// ========== 其他 ==========
#define INT1_GPIO                GPIO_NUM_6   // INT1 中断引脚

#endif // _BOARD_CONFIG_H_

