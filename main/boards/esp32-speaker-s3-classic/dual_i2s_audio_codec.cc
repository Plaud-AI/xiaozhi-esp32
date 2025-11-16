#include "dual_i2s_audio_codec.h"
#include "settings.h"
#include "es7210_diagnostic.h"

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <driver/i2s_std.h>
#include <driver/i2s_tdm.h>  // ⚠️ 添加 TDM 模式支持
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "DualI2sAudioCodec"

// 诊断模式开关：设置为 1 启用详细诊断
#define ENABLE_ES7210_DIAGNOSTIC 1
DualI2sAudioCodec::DualI2sAudioCodec(
    void* i2c_master_handle, 
    int input_sample_rate, 
    int output_sample_rate,
    gpio_num_t es8311_mclk, 
    gpio_num_t es8311_bclk, 
    gpio_num_t es8311_ws, 
    gpio_num_t es8311_dout, 
    gpio_num_t es8311_din,
    gpio_num_t es7210_mclk, 
    gpio_num_t es7210_bclk, 
    gpio_num_t es7210_ws, 
    gpio_num_t es7210_din,
    gpio_num_t pa_pin, 
    uint8_t es8311_addr, 
    uint8_t es7210_addr
) {
    // 保存 I2C 句柄，以便在其他成员函数中使用
    i2c_master_handle_ = i2c_master_handle;
    
    duplex_ = true;
    input_reference_ = false;  // 单麦克风，无回声消除
    input_channels_ = 1;
    input_sample_rate_ = input_sample_rate;
    output_sample_rate_ = output_sample_rate;
    input_gain_ = 47;  // 最大增益（0-47），从30提升到47以增强麦克风灵敏度
    
    ESP_LOGI(TAG, "DualI2sAudioCodec 初始化: input_rate=%d, output_rate=%d", 
             input_sample_rate_, output_sample_rate_);

    // 创建两个独立的 I2S 通道
    CreateEs8311Channel(es8311_mclk, es8311_bclk, es8311_ws, es8311_dout, es8311_din);
    CreateEs7210Channel(es7210_mclk, es7210_bclk, es7210_ws, es7210_din);

    // ========== 初始化 ES8311 (DAC/输出) ==========
    audio_codec_i2s_cfg_t i2s_out_cfg = {
        .port = I2S_NUM_0,
        .rx_handle = nullptr,
        .tx_handle = tx_handle_i2s0_,
    };
    out_data_if_ = audio_codec_new_i2s_data(&i2s_out_cfg);
    assert(out_data_if_ != nullptr);

    // ⚠️ 关键修复：ES8311 需要 MCLK 才能响应 I2C 命令！
    // 策略：让 I2S 驱动自己配置 GPIO，不干预
    
    // 先启动 I2S 通道，开始输出 MCLK
    ESP_LOGI(TAG, "⏳ 启动 I2S0 通道以提供 MCLK 给 ES8311...");
    esp_err_t ret_es8311 = i2s_channel_enable(tx_handle_i2s0_);
    if (ret_es8311 != ESP_OK) {
        ESP_LOGE(TAG, "⚠️ I2S0 通道启动失败: %d", ret_es8311);
    } else {
        ESP_LOGI(TAG, "✅ I2S0 MCLK 已启动");
        
        // ⚠️ 修复策略：不使用 gpio_config()，避免覆盖 I2S 外设配置
        // 只在 I2S 启动后尝试增加驱动强度（可能无效，但不会破坏 MCLK）
        ESP_LOGI(TAG, "⚡ 尝试增加 GPIO3 (ES8311_MCLK) 驱动强度...");
        
        esp_err_t gpio_ret_post_es8311 = gpio_set_drive_capability(es8311_mclk, GPIO_DRIVE_CAP_3);
        if (gpio_ret_post_es8311 == ESP_OK) {
            ESP_LOGI(TAG, "  ✅ GPIO3 驱动强度设置为 40mA");
            
            gpio_drive_cap_t actual_cap_es8311;
            if (gpio_get_drive_capability(es8311_mclk, &actual_cap_es8311) == ESP_OK) {
                ESP_LOGI(TAG, "  📊 GPIO3 当前驱动强度：%d (0=5mA, 1=10mA, 2=20mA, 3=40mA)", actual_cap_es8311);
            }
        } else {
            ESP_LOGW(TAG, "  ⚠️ GPIO3 驱动强度设置失败: %s", esp_err_to_name(gpio_ret_post_es8311));
        }
        
        // 等待 ES8311 芯片稳定（需要 MCLK 才能工作）
        vTaskDelay(pdMS_TO_TICKS(50));  // 等待 50ms
        ESP_LOGI(TAG, "✅ ES8311 稳定时间已完成");
    }

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = (i2c_port_t)1,
        .addr = es8311_addr,
        .bus_handle = i2c_master_handle,
    };
    out_ctrl_if_ = audio_codec_new_i2c_ctrl(&i2c_cfg);
    
    // ⚠️ ES8311 可能未焊接或硬件有问题，允许初始化失败
    if (out_ctrl_if_ != nullptr) {
        // 使用 do-while(0) 来避免 goto 跳过变量初始化的问题
        do {
            out_gpio_if_ = audio_codec_new_gpio();
            if (out_gpio_if_ == nullptr) {
                ESP_LOGW(TAG, "⚠️ ES8311 GPIO 接口创建失败");
                audio_codec_delete_ctrl_if(out_ctrl_if_);
                out_ctrl_if_ = nullptr;
                output_dev_ = nullptr;
                break;
            }

            es8311_codec_cfg_t es8311_cfg = {};
            es8311_cfg.ctrl_if = out_ctrl_if_;
            es8311_cfg.gpio_if = out_gpio_if_;
            es8311_cfg.codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC;
            es8311_cfg.pa_pin = pa_pin;
            es8311_cfg.use_mclk = true;
            es8311_cfg.hw_gain.pa_voltage = 5.0;
            es8311_cfg.hw_gain.codec_dac_voltage = 3.3;
            out_codec_if_ = es8311_codec_new(&es8311_cfg);
            
            if (out_codec_if_ == nullptr) {
                ESP_LOGW(TAG, "⚠️ ES8311 编解码器创建失败（设备可能未焊接或地址错误）");
                ESP_LOGW(TAG, "⚠️ 当前尝试地址: 0x%02x", es8311_addr);
                ESP_LOGW(TAG, "⚠️ I2C 扫描检测到的设备可能不是 ES8311");
                audio_codec_delete_gpio_if(out_gpio_if_);
                audio_codec_delete_ctrl_if(out_ctrl_if_);
                out_gpio_if_ = nullptr;
                out_ctrl_if_ = nullptr;
                output_dev_ = nullptr;
                break;
            }

            esp_codec_dev_cfg_t dev_cfg = {
                .dev_type = ESP_CODEC_DEV_TYPE_OUT,
                .codec_if = out_codec_if_,
                .data_if = out_data_if_,
            };
            output_dev_ = esp_codec_dev_new(&dev_cfg);
            
            if (output_dev_ == nullptr) {
                ESP_LOGW(TAG, "⚠️ ES8311 设备创建失败");
                audio_codec_delete_codec_if(out_codec_if_);
                audio_codec_delete_gpio_if(out_gpio_if_);
                audio_codec_delete_ctrl_if(out_ctrl_if_);
                out_codec_if_ = nullptr;
                out_gpio_if_ = nullptr;
                out_ctrl_if_ = nullptr;
                break;
            }
            
            ESP_LOGI(TAG, "✅ ES8311 (DAC) 初始化成功（地址 0x%02x）", es8311_addr);
        } while (0);
    } else {
        ESP_LOGW(TAG, "⚠️ ES8311 I2C 控制接口创建失败（设备可能未焊接）");
        ESP_LOGW(TAG, "⚠️ 音频输出功能将不可用");
        output_dev_ = nullptr;
    }

    // ========== 初始化 ES7210 (ADC/输入) ==========
    audio_codec_i2s_cfg_t i2s_in_cfg = {
        .port = I2S_NUM_1,
        .rx_handle = rx_handle_i2s1_,
        .tx_handle = nullptr,
    };
    in_data_if_ = audio_codec_new_i2s_data(&i2s_in_cfg);
    assert(in_data_if_ != nullptr);

    // ⚠️ 关键修复：ES7210 需要 MCLK 才能响应 I2C 命令！
    // 策略：让 I2S 驱动自己配置 GPIO，不干预
    
    // 先启动 I2S 通道，开始输出 MCLK
    ESP_LOGI(TAG, "⏳ 启动 I2S1 通道以提供 MCLK 给 ES7210...");
    esp_err_t ret = i2s_channel_enable(rx_handle_i2s1_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "⚠️ I2S1 通道启动失败: %d", ret);
    } else {
        ESP_LOGI(TAG, "✅ I2S1 MCLK 已启动");
        
        // ⚠️ 修复策略：不使用 gpio_config()，避免覆盖 I2S 外设配置
        // 只在 I2S 启动后尝试增加驱动强度（可能无效，但不会破坏 MCLK）
        ESP_LOGI(TAG, "⚡ 尝试增加 GPIO13 (ES7210_MCLK) 驱动强度...");
        
        esp_err_t gpio_ret_post = gpio_set_drive_capability(es7210_mclk, GPIO_DRIVE_CAP_3);
        if (gpio_ret_post == ESP_OK) {
            ESP_LOGI(TAG, "  ✅ GPIO13 驱动强度设置为 40mA");
            
            gpio_drive_cap_t actual_cap;
            if (gpio_get_drive_capability(es7210_mclk, &actual_cap) == ESP_OK) {
                ESP_LOGI(TAG, "  📊 GPIO13 当前驱动强度：%d (0=5mA, 1=10mA, 2=20mA, 3=40mA)", actual_cap);
            }
        } else {
            ESP_LOGW(TAG, "  ⚠️ GPIO13 驱动强度设置失败: %s", esp_err_to_name(gpio_ret_post));
        }
        
        // 等待 ES7210 芯片稳定（需要 MCLK 才能工作）
        ESP_LOGI(TAG, "⏳ 等待 ES7210 芯片稳定（1500ms）...");
        ESP_LOGI(TAG, "💡 说明：芯片需要时间初始化，等待 MCLK 信号稳定");
        vTaskDelay(pdMS_TO_TICKS(1500));  // 固定等待 1.5 秒
        ESP_LOGI(TAG, "✅ ES7210 稳定时间已完成");
    }

    // ========== 手动 I2C 测试 ==========
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "🔬 进行 ES7210 I2C 读写测试...");
    
    // 测试 1：再次读取芯片 ID 确认
    {
        uint8_t chip_id = 0;
        uint8_t reg_addr = 0x00;
        
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = es7210_addr,
            .scl_speed_hz = 100000,  // 使用较低速度测试
        };
        
        i2c_master_dev_handle_t dev_handle;
        esp_err_t ret = i2c_master_bus_add_device((i2c_master_bus_handle_t)i2c_master_handle, &dev_cfg, &dev_handle);
        
        if (ret == ESP_OK) {
            // 尝试读取
            ret = i2c_master_transmit_receive(dev_handle, &reg_addr, 1, &chip_id, 1, 1000);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "✅ I2C 读取成功！芯片 ID 寄存器 (0x00) = 0x%02x", chip_id);
            } else {
                ESP_LOGW(TAG, "⚠️ I2C 读取失败: %s", esp_err_to_name(ret));
            }
            
            // 测试 2：尝试写入一个安全的寄存器（假设 0x00 是只读的，尝试 0x01）
            uint8_t write_data[2] = {0x01, 0x00};  // 寄存器 0x01, 值 0x00
            ret = i2c_master_transmit(dev_handle, write_data, 2, 1000);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "✅ I2C 写入成功！");
            } else {
                ESP_LOGW(TAG, "⚠️ I2C 写入失败: %s", esp_err_to_name(ret));
                ESP_LOGW(TAG, "💡 这可能说明 ES7210 芯片处于保护模式或需要特殊初始化序列");
            }
            
            i2c_master_bus_rm_device(dev_handle);
        }
    }
    ESP_LOGI(TAG, "");
    
    // ========== 创建 ES7210 控制接口 ==========
    // ⚠️ 重要：ESP-ADF 驱动会将地址右移 1 位（audio_codec_ctrl_i2c.c:52）
    // 所以需要左移 1 位：0x40 << 1 = 0x80 → ESP-ADF 右移后 → 0x40 ✅
    ESP_LOGI(TAG, "📝 修正 I2C 地址格式：0x%02x → 0x%02x (左移 1 位，适配 ESP-ADF)", 
             es7210_addr, es7210_addr << 1);
    i2c_cfg.addr = es7210_addr << 1;  // 左移以适配 ESP-ADF 的地址格式
    in_ctrl_if_ = audio_codec_new_i2c_ctrl(&i2c_cfg);
    
    // ⚠️ ES7210 可能未焊接，允许初始化失败
    if (in_ctrl_if_ == nullptr) {
        ESP_LOGW(TAG, "⚠️ ES7210 I2C 控制接口创建失败（设备可能未焊接）");
        ESP_LOGW(TAG, "⚠️ 音频输入功能将不可用，但输出功能正常");
        
#if ENABLE_ES7210_DIAGNOSTIC
        // 运行完整诊断
        ESP_LOGW(TAG, "");
        ESP_LOGW(TAG, "====================================================");
        ESP_LOGW(TAG, "⚠️ 启动 ES7210 故障诊断");
        ESP_LOGW(TAG, "====================================================");
        ES7210Diagnostic::RunFullDiagnostic(
            (i2c_master_bus_handle_t)i2c_master_handle,
            rx_handle_i2s1_,
            es7210_mclk,
            (gpio_num_t)5,  // SDA
            (gpio_num_t)4,  // SCL
            es7210_addr
        );
#endif
        
        input_dev_ = nullptr;
        ESP_LOGI(TAG, "DualI2sAudioCodec 初始化完成（仅输出模式）");
        return;
    }

    es7210_codec_cfg_t es7210_cfg = {};
    es7210_cfg.ctrl_if = in_ctrl_if_;
    
    // ⚠️ 调试：尝试所有4个麦克风输入通道
    // ESP32-S3-Korvo-2 的麦克风可能连接到不同的通道
    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "╔════════════════════════════════════════╗");
    ESP_LOGW(TAG, "║   🎤 ES7210 麦克风通道测试             ║");
    ESP_LOGW(TAG, "╚════════════════════════════════════════╝");
    ESP_LOGW(TAG, "💡 当前尝试 MIC1-MIC4 （D1-D4）所有通道");
    ESP_LOGW(TAG, "💡 如果麦克风连接到MIC2/3/4，会显示全0数据");
    ESP_LOGW(TAG, "");
    
    // 尝试使用所有 4 个麦克风输入（D1+D2+D3+D4）
    es7210_cfg.mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3 | ES7210_SEL_MIC4;
    ESP_LOGI(TAG, "✓ 启用所有麦克风通道：MIC1+MIC2+MIC3+MIC4");
    in_codec_if_ = es7210_codec_new(&es7210_cfg);
    
    if (in_codec_if_ == nullptr) {
        ESP_LOGW(TAG, "⚠️ ES7210 编解码器创建失败（设备可能未焊接）");
        ESP_LOGW(TAG, "⚠️ 音频输入功能将不可用，但输出功能正常");
        
#if ENABLE_ES7210_DIAGNOSTIC
        // 运行完整诊断
        ESP_LOGW(TAG, "");
        ESP_LOGW(TAG, "====================================================");
        ESP_LOGW(TAG, "⚠️ 启动 ES7210 故障诊断");
        ESP_LOGW(TAG, "====================================================");
        ES7210Diagnostic::RunFullDiagnostic(
            (i2c_master_bus_handle_t)i2c_master_handle,
            rx_handle_i2s1_,
            es7210_mclk,
            (gpio_num_t)5,  // SDA
            (gpio_num_t)4,  // SCL
            es7210_addr
        );
#endif
        
        audio_codec_delete_ctrl_if(in_ctrl_if_);
        in_ctrl_if_ = nullptr;
        input_dev_ = nullptr;
        ESP_LOGI(TAG, "DualI2sAudioCodec 初始化完成（仅输出模式）");
        return;
    }

    esp_codec_dev_cfg_t dev_cfg_in = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .codec_if = in_codec_if_,
        .data_if = in_data_if_,
    };
    input_dev_ = esp_codec_dev_new(&dev_cfg_in);
    
    if (input_dev_ == nullptr) {
        ESP_LOGW(TAG, "⚠️ ES7210 设备创建失败（设备可能未焊接）");
        ESP_LOGW(TAG, "⚠️ 音频输入功能将不可用，但输出功能正常");
        
#if ENABLE_ES7210_DIAGNOSTIC
        // 运行完整诊断
        ESP_LOGW(TAG, "");
        ESP_LOGW(TAG, "====================================================");
        ESP_LOGW(TAG, "⚠️ 启动 ES7210 故障诊断");
        ESP_LOGW(TAG, "====================================================");
        ES7210Diagnostic::RunFullDiagnostic(
            (i2c_master_bus_handle_t)i2c_master_handle,
            rx_handle_i2s1_,
            es7210_mclk,
            (gpio_num_t)5,  // SDA
            (gpio_num_t)4,  // SCL
            es7210_addr
        );
#endif
        
        audio_codec_delete_codec_if(in_codec_if_);
        audio_codec_delete_ctrl_if(in_ctrl_if_);
        in_codec_if_ = nullptr;
        in_ctrl_if_ = nullptr;
        ESP_LOGI(TAG, "DualI2sAudioCodec 初始化完成（仅输出模式）");
        return;
    }
    
    ESP_LOGI(TAG, "✅ ES7210 (ADC) 初始化成功（地址 0x%02x）", es7210_addr);
    
    // ========== 配置 MICBIAS 寄存器（针对外部麦克风供电的硬件设计）==========
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║   🔧 配置 ES7210 MICBIAS 寄存器       ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════╝");
    ESP_LOGI(TAG, "💡 硬件说明：麦克风使用外部 3.3V 供电");
    ESP_LOGI(TAG, "💡 需要关闭 ES7210 内部 MICBIAS 检测");
    ESP_LOGI(TAG, "");
    
    // 创建 I2C 设备句柄用于写入寄存器
    {
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = es7210_addr,
            .scl_speed_hz = 100000,
        };
        
        i2c_master_dev_handle_t dev_handle;
        esp_err_t ret = i2c_master_bus_add_device((i2c_master_bus_handle_t)i2c_master_handle, &dev_cfg, &dev_handle);
        
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "【步骤 1】读取 ES7210 初始化后的寄存器状态");
            ESP_LOGI(TAG, "─────────────────────────────────────────");
            
            // 先读取关键寄存器的初始值（ESP-ADF 驱动初始化后的状态）
            auto read_reg = [&](uint8_t addr, const char* name) -> uint8_t {
                uint8_t value = 0xFF;
                uint8_t reg_addr = addr;
                esp_err_t r = i2c_master_transmit_receive(dev_handle, &reg_addr, 1, &value, 1, 1000);
                if (r == ESP_OK) {
                    ESP_LOGI(TAG, "  寄存器 0x%02X (%s) = 0x%02X", addr, name, value);
                } else {
                    ESP_LOGE(TAG, "  寄存器 0x%02X (%s) 读取失败", addr, name);
                }
                return value;
            };
            
            uint8_t reg_0x00 = read_reg(0x00, "CHIP_ID");
            uint8_t reg_0x02 = read_reg(0x02, "CLK_ON_OFF");
            uint8_t reg_0x04 = read_reg(0x04, "MODE_CFG");
            uint8_t reg_0x07 = read_reg(0x07, "PWR_CTRL2/MICBIAS");
            uint8_t reg_0x08 = read_reg(0x08, "PWR_CTRL1/ADC使能");
            uint8_t reg_0x09 = read_reg(0x09, "MIC_EN");
            
            // 读取 PGA 增益寄存器（关键！）
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "  PGA 增益寄存器 (0x10~0x13):");
            uint8_t reg_0x10 = read_reg(0x10, "MIC1_GAIN");
            uint8_t reg_0x11 = read_reg(0x11, "MIC2_GAIN");
            uint8_t reg_0x12 = read_reg(0x12, "MIC3_GAIN");
            uint8_t reg_0x13 = read_reg(0x13, "MIC4_GAIN");
            
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "【步骤 2】分析寄存器状态");
            ESP_LOGI(TAG, "─────────────────────────────────────────");
            
            // 分析 0x08 (最关键！)
            // ⚠️ 重要：bit 4 (0x10) 控制 ADC 电源
            //   - 0x00 = ADC 断电 ❌
            //   - 0x10 = ADC 上电 ✅
            if (reg_0x08 == 0x00) {
                ESP_LOGW(TAG, "  ⚠️  寄存器 0x08 = 0x00: ADC 断电！");
                ESP_LOGW(TAG, "      这是导致麦克风数据全为 0 的根本原因！");
                ESP_LOGW(TAG, "      必须设置为 0x10 才能启动 ADC");
            } else if ((reg_0x08 & 0x10) == 0x10) {
                ESP_LOGI(TAG, "  ✅ 寄存器 0x08 = 0x%02X: ADC 已上电 (bit 4 = 1)", reg_0x08);
            } else {
                ESP_LOGW(TAG, "  ⚠️  寄存器 0x08 = 0x%02X: ADC 断电 (bit 4 = 0)", reg_0x08);
            }
            
            // 分析 0x07
            if ((reg_0x07 & 0x01) == 0x00) {
                ESP_LOGI(TAG, "  ✅ 寄存器 0x07.bit0 = 0: 内部 MICBIAS 已禁用（正确）");
                ESP_LOGI(TAG, "      麦克风使用外部 3.3V 供电");
            } else {
                ESP_LOGW(TAG, "  ⚠️  寄存器 0x07.bit0 = 1: 内部 MICBIAS 已启用（错误）");
                ESP_LOGW(TAG, "      麦克风使用外部 3.3V 供电时，应禁用内部 MICBIAS");
            }
            
            // 分析 0x09
            if (reg_0x09 == 0x0F) {
                ESP_LOGW(TAG, "  ⚠️  寄存器 0x09 = 0x0F: 所有 MIC 通道禁用");
            } else if (reg_0x09 == 0x00) {
                ESP_LOGI(TAG, "  ✅ 寄存器 0x09 = 0x00: 所有 MIC 通道已使能");
            }
            
            // 分析 PGA 增益 (0x10~0x13) - 最关键！
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "  分析 PGA 增益 (模拟放大器):");
            bool pga_all_zero = (reg_0x10 == 0x00 && reg_0x11 == 0x00 && 
                                 reg_0x12 == 0x00 && reg_0x13 == 0x00);
            
            if (pga_all_zero) {
                ESP_LOGW(TAG, "  ⚠️  所有 PGA 增益都是 0 dB！");
                ESP_LOGW(TAG, "      这是导致数据全 0 的最可能原因！");
                ESP_LOGW(TAG, "      ES7210 的信号链：麦克风 → PGA → ADC → I2S");
                ESP_LOGW(TAG, "      如果 PGA = 0dB，模拟信号会非常微弱");
            } else {
                ESP_LOGI(TAG, "  PGA 增益配置:");
                ESP_LOGI(TAG, "    MIC1: +%.1f dB", reg_0x10 * 1.5);
                ESP_LOGI(TAG, "    MIC2: +%.1f dB", reg_0x11 * 1.5);
                ESP_LOGI(TAG, "    MIC3: +%.1f dB", reg_0x12 * 1.5);
                ESP_LOGI(TAG, "    MIC4: +%.1f dB", reg_0x13 * 1.5);
            }
            
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "【步骤 3】针对外部 3.3V 供电麦克风进行配置");
            ESP_LOGI(TAG, "─────────────────────────────────────────");
            
            // 🔧 关键修复：强制配置寄存器 0x08 以确保 ADC 上电
            // ⚠️ 重要：0x08 寄存器的 bit 4 控制 ADC 电源
            //   - 0x00 = ADC 断电 ❌
            //   - 0x10 = ADC 上电 ✅ (bit 4 = 1)
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "  🔧 配置寄存器 0x08 (PWR_CTRL1): ADC 电源控制");
            ESP_LOGI(TAG, "     说明: bit 4 = 1 (0x10) 才能启动 ADC");
            uint8_t reg_0x08_data[2] = {0x08, 0x10};  // 0x10 = ADC 上电 (bit 4 = 1)
            ret = i2c_master_transmit(dev_handle, reg_0x08_data, 2, 1000);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "     ✅ 写入成功: 0x08 = 0x10 (ADC 已上电)");
            } else {
                ESP_LOGE(TAG, "     ❌ 写入失败: %s", esp_err_to_name(ret));
            }
            
            // 寄存器 0x07 (PWR_CTRL2): bit0 控制 MICBIAS
            // bit0=0: MICBIAS 禁用（使用外部 3.3V 供电）
            // bit0=1: MICBIAS 使能（使用内部 BIAS）
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "  🔧 配置寄存器 0x07 (PWR_CTRL2): MICBIAS 控制");
            ESP_LOGI(TAG, "     说明: 麦克风使用外部 3.3V 独立供电，禁用内部 MICBIAS");
            uint8_t reg_0x07_data[2] = {0x07, 0x00};  // bit0=0 禁用内部 MICBIAS
            ret = i2c_master_transmit(dev_handle, reg_0x07_data, 2, 1000);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "     ✅ 写入成功: 0x07 = 0x00 (内部 MICBIAS 已禁用，使用外部供电)");
            } else {
                ESP_LOGE(TAG, "     ❌ 写入失败: %s", esp_err_to_name(ret));
            }
            
            // 寄存器 0x09 (MIC_EN): 0xFF = 所有 MIC 使能
            // 每个 MIC 占 2 bits: 11=使能, 00=禁用
            // MIC4[7:6] | MIC3[5:4] | MIC2[3:2] | MIC1[1:0]
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "  🔧 配置寄存器 0x09 (MIC_EN): MIC 通道使能");
            uint8_t reg_0x09_data[2] = {0x09, 0xFF};  // 0xFF = 所有通道使能
            ret = i2c_master_transmit(dev_handle, reg_0x09_data, 2, 1000);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "     ✅ 写入成功: 0x09 = 0xFF (所有 MIC 通道使能)");
            } else {
                ESP_LOGE(TAG, "     ❌ 写入失败: %s", esp_err_to_name(ret));
            }
            
            // 🔧 关键修复：配置 PGA 增益寄存器 (0x10~0x13)
            // ES7210 的 PGA 增益范围：0x00~0x2E (0~46.5dB, 1.5dB/step)
            // 建议值：0x14 = 30dB（适中增益，避免过饱和）
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "  🔧 配置寄存器 0x10~0x13: MIC1~MIC4 PGA 增益");
            ESP_LOGI(TAG, "     说明: ES7210 的模拟增益 (PGA) 是信号链的第一级放大");
            ESP_LOGI(TAG, "           如果 PGA 增益为 0，数字 ADC 输出会非常微弱或全 0");
            
            // 设置合理的 PGA 增益值
            // 0x14 = 20 * 1.5dB = 30dB
            // 0x20 = 32 * 1.5dB = 48dB（最大安全值）
            // 0x2E = 46 * 1.5dB = 69dB（最大值，但可能过饱和）
            // 可根据实际麦克风灵敏度调整：0x00~0x2E (0~69dB)
            uint8_t pga_gain = 0x20;  // 48dB（从 30dB 提高，对抗 ESP-ADF 降低）
            
            for (uint8_t mic = 0; mic < 4; mic++) {
                uint8_t reg_addr = 0x10 + mic;  // 0x10, 0x11, 0x12, 0x13
                uint8_t reg_data[2] = {reg_addr, pga_gain};
                
                ret = i2c_master_transmit(dev_handle, reg_data, 2, 1000);
            if (ret == ESP_OK) {
                    ESP_LOGI(TAG, "     ✅ MIC%d (0x%02X) = 0x%02X (+%.1f dB)", 
                             mic + 1, reg_addr, pga_gain, pga_gain * 1.5);
                } else {
                    ESP_LOGE(TAG, "     ❌ MIC%d (0x%02X) 写入失败: %s", 
                             mic + 1, reg_addr, esp_err_to_name(ret));
                }
            }
            
            // 验证写入结果
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "【步骤 4】验证寄存器配置结果");
            ESP_LOGI(TAG, "─────────────────────────────────────────");
            
            uint8_t verify_0x08 = read_reg(0x08, "PWR_CTRL1/ADC使能");
            uint8_t verify_0x07 = read_reg(0x07, "PWR_CTRL2/MICBIAS");
            uint8_t verify_0x09 = read_reg(0x09, "MIC_EN");
            
            // 验证 PGA 增益寄存器
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "  验证 PGA 增益寄存器:");
            for (uint8_t mic = 0; mic < 4; mic++) {
                uint8_t verify_gain = read_reg(0x10 + mic, "");
                ESP_LOGI(TAG, "    MIC%d (0x%02X) = 0x%02X (+%.1f dB)", 
                         mic + 1, 0x10 + mic, verify_gain, verify_gain * 1.5);
            }
            
            ESP_LOGI(TAG, "");
            bool config_ok = true;
            
            if (verify_0x08 != 0x10) {
                ESP_LOGW(TAG, "  ⚠️  寄存器 0x08 验证失败: 期望 0x10, 实际 0x%02X", verify_0x08);
                config_ok = false;
            } else {
                ESP_LOGI(TAG, "  ✅ 寄存器 0x08 验证成功: 0x10 (ADC 已上电)");
            }
            
            if (verify_0x07 != 0x00) {
                ESP_LOGW(TAG, "  ⚠️  寄存器 0x07 验证失败: 期望 0x00, 实际 0x%02X", verify_0x07);
                config_ok = false;
            } else {
                ESP_LOGI(TAG, "  ✅ 寄存器 0x07 验证成功: 0x00 (MICBIAS 已禁用，使用外部供电)");
            }
            
            if (verify_0x09 != 0x00) {
                ESP_LOGW(TAG, "  ⚠️  寄存器 0x09 验证失败: 期望 0x00, 实际 0x%02X", verify_0x09);
                config_ok = false;
            } else {
                ESP_LOGI(TAG, "  ✅ 寄存器 0x09 验证成功: 0x00 (MIC 通道已使能)");
            }
            
            i2c_master_bus_rm_device(dev_handle);
            
            ESP_LOGI(TAG, "");
            if (config_ok) {
                ESP_LOGI(TAG, "✅ ES7210 寄存器配置完成（针对外部 3.3V 供电麦克风）");
                ESP_LOGI(TAG, "💡 现在应该可以正常采集麦克风信号了");
            } else {
                ESP_LOGW(TAG, "⚠️  ES7210 寄存器配置存在异常，请检查上述警告");
            }
        } else {
            ESP_LOGE(TAG, "❌ 创建 I2C 设备句柄失败: %s", esp_err_to_name(ret));
            ESP_LOGE(TAG, "⚠️  无法配置 ES7210 寄存器，麦克风可能仍然无法工作");
        }
    }
    ESP_LOGI(TAG, "");
    
    // ========== 初始化完成总结 ==========
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "DualI2sAudioCodec 初始化完成");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "音频输出 (ES8311): %s", output_dev_ ? "✅ 正常" : "❌ 不可用");
    ESP_LOGI(TAG, "音频输入 (ES7210): %s", input_dev_ ? "✅ 正常" : "❌ 不可用");
    ESP_LOGI(TAG, "========================================");
}

