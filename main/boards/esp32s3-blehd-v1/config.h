
#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

// ============================================================================
// 音频配置 - ESP32S3 BLEHD V1 硬件
// ============================================================================
#define AUDIO_INPUT_SAMPLE_RATE  24000  // 输入采样率 24kHz
#define AUDIO_OUTPUT_SAMPLE_RATE 24000  // 输出采样率 24kHz
#define AUDIO_DEFAULT_OUTPUT_VOLUME 80  // 默认音量 80%

#define AUDIO_INPUT_REFERENCE    true   // 是否使用参考通道（用于回声消除）

// I2S 音频引脚配置（从原理图提取）
#define AUDIO_I2S_GPIO_MCLK GPIO_NUM_6   // I2S_MCLK
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_7   // I2S_SCLK
#define AUDIO_I2S_GPIO_WS   GPIO_NUM_15  // I2S_LRCK
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_16  // I2S_DIN → ES8311 喇叭输出
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_17  // I2S_SDOUT_BUF ← ES7210 麦克风输入

// 功放控制引脚
#define AUDIO_CODEC_PA_PIN       GPIO_NUM_18  // PA_CONTROL

// I2C 配置（音频编解码器）
#define AUDIO_CODEC_I2C_SDA_PIN  GPIO_NUM_5   // ES_SDA
#define AUDIO_CODEC_I2C_SCL_PIN  GPIO_NUM_4   // ES_SCL
#define AUDIO_CODEC_ES8311_ADDR  ES8311_CODEC_DEFAULT_ADDR  // 0x18
#define AUDIO_CODEC_ES7210_ADDR  ES7210_CODEC_DEFAULT_ADDR  // 0x40

// ============================================================================
// 按钮配置
// ============================================================================
#define BUILTIN_LED_GPIO        GPIO_NUM_NC
#define BOOT_BUTTON_GPIO        GPIO_NUM_0
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_NC
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_NC

// ============================================================================
// LCD 显示屏配置 - OK-118RM024-2-35 (从原理图提取)
// ============================================================================
// SPI 引脚
#define LCD_SPI_MOSI    GPIO_NUM_47  // L_DIN
#define LCD_SPI_CLK     GPIO_NUM_13  // L_CLK
#define LCD_CS_PIN      GPIO_NUM_14  // L_CS
#define LCD_DC_PIN      GPIO_NUM_21  // L_RS (DC)
#define LCD_RST_PIN     GPIO_NUM_12  // L_RES (复位)
#define LCD_BL_PIN      GPIO_NUM_40  // LED_BL (背光)

// 触摸屏引脚（与音频共用 I2C）
#define TP_INT_PIN      GPIO_NUM_48  // TP_INT
#define TP_RST_PIN      GPIO_NUM_45  // TP_RST

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

#define DISPLAY_BACKLIGHT_PIN LCD_BL_PIN
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false

#endif // _BOARD_CONFIG_H_

