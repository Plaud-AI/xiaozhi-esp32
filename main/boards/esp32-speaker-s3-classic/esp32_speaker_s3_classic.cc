#include "boards/common/wifi_board.h"
#include "dual_i2s_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "assets/lang_config.h"

#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <wifi_station.h>

#define TAG "esp32-speaker-s3-classic"

// ========== I2C 诊断模式开关 ==========
// 设置为 1 启用详细的 I2C 诊断（会测试多组引脚）
// 设置为 0 使用正常模式
#define ENABLE_I2C_DIAGNOSTIC 0  // ✅ 已禁用：硬件配置已从原理图确认

#if ENABLE_I2C_DIAGNOSTIC
extern void TestI2CConnection();  // 声明测试函数
#endif

class Esp32SpeakerS3Classic : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    LcdDisplay* display_;
    Button boot_button_;
    Button key_button_;
    Button volume_up_button_;
    Button volume_down_button_;

    // ========== I2C 初始化 ==========
    void InitializeI2c() {
        ESP_LOGI(TAG, "初始化 I2C 总线: SDA=%d, SCL=%d", 
                 AUDIO_CODEC_I2C_SDA_PIN, AUDIO_CODEC_I2C_SCL_PIN);
        
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = (i2c_port_t)1,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
        ESP_LOGI(TAG, "I2C 总线初始化成功");
    }

    // ========== I2C 设备扫描（调试用） ==========
    void I2cDetect() {
        ESP_LOGI(TAG, "扫描 I2C 设备...");
        printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\r\n");
        for (int i = 0; i < 128; i += 16) {
            printf("%02x: ", i);
            for (int j = 0; j < 16; j++) {
                fflush(stdout);
                uint8_t address = i + j;
                esp_err_t ret = i2c_master_probe(i2c_bus_, address, pdMS_TO_TICKS(200));
                if (ret == ESP_OK) {
                    printf("%02x ", address);
                } else if (ret == ESP_ERR_TIMEOUT) {
                    printf("UU ");
                } else {
                    printf("-- ");
                }
            }
            printf("\r\n");
        }
    }

    // ========== SPI 初始化（用于 LCD） ==========
    void InitializeSpi() {
        ESP_LOGI(TAG, "初始化 SPI 总线: CLK=%d, MOSI=%d", 
                 LCD_PIN_CLK, LCD_PIN_MOSI);
        
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = LCD_PIN_MOSI;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = LCD_PIN_CLK;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
        ESP_LOGI(TAG, "SPI 总线初始化成功");
    }

    // ========== 按键初始化 ==========
    void InitializeButtons() {
        ESP_LOGI(TAG, "初始化按键");
        
        // Boot 按键（多功能）
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState();
        });

        // 功能按键
        key_button_.OnClick([this]() {
            ESP_LOGI(TAG, "功能键按下");
            auto& app = Application::GetInstance();
            app.ToggleChatState();
        });

        // 音量加按键
        volume_up_button_.OnClick([this]() {
            ChangeVolume(10);
        });
        volume_up_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(100);
            GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME);
        });

        // 音量减按键
        volume_down_button_.OnClick([this]() {
            ChangeVolume(-10);
        });
        volume_down_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(0);
            GetDisplay()->ShowNotification(Lang::Strings::MUTED);
        });
        
        ESP_LOGI(TAG, "按键初始化完成");
    }

    // ========== 音量调节 ==========
    void ChangeVolume(int delta) {
        auto codec = GetAudioCodec();
        int volume = codec->output_volume() + delta;
        if (volume > 100) volume = 100;
        if (volume < 0) volume = 0;
        codec->SetOutputVolume(volume);
        GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        ESP_LOGI(TAG, "音量调节: %d", volume);
    }

    // ========== ST7789P3 显示屏初始化 ==========
    void InitializeSt7789Display() {
        ESP_LOGI(TAG, "初始化 ST7789P3 显示屏 (%dx%d)", DISPLAY_WIDTH, DISPLAY_HEIGHT);
        
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        // LCD 控制 IO 初始化
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = LCD_PIN_CS;
        io_config.dc_gpio_num = LCD_PIN_DC;
        io_config.spi_mode = 0;
        io_config.pclk_hz = 40 * 1000 * 1000;  // 40 MHz
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &panel_io));

        // 初始化 ST7789 驱动芯片
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = LCD_PIN_RST;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));

        // 复位并初始化显示屏
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, true));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

        display_ = new SpiLcdDisplay(panel_io, panel,
                                     DISPLAY_WIDTH, DISPLAY_HEIGHT, 
                                     DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, 
                                     DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, 
                                     DISPLAY_SWAP_XY);
        
        ESP_LOGI(TAG, "ST7789P3 显示屏初始化成功");
    }

public:
    Esp32SpeakerS3Classic() 
        : boot_button_(BOOT_BUTTON_GPIO),
          key_button_(KEY_BUTTON_GPIO),
          volume_up_button_(VOLUME_UP_GPIO),
          volume_down_button_(VOLUME_DOWN_GPIO) {
        
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "初始化 ESP32 Speaker S3 Classic 开发板");
        ESP_LOGI(TAG, "========================================");
        
#if ENABLE_I2C_DIAGNOSTIC
        // ⚠️ I2C 诊断模式：测试所有可能的引脚组合
        ESP_LOGW(TAG, "");
        ESP_LOGW(TAG, "⚠️⚠️⚠️ I2C 诊断模式已启用 ⚠️⚠️⚠️");
        ESP_LOGW(TAG, "将测试多组引脚配置以找到正确的 I2C 连接");
        ESP_LOGW(TAG, "");
        TestI2CConnection();
        ESP_LOGW(TAG, "");
        ESP_LOGW(TAG, "⚠️ 诊断完成！请查看上面的输出找到正确的引脚配置");
        ESP_LOGW(TAG, "⚠️ 然后修改 config.h 并设置 ENABLE_I2C_DIAGNOSTIC=0");
        ESP_LOGW(TAG, "");
        // 诊断模式下不继续初始化其他模块，避免崩溃
        return;
#else
        // 按顺序初始化各个模块
        InitializeI2c();
        I2cDetect();  // 调试：扫描 I2C 设备
        InitializeSpi();
        InitializeSt7789Display();
        InitializeButtons();
#endif
        
        // 设置背光亮度
        GetBacklight()->SetBrightness(100);
        
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "开发板初始化完成");
        ESP_LOGI(TAG, "========================================");
    }

    // ========== 获取音频编解码器 ==========
    virtual AudioCodec* GetAudioCodec() override {
        static DualI2sAudioCodec audio_codec(
            i2c_bus_,
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_ES8311_MCLK,
            AUDIO_ES8311_BCLK,
            AUDIO_ES8311_WS,
            AUDIO_ES8311_DOUT,
            AUDIO_ES8311_DIN,
            AUDIO_ES7210_MCLK,
            AUDIO_ES7210_BCLK,
            AUDIO_ES7210_WS,
            AUDIO_ES7210_DIN,
            AUDIO_CODEC_PA_PIN,
            AUDIO_CODEC_ES8311_ADDR,
            AUDIO_CODEC_ES7210_ADDR
        );
        return &audio_codec;
    }

    // ========== 获取显示屏 ==========
    virtual Display* GetDisplay() override {
        return display_;
    }

    // ========== 获取背光控制 ==========
    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }
};

// 注册开发板
DECLARE_BOARD(Esp32SpeakerS3Classic);

