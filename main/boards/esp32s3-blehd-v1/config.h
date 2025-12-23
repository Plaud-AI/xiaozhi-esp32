#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>
#include <driver/spi_master.h>

// ============================================================================
// 音频配置 - ESP32S3 BLEHD V1 硬件
// ============================================================================
#define AUDIO_INPUT_SAMPLE_RATE  24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

#define AUDIO_INPUT_REFERENCE    true

// I2S 音频引脚配置
#define AUDIO_I2S_GPIO_MCLK GPIO_NUM_6
#define AUDIO_I2S_GPIO_WS   GPIO_NUM_15
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_7
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_17
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_16

// 功放控制引脚
#define AUDIO_CODEC_PA_PIN       GPIO_NUM_18

// I2C 配置（音频编解码器）
#define AUDIO_CODEC_I2C_SDA_PIN  GPIO_NUM_5
#define AUDIO_CODEC_I2C_SCL_PIN  GPIO_NUM_4
#define AUDIO_CODEC_ES8311_ADDR  ES8311_CODEC_DEFAULT_ADDR
#define AUDIO_CODEC_ES7210_ADDR  ES7210_CODEC_DEFAULT_ADDR

// ============================================================================
// 按钮配置
// ============================================================================
#define BOOT_BUTTON_GPIO        GPIO_NUM_0
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_11   // 音量+
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_10   // 音量-

// ============================================================================
// LCD 显示屏配置 - ST7789 驱动（与 A_CHAOGE_BOARD 对齐）
// ============================================================================
#define DISPLAY_SPI_MODE        0  // ST7789 使用 SPI Mode 0
#define DISPLAY_CS_PIN          GPIO_NUM_14
#define DISPLAY_MOSI_PIN        GPIO_NUM_47
#define DISPLAY_MISO_PIN        GPIO_NUM_NC
#define DISPLAY_CLK_PIN         GPIO_NUM_13
#define DISPLAY_DC_PIN          GPIO_NUM_21
#define DISPLAY_INT_PIN         GPIO_NUM_48
#define DISPLAY_RST_PIN         GPIO_NUM_12  // L_RES 连接到 GPIO12（原理图）

#define DISPLAY_WIDTH           240
#define DISPLAY_HEIGHT          284
#define DISPLAY_MIRROR_X        false
#define DISPLAY_MIRROR_Y        false
#define DISPLAY_SWAP_XY         false

#define DISPLAY_OFFSET_X        0
#define DISPLAY_OFFSET_Y        20

#define DISPLAY_BACKLIGHT_PIN   GPIO_NUM_40
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false

// ============================================================================
// NFC 配置（对齐参考工程）
// ============================================================================
#define NFC_UART_PORT               UART_NUM_1
#define NFC_TX_PIN                  GPIO_NUM_41
#define NFC_RX_PIN                  GPIO_NUM_42
#define NFC_BAUDRATE                115200

// ============================================================================
// 舵机配置（对齐参考工程）
// ============================================================================
#define SERVO1_PIN                  GPIO_NUM_1
#define SERVO2_PIN                  GPIO_NUM_2

// ============================================================================
// RGB 灯带配置（对齐参考工程）
// ============================================================================
#define WS2812_PIN                  GPIO_NUM_38
#define WS2812_LED_NUM              6

// ============================================================================
// 压感传感器配置（对齐参考工程，暂时屏蔽）
// ============================================================================
#define PRESSURE_SDA_PIN            GPIO_NUM_5
#define PRESSURE_SCL_PIN            GPIO_NUM_4
#define PRESSURE_INT_PIN            GPIO_NUM_6

#endif // _BOARD_CONFIG_H_