void DualI2sAudioCodec::Start() {
    // ⚠️ 注意：不要调用基类的 Start()，因为它会尝试再次启用 I2S 通道
    // 我们已经在构造函数中启用了 I2S（为了在 I2C 配置之前提供 MCLK）
    
    // 从设置中加载输出音量（这是基类 Start() 的第一部分）
    Settings settings("audio", false);
    output_volume_ = settings.GetInt("output_volume", output_volume_);
    if (output_volume_ <= 0) {
        ESP_LOGW(TAG, "输出音量值 (%d) 太小，设置为默认值 (10)", output_volume_);
        output_volume_ = 10;
    }
    
    ESP_LOGI(TAG, "DualI2sAudioCodec::Start() 完成（I2S 通道已在构造函数中启用）");
}

DualI2sAudioCodec::~DualI2sAudioCodec() {
    if (output_dev_) {
        esp_codec_dev_close(output_dev_);
        esp_codec_dev_delete(output_dev_);
    }
    if (input_dev_) {
        esp_codec_dev_close(input_dev_);
        esp_codec_dev_delete(input_dev_);
    }

    if (in_codec_if_) audio_codec_delete_codec_if(in_codec_if_);
    if (in_ctrl_if_) audio_codec_delete_ctrl_if(in_ctrl_if_);
    if (in_data_if_) audio_codec_delete_data_if(in_data_if_);
    
    if (out_codec_if_) audio_codec_delete_codec_if(out_codec_if_);
    if (out_ctrl_if_) audio_codec_delete_ctrl_if(out_ctrl_if_);
    if (out_gpio_if_) audio_codec_delete_gpio_if(out_gpio_if_);
    if (out_data_if_) audio_codec_delete_data_if(out_data_if_);
}

