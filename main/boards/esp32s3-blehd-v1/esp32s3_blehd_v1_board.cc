// ESP32S3 BLEHD V1 Board - 参考 A_CHAOGE_BOARD 实现，保留 AAF 显示系统
#include "wifi_board.h"
#include "display/aaf_display_widget.h"
#include "codecs/box_audio_codec.h"
#include "application.h"
#include "button.h"
#include "mcp_server.h"
#include "config.h"
#include "power_save_timer.h"
#include "i2c_device.h"
#include "assets/lang_config.h"
#include "hardware/cs_press_driver.h"
#include "hardware/nfc_driver.h"
#include "hardware/servo_driver.h"
#include "hardware/ws2812_driver.h"

#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <driver/i2c_master.h>
#include <driver/spi_master.h>
#include <wifi_station.h>
#include "settings.h"

#define TAG "Esp32S3BlehdV1Board"

class Esp32S3BlehdV1Board : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    Button boot_button_;
    Button volume_up_button_;
    Button volume_down_button_;
    Display* display_;
    PowerSaveTimer* power_save_timer_;
    CsPressDriver* pressure_driver_;
    NfcDriver* nfc_driver_;
    DualServoController* dual_servo_controller_;
    Ws2812Driver* ws2812_driver_;

    void InitializePowerSaveTimer() {
        power_save_timer_ = new PowerSaveTimer(-1, 60, 300);
        power_save_timer_->OnEnterSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(true);
            GetBacklight()->SetBrightness(20);
        });
        power_save_timer_->OnExitSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(false);
            GetBacklight()->RestoreBrightness();
        });
        power_save_timer_->SetEnabled(true);
    }

    void InitializeI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
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
    }

    void InitializePressureSensor() {
        // 使用共享的 I2C 总线初始化 CHIPSEA 压感驱动
        ESP_LOGI(TAG, "Initializing CHIPSEA pressure sensor...");
        pressure_driver_ = new CsPressDriver(i2c_bus_, CS_PRESS_I2C_ADDR, GPIO_NUM_NC);
        
        if (pressure_driver_ != nullptr) {
            esp_err_t ret = pressure_driver_->InitWithoutFwUpdate();
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "CHIPSEA pressure sensor initialized successfully");
                pressure_driver_->PrintFwInfo();
            } else {
                ESP_LOGW(TAG, "Failed to initialize pressure sensor: %s", esp_err_to_name(ret));
                // 不删除驱动，允许后续重试
            }
        }
    }

    void InitializeNfc() {
        // 初始化 NFC 模块
        ESP_LOGI(TAG, "Initializing NFC module...");
        nfc_driver_ = new NfcDriver(NFC_UART_PORT, NFC_TX_PIN, NFC_RX_PIN);
        
        if (nfc_driver_ != nullptr) {
            esp_err_t ret = nfc_driver_->Init();
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "NFC module initialized successfully (UART%d, TX=%d, RX=%d)",
                         NFC_UART_PORT, NFC_TX_PIN, NFC_RX_PIN);
            } else {
                ESP_LOGW(TAG, "Failed to initialize NFC module: %s", esp_err_to_name(ret));
                // 不删除驱动，允许后续重试
            }
        }
    }

    void InitializeDualServo() {
        // 初始化双舵机控制器
        ESP_LOGI(TAG, "Initializing dual servo controller...");
        dual_servo_controller_ = new DualServoController(SERVO1_PIN, SERVO2_PIN);
        
        if (dual_servo_controller_ != nullptr) {
            esp_err_t ret = dual_servo_controller_->Init();
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Dual servo controller initialized successfully (Servo1=%d, Servo2=%d)",
                         SERVO1_PIN, SERVO2_PIN);
                // 回到中位
                dual_servo_controller_->SetAngle(SERVO_ID_1, 90);
                dual_servo_controller_->SetAngle(SERVO_ID_2, 90);
            } else {
                ESP_LOGW(TAG, "Failed to initialize dual servo controller: %s", esp_err_to_name(ret));
            }
        }
    }

    void InitializeWs2812() {
        // 初始化 WS2812 灯带
        ESP_LOGI(TAG, "Initializing WS2812 LED strip...");
        ws2812_driver_ = new Ws2812Driver(WS2812_PIN, WS2812_LED_NUM);
        
        if (ws2812_driver_ != nullptr) {
            esp_err_t ret = ws2812_driver_->Init();
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "WS2812 LED strip initialized successfully (GPIO=%d, LEDs=%d)",
                         WS2812_PIN, WS2812_LED_NUM);
            } else {
                ESP_LOGW(TAG, "Failed to initialize WS2812 LED strip: %s", esp_err_to_name(ret));
            }
        }
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_CLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState();
        });

        volume_up_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) {
                volume = 100;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_up_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(100);
            GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME);
        });

        volume_down_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() - 10;
            if (volume < 0) {
                volume = 0;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_down_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(0);
            GetDisplay()->ShowNotification(Lang::Strings::MUTED);
        });
    }

    void InitializeDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        ESP_LOGI(TAG, "Install panel IO: CS=%d, DC=%d, RST=%d", DISPLAY_CS_PIN, DISPLAY_DC_PIN, DISPLAY_RST_PIN);
        
        // 注意：原理图显示 SPI 信号通过 1kΩ 电阻连接，降低时钟频率以提高信号质量
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = 0;  // ST7789 标准 SPI Mode 0
        io_config.pclk_hz = 20 * 1000 * 1000;  // 降低到 20MHz（原理图有串联电阻）
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        // 使用 ST7789 驱动
        ESP_LOGI(TAG, "Install ST7789 LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
        
        // LCD 初始化序列（简化版，参考其他工作正常的板子）
        ESP_LOGI(TAG, "Initializing LCD panel...");
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
        vTaskDelay(pdMS_TO_TICKS(100));  // 复位后等待
        
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
        vTaskDelay(pdMS_TO_TICKS(50));   // 初始化后等待
        
        // 设置显示方向（在 invert_color 之前）
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));
        
        // 颜色反转（ST7789 通常需要）
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, true));
        
        // 设置显示偏移
        esp_lcd_panel_set_gap(panel, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y);
        
        // 打开显示
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
        
        ESP_LOGI(TAG, "LCD panel initialized: %dx%d, offset=(%d,%d), mirror=(%d,%d), swap=%d",
                 DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
                 DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);

        // 使用 AAF Display Widget
        ESP_LOGI(TAG, "Initializing AAF Display Framework...");
        display_ = new xiaozhi::display::AafDisplayWidget(panel_io, panel,
                                                           DISPLAY_WIDTH, DISPLAY_HEIGHT);
        
        if (display_) {
            ESP_LOGI(TAG, "AAF Display Framework ready!");
        } else {
            ESP_LOGW(TAG, "AAF Display Framework init failed");
        }
    }

    void InitializeTools() {
        auto& mcp_server = McpServer::GetInstance();
        mcp_server.AddTool("self.system.reconfigure_wifi",
            "Reboot device and enter WiFi configuration mode.",
            PropertyList(),
            [this](const PropertyList&) {
                ResetWifiConfiguration();
                return true;
            });
    }

