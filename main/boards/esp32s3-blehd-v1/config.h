
#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

// ============================================================================
// 音频配置
// ============================================================================
#define AUDIO_INPUT_SAMPLE_RATE  24000  // 输入采样率 24kHz
#define AUDIO_OUTPUT_SAMPLE_RATE 24000  // 输出采样率 24kHz
#define AUDIO_DEFAULT_OUTPUT_VOLUME 80  // 默认音量 80%

#define AUDIO_INPUT_REFERENCE    true   // 是否使用参考通道（用于回声消除）

// I2S 音频引脚配置
#define AUDIO_I2S_GPIO_MCLK GPIO_NUM_16
#define AUDIO_I2S_GPIO_WS   GPIO_NUM_45
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_9
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_10
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_8

// 功放控制引脚
#define AUDIO_CODEC_PA_PIN       GPIO_NUM_48

// I2C 配置（音频编解码器）
#define AUDIO_CODEC_I2C_SDA_PIN  GPIO_NUM_17
#define AUDIO_CODEC_I2C_SCL_PIN  GPIO_NUM_18
#define AUDIO_CODEC_ES8311_ADDR  ES8311_CODEC_DEFAULT_ADDR
#define AUDIO_CODEC_ES7210_ADDR  ES7210_CODEC_DEFAULT_ADDR

// ============================================================================
// 按钮配置
// ============================================================================
#define BUILTIN_LED_GPIO        GPIO_NUM_NC
#define BOOT_BUTTON_GPIO        GPIO_NUM_0
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_NC
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_NC

// ============================================================================
// 显示屏配置
// ============================================================================
#ifdef CONFIG_ESP32S3_BLEHD_V1_LCD_ST7789
#define DISPLAY_SDA_PIN GPIO_NUM_NC
#define DISPLAY_SCL_PIN GPIO_NUM_NC
#define DISPLAY_WIDTH   280
#define DISPLAY_HEIGHT  240
#define DISPLAY_SWAP_XY true
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y true
#define BACKLIGHT_INVERT false

#define DISPLAY_OFFSET_X  20
#define DISPLAY_OFFSET_Y  0
#endif

#ifdef CONFIG_ESP32S3_BLEHD_V1_LCD_ILI9341
#define LCD_TYPE_ILI9341_SERIAL
#define DISPLAY_SDA_PIN GPIO_NUM_NC
#define DISPLAY_SCL_PIN GPIO_NUM_NC
#define DISPLAY_WIDTH   320
#define DISPLAY_HEIGHT  240

#define DISPLAY_SWAP_XY false
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y true
#define BACKLIGHT_INVERT false

#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#endif

#define DISPLAY_BACKLIGHT_PIN GPIO_NUM_NC
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false

#endif // _BOARD_CONFIG_H_