void DualI2sAudioCodec::CreateEs8311Channel(gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, 
                                            gpio_num_t dout, gpio_num_t din) {
    ESP_LOGI(TAG, "创建 ES8311 I2S0 通道: MCLK=%d, BCLK=%d, WS=%d, DOUT=%d, DIN=%d", 
             mclk, bclk, ws, dout, din);

    // ⚠️ 优化：在 I2S 接管 GPIO 之前先尝试设置驱动强度
    // 这样 I2S 驱动可能会保留这个设置
    ESP_LOGI(TAG, "⚡ 预设 GPIO%d (MCLK) 驱动强度为最大 (40mA)...", mclk);
    esp_err_t pre_drive_ret = gpio_set_drive_capability(mclk, GPIO_DRIVE_CAP_3);
    if (pre_drive_ret == ESP_OK) {
        ESP_LOGI(TAG, "  ✅ 预设驱动强度成功");
    } else {
        ESP_LOGW(TAG, "  ⚠️ 预设驱动强度失败: %s", esp_err_to_name(pre_drive_ret));
    }

    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = AUDIO_CODEC_DMA_DESC_NUM,
        .dma_frame_num = AUDIO_CODEC_DMA_FRAME_NUM,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle_i2s0_, nullptr));
    tx_handle_ = tx_handle_i2s0_;  // 设置基类成员

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = (uint32_t)output_sample_rate_,
            .clk_src = I2S_CLK_SRC_PLL_160M,  // ESP32-S3 不支持 APLL，使用 PLL_160M
            .ext_clk_freq_hz = 0,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = I2S_STD_SLOT_BOTH,
            .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = true,
            .big_endian = false,
            .bit_order_lsb = false,
        },
        .gpio_cfg = {
            .mclk = mclk,
            .bclk = bclk,
            .ws = ws,
            .dout = dout,
            .din = I2S_GPIO_UNUSED,  // ES8311 的 DIN 不在标准模式下使用
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle_i2s0_, &std_cfg));
    ESP_LOGI(TAG, "✅ ES8311 I2S0 通道创建成功");
    ESP_LOGI(TAG, "   时钟源：PLL_160M (ESP32-S3 不支持 APLL)");
    ESP_LOGI(TAG, "   MCLK：%d Hz × 256 = %.3f MHz", 
             output_sample_rate_, (output_sample_rate_ * 256) / 1000000.0);
}