public:
    Esp32S3BlehdV1Board() : 
        boot_button_(BOOT_BUTTON_GPIO),
        volume_up_button_(VOLUME_UP_BUTTON_GPIO),
        volume_down_button_(VOLUME_DOWN_BUTTON_GPIO),
        pressure_driver_(nullptr),
        nfc_driver_(nullptr),
        dual_servo_controller_(nullptr),
        ws2812_driver_(nullptr) {
        
        ESP_LOGI(TAG, "Initializing ESP32S3 BLEHD V1 Board");
        
        InitializePowerSaveTimer();
        InitializeI2c();
        InitializePressureSensor();
        InitializeNfc();
        InitializeDualServo();
        InitializeWs2812();
        InitializeSpi();
        InitializeDisplay();
        InitializeButtons();
        InitializeTools();
        GetBacklight()->RestoreBrightness();
    }

    virtual AudioCodec* GetAudioCodec() override {
        static BoxAudioCodec audio_codec(
            i2c_bus_, 
            AUDIO_INPUT_SAMPLE_RATE, 
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, 
            AUDIO_I2S_GPIO_BCLK, 
            AUDIO_I2S_GPIO_WS, 
            AUDIO_I2S_GPIO_DOUT, 
            AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN, 
            AUDIO_CODEC_ES8311_ADDR, 
            AUDIO_CODEC_ES7210_ADDR, 
            AUDIO_INPUT_REFERENCE);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        // No PMIC → no battery readings
        charging = false;
        discharging = false;
        level = 100;
        return true;
    }

    virtual CsPressDriver* GetPressureDriver() override {
        return pressure_driver_;
    }

    virtual NfcDriver* GetNfcDriver() override {
        return nfc_driver_;
    }

    virtual DualServoController* GetDualServoController() override {
        return dual_servo_controller_;
    }

    virtual Ws2812Driver* GetWs2812Driver() override {
        return ws2812_driver_;
    }

    virtual void SetPowerSaveMode(bool enabled) override {
        if (!enabled) {
            power_save_timer_->WakeUp();
        }
        WifiBoard::SetPowerSaveMode(enabled);
    }
};

DECLARE_BOARD(Esp32S3BlehdV1Board);