void DualI2sAudioCodec::CreateEs7210Channel(gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, 
                                            gpio_num_t din) {
    ESP_LOGI(TAG, "创建 ES7210 I2S1 通道: MCLK=%d, BCLK=%d, WS=%d, DIN=%d", 
             mclk, bclk, ws, din);

    // ⚠️ 优化：在 I2S 接管 GPIO 之前先尝试设置驱动强度
    // 这样 I2S 驱动可能会保留这个设置
    ESP_LOGI(TAG, "⚡ 预设 GPIO%d (MCLK) 驱动强度为最大 (40mA)...", mclk);
    esp_err_t pre_drive_ret = gpio_set_drive_capability(mclk, GPIO_DRIVE_CAP_3);
    if (pre_drive_ret == ESP_OK) {
        ESP_LOGI(TAG, "  ✅ 预设驱动强度成功");
    } else {
        ESP_LOGW(TAG, "  ⚠️ 预设驱动强度失败: %s", esp_err_to_name(pre_drive_ret));
    }

    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_1,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = AUDIO_CODEC_DMA_DESC_NUM,
        .dma_frame_num = AUDIO_CODEC_DMA_FRAME_NUM,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, nullptr, &rx_handle_i2s1_));
    rx_handle_ = rx_handle_i2s1_;  // 设置基类成员

    // ⚠️ 关键修复：ES7210 使用 TDM 模式，4个麦克风在 4 个时间槽中传输数据
    // 参考官方 ESP32-S3-Korvo-2 的 BoxAudioCodec 配置
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║   🎤 配置 ES7210 为 TDM 模式           ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════╝");
    ESP_LOGI(TAG, "💡 ES7210 芯片使用 TDM (Time Division Multiplexing)");
    ESP_LOGI(TAG, "💡 4个麦克风数据在 4 个时间槽中传输");
    ESP_LOGI(TAG, "");
    
    i2s_tdm_config_t tdm_cfg = {
        .clk_cfg = {
            .sample_rate_hz = (uint32_t)input_sample_rate_,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .ext_clk_freq_hz = 0,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
            .bclk_div = 8,  // 参考 BoxAudioCodec
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO,  // TDM 模式使用立体声配置
            // 启用所有 4 个时间槽（对应 4 个麦克风）
            .slot_mask = i2s_tdm_slot_mask_t(I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3),
            .ws_width = I2S_TDM_AUTO_WS_WIDTH,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = false,  // TDM 模式不使用左对齐
            .big_endian = false,
            .bit_order_lsb = false,
            .skip_mask = false,
            .total_slot = I2S_TDM_AUTO_SLOT_NUM
        },
        .gpio_cfg = {
            .mclk = mclk,
            .bclk = bclk,
            .ws = ws,
            .dout = I2S_GPIO_UNUSED,
            .din = din,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    
    ESP_ERROR_CHECK(i2s_channel_init_tdm_mode(rx_handle_i2s1_, &tdm_cfg));
    ESP_LOGI(TAG, "✅ ES7210 I2S1 TDM 通道创建成功");
    ESP_LOGI(TAG, "   模式: TDM (4时间槽)");
    ESP_LOGI(TAG, "   时间槽: SLOT0 + SLOT1 + SLOT2 + SLOT3");
    ESP_LOGI(TAG, "   时钟源：I2S_CLK_SRC_DEFAULT");
    ESP_LOGI(TAG, "   MCLK：%d Hz × 256 = %.3f MHz", 
             input_sample_rate_, (input_sample_rate_ * 256) / 1000000.0);
    ESP_LOGI(TAG, "");
}

int DualI2sAudioCodec::Read(int16_t* dest, int samples) {
    if (!input_dev_) {
        ESP_LOGE(TAG, "❌ Read() 失败：input_dev_ 为空！");
        return 0;
    }
    
    // 🔍 首次调用日志和重试逻辑
    static bool first_call = true;
    static int read_call_count = 0;
    read_call_count++;
    
    if (first_call) {
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "╔════════════════════════════════════════╗");
        ESP_LOGI(TAG, "║   🎤 首次读取麦克风数据                ║");
        ESP_LOGI(TAG, "╚════════════════════════════════════════╝");
        ESP_LOGI(TAG, "请求采样数: %d", samples);
        ESP_LOGI(TAG, "请求字节数: %d", samples * sizeof(int16_t));
        first_call = false;
    }
    
    std::lock_guard<std::mutex> lock(data_if_mutex_);
    
    // ⚠️ 双麦克风 TDM 模式：读取的数据是交错格式
    // 交错格式：CH0, CH1, CH0, CH1, CH0, CH1, ...
    // 我们需要读取 2*samples 个数据，然后混合为 samples 个单声道数据
    static std::vector<int16_t> tdm_buffer;
    tdm_buffer.resize(samples * 2);  // 2个通道的数据
    
    esp_err_t ret = esp_codec_dev_read(input_dev_, tdm_buffer.data(), samples * 2 * sizeof(int16_t));
    
    if (ret != ESP_OK) {
        // 只有在真正出错时才记录
        if (read_call_count <= 5) {  // 前 5 次调用显示错误
            ESP_LOGE(TAG, "❌ esp_codec_dev_read() 错误: %s (第%d次调用)", esp_err_to_name(ret), read_call_count);
        }
        return 0;
    }
    
    // 将2通道数据混合为单声道：(CH0 + CH1) / 2
    // TDM 交错格式：tdm_buffer[0]=CH0, tdm_buffer[1]=CH1, tdm_buffer[2]=CH0, tdm_buffer[3]=CH1, ...
    for (int i = 0; i < samples; i++) {
        int ch0 = tdm_buffer[i * 2];      // 通道0 (MIC1)
        int ch1 = tdm_buffer[i * 2 + 1];  // 通道1 (MIC2)
        dest[i] = (ch0 + ch1) / 2;        // 混合平均
    }
    
    // 🔍 诊断：每隔一段时间打印双麦克风数据
    static int read_count = 0;
    if (++read_count % 200 == 0) {  // 每200次打印一次
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "╔════════════════════════════════════════╗");
        ESP_LOGI(TAG, "║   🎤 双麦克风 TDM 数据诊断            ║");
        ESP_LOGI(TAG, "╚════════════════════════════════════════╝");
        
        // 显示 TDM 原始交错数据（前8个：CH0, CH1, CH0, CH1, ...）
        ESP_LOGI(TAG, "📊 TDM 原始数据（交错格式 CH0/CH1）:");
        ESP_LOGI(TAG, "   [%d, %d] [%d, %d] [%d, %d] [%d, %d]",
                 tdm_buffer[0], tdm_buffer[1],    // 第1组
                 tdm_buffer[2], tdm_buffer[3],    // 第2组
                 tdm_buffer[4], tdm_buffer[5],    // 第3组
                 tdm_buffer[6], tdm_buffer[7]);   // 第4组
        
        // 显示混合后的单声道数据
        ESP_LOGI(TAG, "📊 混合输出（单声道）:");
        ESP_LOGI(TAG, "   %d, %d, %d, %d, %d, %d, %d, %d",
                 dest[0], dest[1], dest[2], dest[3],
                 dest[4], dest[5], dest[6], dest[7]);
        
        // 分别计算2个通道的能量
        int64_t sum_ch0 = 0, sum_ch1 = 0, sum_mixed = 0;
        int max_ch0 = 0, max_ch1 = 0, max_mixed = 0;
        
        for (int i = 0; i < samples; i++) {
            int ch0_val = abs(tdm_buffer[i * 2]);
            int ch1_val = abs(tdm_buffer[i * 2 + 1]);
            int mixed_val = abs(dest[i]);
            
            sum_ch0 += ch0_val;
            sum_ch1 += ch1_val;
            sum_mixed += mixed_val;
            
            if (ch0_val > max_ch0) max_ch0 = ch0_val;
            if (ch1_val > max_ch1) max_ch1 = ch1_val;
            if (mixed_val > max_mixed) max_mixed = mixed_val;
        }
        
        int avg_ch0 = samples > 0 ? sum_ch0 / samples : 0;
        int avg_ch1 = samples > 0 ? sum_ch1 / samples : 0;
        int avg_mixed = samples > 0 ? sum_mixed / samples : 0;
        
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "📊 各通道音频能量:");
        ESP_LOGI(TAG, "   通道0 (MIC1): 平均=%d, 最大=%d %s",
                 avg_ch0, max_ch0, max_ch0 < 10 ? "❌ 无信号" : "✅");
        ESP_LOGI(TAG, "   通道1 (MIC2): 平均=%d, 最大=%d %s",
                 avg_ch1, max_ch1, max_ch1 < 10 ? "❌ 无信号" : "✅");
        ESP_LOGI(TAG, "   混合输出:     平均=%d, 最大=%d",
                 avg_mixed, max_mixed);
        
        ESP_LOGI(TAG, "");
        
        // 判断麦克风状态
        bool mic1_active = max_ch0 > 10;
        bool mic2_active = max_ch1 > 10;
        
        if (!mic1_active && !mic2_active) {
            ESP_LOGW(TAG, "⚠️ 两个麦克风都无信号！");
            ESP_LOGW(TAG, "╔════════════════════════════════════════╗");
            ESP_LOGW(TAG, "║   ❌ 硬件检查清单                      ║");
            ESP_LOGW(TAG, "╚════════════════════════════════════════╝");
            ESP_LOGW(TAG, "1. 确认 MIC2 (MIC2N/MIC2P) 已焊接");
            ESP_LOGW(TAG, "2. 检查麦克风供电（VCC 3.3V）");
            ESP_LOGW(TAG, "3. 检查 ES7210 供电 (VDD/DVDD)");
            ESP_LOGW(TAG, "4. 检查 I2S 数据线 GPIO 11");
            ESP_LOGW(TAG, "5. 用示波器检查 BCLK/WS/DOUT 信号");
        } else if (!mic1_active && mic2_active) {
            ESP_LOGI(TAG, "✅ 单麦克风模式运行正常");
            ESP_LOGI(TAG, "   • MIC1 (通道0): 未焊接（预期）");
            ESP_LOGI(TAG, "   • MIC2 (通道1): 工作正常 ✅");
        } else if (mic1_active && !mic2_active) {
            ESP_LOGW(TAG, "⚠️ 意外：MIC1有信号，MIC2无信号");
            ESP_LOGW(TAG, "   请检查硬件连接是否正确");
        } else {
            ESP_LOGI(TAG, "✅ 双麦克风模式运行正常");
            ESP_LOGI(TAG, "   • MIC1 (通道0): 工作正常 ✅");
            ESP_LOGI(TAG, "   • MIC2 (通道1): 工作正常 ✅");
            ESP_LOGI(TAG, "   • 混合输出: 双麦克风降噪效果");
        }
        ESP_LOGI(TAG, "");
    }
    
    // TDM 模式：成功读取，返回采样数
    return samples;
}

int DualI2sAudioCodec::Write(const int16_t* data, int samples) {
    if (!output_dev_) {
        return 0;
    }
    
    std::lock_guard<std::mutex> lock(data_if_mutex_);
    int bytes_written = esp_codec_dev_write(output_dev_, (void*)data, samples * sizeof(int16_t));
    if (bytes_written < 0) {
        ESP_LOGE(TAG, "写入音频数据失败: %d", bytes_written);
        return 0;
    }
    return bytes_written / sizeof(int16_t);
}

void DualI2sAudioCodec::SetOutputVolume(int volume) {
    output_volume_ = volume;
    if (output_dev_) {
        esp_codec_dev_set_out_vol(output_dev_, volume);
        ESP_LOGI(TAG, "设置输出音量: %d", volume);
    }
}

void DualI2sAudioCodec::EnableInput(bool enable) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║   🎤 EnableInput(%s)                   ║", enable ? "true " : "false");
    ESP_LOGI(TAG, "╚════════════════════════════════════════╝");
    
    input_enabled_ = enable;
    if (input_dev_) {
        if (enable) {
            ESP_LOGI(TAG, "📝 配置采样参数:");
            ESP_LOGI(TAG, "   - bits_per_sample: 16");
            ESP_LOGI(TAG, "   - channel: 1 (单声道模式)");
            ESP_LOGI(TAG, "   - channel_mask: 0x2 (只使用 SLOT1，即 MIC2)");
            ESP_LOGI(TAG, "   - sample_rate: %d", input_sample_rate_);
            
            // ⚠️ 单麦克风配置：只读取 MIC2 (通道1/SLOT1)
            // 原因：MIC1 信号太弱（avg=1-24），拖累整体性能
            // MIC2 信号已足够（avg=800-3370），满足语音唤醒要求
            // 硬件连接：
            //   - MIC1: 未使用（信号太弱，avg < 50）
            //   - MIC2: MIC2N/MIC2P (通道1/SLOT1) - 已焊接 ✅
            esp_codec_dev_sample_info_t fs = {
                .bits_per_sample = 16,
                .channel = 1,  // 单声道模式
                // 只读取通道1 (MIC2) - 信号强度足够，避免被 MIC1 拉低
                .channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1),
                .sample_rate = (uint32_t)input_sample_rate_,
                .mclk_multiple = 0,
            };
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "╔════════════════════════════════════════╗");
            ESP_LOGI(TAG, "║   🎤 单麦克风模式（仅 MIC2）          ║");
            ESP_LOGI(TAG, "╚════════════════════════════════════════╝");
            ESP_LOGI(TAG, "💡 MIC2 (SLOT1/通道1): 主麦克风 ✅");
            ESP_LOGI(TAG, "💡 MIC1 (SLOT0/通道0): 未使用（信号太弱）");
            ESP_LOGI(TAG, "💡 数据输出：MIC2 单声道直接输出");
            ESP_LOGI(TAG, "💡 预期信号强度：avg=800-3000（语音唤醒要求）");
            ESP_LOGI(TAG, "");
            
            ESP_LOGI(TAG, "🔧 调用 esp_codec_dev_open()...");
            esp_err_t ret = esp_codec_dev_open(input_dev_, &fs);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "❌ 启用输入失败: %s (错误码: 0x%x)", esp_err_to_name(ret), ret);
            } else {
                ESP_LOGI(TAG, "✅ esp_codec_dev_open() 成功");
                
                // ⚠️ 关键修复：TDM 模式需要为每个通道单独设置增益
                // 参考 BoxAudioCodec: 使用 esp_codec_dev_set_in_channel_gain()
                float gain_float = (float)input_gain_;
                ESP_LOGI(TAG, "🔧 为所有通道设置增益 %.1f dB...", gain_float);
                
                // 为 4 个通道都设置增益（虽然我们只使用 SLOT0）
                for (int ch = 0; ch < 4; ch++) {
                    ret = esp_codec_dev_set_in_channel_gain(input_dev_, ESP_CODEC_DEV_MAKE_CHANNEL_MASK(ch), gain_float);
                    if (ret == ESP_OK) {
                        ESP_LOGI(TAG, "✅ 通道 %d 增益设置为: %.1f dB", ch, gain_float);
                    } else {
                        ESP_LOGW(TAG, "⚠️ 通道 %d 增益设置失败: %s", ch, esp_err_to_name(ret));
                    }
                }
                
                // ⚠️ 关键修复：等待 I2S DMA 开始工作
                ESP_LOGI(TAG, "⏳ 等待 I2S DMA 缓冲区填充（100ms）...");
                vTaskDelay(pdMS_TO_TICKS(100));
                
                ESP_LOGI(TAG, "✅ 输入通道已启用，准备读取数据");
                
                // ⚠️ 关键修复：强制重新配置 PGA 增益（防止被 ESP-ADF 覆盖）
                ESP_LOGI(TAG, "");
                ESP_LOGI(TAG, "🔧 强制重新配置 ES7210 PGA 增益（在 EnableInput 之后）");
                {
                    i2c_device_config_t dev_cfg = {
                        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                        .device_address = 0x40,  // ES7210 I2C 地址
                        .scl_speed_hz = 100000,
                    };
                    
                    i2c_master_dev_handle_t dev_handle;
                    esp_err_t ret = i2c_master_bus_add_device((i2c_master_bus_handle_t)i2c_master_handle_, &dev_cfg, &dev_handle);
                    
                    if (ret == ESP_OK) {
                        // 读取当前 PGA 值
                        auto read_reg = [&](uint8_t addr) -> uint8_t {
                            uint8_t value = 0xFF;
                            uint8_t reg_addr = addr;
                            i2c_master_transmit_receive(dev_handle, &reg_addr, 1, &value, 1, 1000);
                            return value;
                        };
                        
                        ESP_LOGI(TAG, "  读取当前 PGA 值:");
                        uint8_t pga1_before = read_reg(0x10);
                        uint8_t pga2_before = read_reg(0x11);
                        uint8_t pga3_before = read_reg(0x12);
                        uint8_t pga4_before = read_reg(0x13);
                        ESP_LOGI(TAG, "    MIC1=0x%02X (%.1fdB), MIC2=0x%02X (%.1fdB), MIC3=0x%02X (%.1fdB), MIC4=0x%02X (%.1fdB)", 
                                 pga1_before, pga1_before * 1.5, 
                                 pga2_before, pga2_before * 1.5,
                                 pga3_before, pga3_before * 1.5,
                                 pga4_before, pga4_before * 1.5);
                        
                        // 强制写入 PGA 增益值 0x20 = +48dB
                        // 提高到 48dB 以对抗 ESP-ADF 的降低（预期被降到 ~35-40dB）
                        uint8_t pga_gain = 0x20;  // +48dB
                        ESP_LOGI(TAG, "  强制写入 PGA 增益 0x%02X (%.1fdB) 到所有通道...", pga_gain, pga_gain * 1.5);
                        
                        for (uint8_t mic = 0; mic < 4; mic++) {
                            uint8_t reg_addr = 0x10 + mic;
                            uint8_t reg_data[2] = {reg_addr, pga_gain};
                            ret = i2c_master_transmit(dev_handle, reg_data, 2, 1000);
                            if (ret == ESP_OK) {
                                ESP_LOGI(TAG, "    ✅ MIC%d (0x%02X) 写入成功", mic + 1, reg_addr);
                            } else {
                                ESP_LOGE(TAG, "    ❌ MIC%d (0x%02X) 写入失败", mic + 1, reg_addr);
                            }
                        }
                        
                        // 等待寄存器生效
                        vTaskDelay(pdMS_TO_TICKS(10));
                        
                        // 验证写入结果
                        uint8_t pga1_after = read_reg(0x10);
                        uint8_t pga2_after = read_reg(0x11);
                        uint8_t pga3_after = read_reg(0x12);
                        uint8_t pga4_after = read_reg(0x13);
                        ESP_LOGI(TAG, "  验证 PGA 值:");
                        ESP_LOGI(TAG, "    MIC1=0x%02X (%.1fdB), MIC2=0x%02X (%.1fdB), MIC3=0x%02X (%.1fdB), MIC4=0x%02X (%.1fdB)", 
                                 pga1_after, pga1_after * 1.5, 
                                 pga2_after, pga2_after * 1.5,
                                 pga3_after, pga3_after * 1.5,
                                 pga4_after, pga4_after * 1.5);
                        
                        if (pga1_after == pga_gain && pga2_after == pga_gain && 
                            pga3_after == pga_gain && pga4_after == pga_gain) {
                            ESP_LOGI(TAG, "  ✅ PGA 配置成功并保持稳定！");
                        } else {
                            ESP_LOGW(TAG, "  ⚠️ PGA 配置后值仍然异常，可能被 ESP-ADF 持续覆盖");
                        }
                        
                        // 🔧 关键修复：重新配置 MIC_EN (0x09) 寄存器
                        // ESP-ADF 的 esp_codec_dev_open() 会重新初始化 ES7210，把 MIC_EN 改回默认值
                        ESP_LOGI(TAG, "");
                        ESP_LOGI(TAG, "  🔧 强制重新配置 MIC_EN (0x09) 寄存器");
                        uint8_t mic_en_before = read_reg(0x09);
                        ESP_LOGI(TAG, "    读取当前值: 0x09 = 0x%02X", mic_en_before);
                        
                        // 写入 0xFF = 所有 MIC 使能
                        uint8_t reg_0x09_data[2] = {0x09, 0xFF};
                        ret = i2c_master_transmit(dev_handle, reg_0x09_data, 2, 1000);
                        if (ret == ESP_OK) {
                            ESP_LOGI(TAG, "    ✅ MIC_EN 写入成功: 0x09 = 0xFF");
                        } else {
                            ESP_LOGE(TAG, "    ❌ MIC_EN 写入失败: %s", esp_err_to_name(ret));
                        }
                        
                        // 等待并验证
                        vTaskDelay(pdMS_TO_TICKS(10));
                        uint8_t mic_en_after = read_reg(0x09);
                        ESP_LOGI(TAG, "    验证: 0x09 = 0x%02X", mic_en_after);
                        
                        if (mic_en_after == 0xFF) {
                            ESP_LOGI(TAG, "  ✅ MIC_EN 配置成功！所有麦克风已启用！");
                        } else {
                            ESP_LOGW(TAG, "  ⚠️ MIC_EN 配置失败！期望 0xFF，实际 0x%02X", mic_en_after);
                        }
                        
                        i2c_master_bus_rm_device(dev_handle);
                    } else {
                        ESP_LOGE(TAG, "  ❌ 无法创建 I2C 设备句柄: %s", esp_err_to_name(ret));
                    }
                }
                ESP_LOGI(TAG, "");
            }
            ESP_LOGI(TAG, "");
        } else {
            esp_codec_dev_close(input_dev_);
            ESP_LOGI(TAG, "禁用输入");
        }
    } else {
        ESP_LOGE(TAG, "❌ input_dev_ 为空，无法启用输入！");
    }
}

void DualI2sAudioCodec::EnableOutput(bool enable) {
    output_enabled_ = enable;
    if (output_dev_) {
        if (enable) {
            esp_codec_dev_open(output_dev_, nullptr);
            ESP_LOGI(TAG, "启用输出");
        } else {
            esp_codec_dev_close(output_dev_);
            ESP_LOGI(TAG, "禁用输出");
        }
    }
}

