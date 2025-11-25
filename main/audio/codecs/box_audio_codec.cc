#include "box_audio_codec.h"

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <driver/i2s_tdm.h>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "settings.h"

#define TAG "BoxAudioCodec"

BoxAudioCodec::BoxAudioCodec(void* i2c_master_handle, int input_sample_rate, int output_sample_rate,
    gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din,
    gpio_num_t pa_pin, uint8_t es8311_addr, uint8_t es7210_addr, bool input_reference) {
    duplex_ = true; // 是否双工
    input_reference_ = input_reference; // 是否使用参考输入，实现回声消除
    // 🔧 ES7210 在 TDM 模式下配置为 4 通道，这里必须匹配硬件配置
    input_channels_ = 4;  // ES7210 TDM 模式固定为 4 通道
    input_sample_rate_ = input_sample_rate;
    output_sample_rate_ = output_sample_rate;
    
    // 🎤 麦克风增益设置（范围 0-47 dB）
    // 🔧 ES7210 + ESPHome标准noise reduction (0.05) 需要极高增益
    // 测试记录：
    //   - 42 dB: Frontend 输出大量零值 (zero_count=30-40/40) ❌
    //   - 45 dB: 仍有过度抑制 ❌
    //   - 47 dB: 最大增益，配合标准 noise reduction ✅
    input_gain_ = 47;  // 最大值：ES7210 4-mic + ESPHome 标准参数
    
    ESP_LOGI(TAG, "🎤 BoxAudioCodec constructor: input_sample_rate=%d, output_sample_rate=%d, input_reference=%d, input_channels=%d (4-ch TDM), input_gain=%.1f dB",
             input_sample_rate_, output_sample_rate_, input_reference_, input_channels_, input_gain_);

    CreateDuplexChannels(mclk, bclk, ws, dout, din);

    // Do initialize of related interface: data_if, ctrl_if and gpio_if
    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_NUM_0,
        .rx_handle = rx_handle_,
        .tx_handle = tx_handle_,
    };
    data_if_ = audio_codec_new_i2s_data(&i2s_cfg);
    assert(data_if_ != NULL);

    // Output
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = (i2c_port_t)1,
        .addr = es8311_addr,
        .bus_handle = i2c_master_handle,
    };
    out_ctrl_if_ = audio_codec_new_i2c_ctrl(&i2c_cfg);
    assert(out_ctrl_if_ != NULL);

    gpio_if_ = audio_codec_new_gpio();
    assert(gpio_if_ != NULL);

    es8311_codec_cfg_t es8311_cfg = {};
    es8311_cfg.ctrl_if = out_ctrl_if_;
    es8311_cfg.gpio_if = gpio_if_;
    es8311_cfg.codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC;
    es8311_cfg.pa_pin = pa_pin;
    es8311_cfg.use_mclk = true;
    es8311_cfg.hw_gain.pa_voltage = 5.0;
    es8311_cfg.hw_gain.codec_dac_voltage = 3.3;
    out_codec_if_ = es8311_codec_new(&es8311_cfg);
    assert(out_codec_if_ != NULL);

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = out_codec_if_,
        .data_if = data_if_,
    };
    output_dev_ = esp_codec_dev_new(&dev_cfg);
    assert(output_dev_ != NULL);

    // Input
    i2c_cfg.addr = es7210_addr;
    in_ctrl_if_ = audio_codec_new_i2c_ctrl(&i2c_cfg);
    assert(in_ctrl_if_ != NULL);

    es7210_codec_cfg_t es7210_cfg = {};
    es7210_cfg.ctrl_if = in_ctrl_if_;
    es7210_cfg.mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3 | ES7210_SEL_MIC4;
    in_codec_if_ = es7210_codec_new(&es7210_cfg);
    assert(in_codec_if_ != NULL);

    dev_cfg.dev_type = ESP_CODEC_DEV_TYPE_IN;
    dev_cfg.codec_if = in_codec_if_;
    input_dev_ = esp_codec_dev_new(&dev_cfg);
    assert(input_dev_ != NULL);

    // 关键修复：在双工模式下，input_dev 和 output_dev 共享同一个 data_if_
    // 防止 close 一个设备时禁用底层的 I2S 通道，影响另一个设备
    esp_codec_set_disable_when_closed(output_dev_, false);
    esp_codec_set_disable_when_closed(input_dev_, false);
    
    // 双工模式优化：在初始化时就打开输入设备，保持打开状态
    // 避免频繁 close/open 导致的 I2S 状态问题
    uint16_t input_channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0);
    if (input_reference_) {
        input_channel_mask |= ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1);
    }
    esp_codec_dev_sample_info_t input_fs = {
        .bits_per_sample = 16,
        .channel = 4,
        .channel_mask = input_channel_mask,
        .sample_rate = (uint32_t)input_sample_rate_,  // ✅ 修复：应该用 input_sample_rate_ 而不是 output_sample_rate_
        .mclk_multiple = 0,
    };
    esp_err_t ret = esp_codec_dev_open(input_dev_, &input_fs);
    if (ret == ESP_OK) {
        // 设置所有通道的增益，并验证是否设置成功
        ESP_LOGI(TAG, "🔧 Setting input gain to %.1f dB for all 4 channels...", (float)input_gain_);
        for (int ch = 0; ch < 4; ch++) {
            esp_err_t gain_ret = esp_codec_dev_set_in_channel_gain(input_dev_, ESP_CODEC_DEV_MAKE_CHANNEL_MASK(ch), (float)input_gain_);
            if (gain_ret == ESP_OK) {
                ESP_LOGI(TAG, "✅ Channel %d gain set to %.1f dB", ch, (float)input_gain_);
            } else {
                ESP_LOGW(TAG, "⚠️ Channel %d gain set failed: %s", ch, esp_err_to_name(gain_ret));
            }
        }
        input_device_permanently_open_ = true;
        ESP_LOGI(TAG, "Input device opened and will remain open (duplex mode optimization)");
    } else {
        ESP_LOGW(TAG, "Failed to pre-open input device: %s, will use dynamic open/close", esp_err_to_name(ret));
        input_device_permanently_open_ = false;
    }

    ESP_LOGI(TAG, "BoxAudioDevice initialized");
}

BoxAudioCodec::~BoxAudioCodec() {
    ESP_ERROR_CHECK(esp_codec_dev_close(output_dev_));
    esp_codec_dev_delete(output_dev_);
    ESP_ERROR_CHECK(esp_codec_dev_close(input_dev_));
    esp_codec_dev_delete(input_dev_);

    audio_codec_delete_codec_if(in_codec_if_);
    audio_codec_delete_ctrl_if(in_ctrl_if_);
    audio_codec_delete_codec_if(out_codec_if_);
    audio_codec_delete_ctrl_if(out_ctrl_if_);
    audio_codec_delete_gpio_if(gpio_if_);
    audio_codec_delete_data_if(data_if_);
}

void BoxAudioCodec::CreateDuplexChannels(gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din) {
    assert(input_sample_rate_ == output_sample_rate_);

    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = AUDIO_CODEC_DMA_DESC_NUM,
        .dma_frame_num = AUDIO_CODEC_DMA_FRAME_NUM,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle_, &rx_handle_));

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = (uint32_t)output_sample_rate_,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .ext_clk_freq_hz = 0,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256
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
            .bit_order_lsb = false
        },
        .gpio_cfg = {
            .mclk = mclk,
            .bclk = bclk,
            .ws = ws,
            .dout = dout,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false
            }
        }
    };

    i2s_tdm_config_t tdm_cfg = {
        .clk_cfg = {
            .sample_rate_hz = (uint32_t)input_sample_rate_,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .ext_clk_freq_hz = 0,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
            .bclk_div = 8,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = i2s_tdm_slot_mask_t(I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3),
            .ws_width = I2S_TDM_AUTO_WS_WIDTH,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = false,
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
                .ws_inv = false
            }
        }
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle_, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_tdm_mode(rx_handle_, &tdm_cfg));
    ESP_LOGI(TAG, "Duplex channels created");
}

void BoxAudioCodec::Start() {
    // 如果输入设备永久打开，I2S 通道已经全部 enabled（双工模式共享 I2S 接口）
    if (input_device_permanently_open_) {
        ESP_LOGI(TAG, "Starting audio codec (I2S channels already enabled in duplex mode)");
        
        // 读取音量设置（与父类 AudioCodec::Start() 相同）
        Settings settings("audio", false);
        output_volume_ = settings.GetInt("output_volume", output_volume_);
        if (output_volume_ <= 0) {
            ESP_LOGW(TAG, "Output volume value (%d) is too small, setting to default (10)", output_volume_);
            output_volume_ = 10;
        }
        
        // 不需要 enable 任何 I2S 通道，esp_codec_dev_open(input_dev_) 已经 enable 了
        // rx_handle_ 和 tx_handle_（因为它们共享同一个 I2S 接口）
        
        EnableInput(true);
        EnableOutput(true);
        ESP_LOGI(TAG, "Audio codec started");
    } else {
        // 回退到标准实现
        AudioCodec::Start();
    }
}

void BoxAudioCodec::SetOutputVolume(int volume) {
    ESP_LOGI(TAG, "🔊 设置音量: %d -> %d", output_volume_, volume);
    ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(output_dev_, volume));
    AudioCodec::SetOutputVolume(volume);
}

void BoxAudioCodec::EnableInput(bool enable) {
    std::lock_guard<std::mutex> lock(data_if_mutex_);
    if (enable == input_enabled_) {
        return;
    }
    
    // 双工模式优化：如果输入设备永久打开，不做实际的 close/open
    if (input_device_permanently_open_) {
        ESP_LOGI(TAG, "Input device permanently open, only updating enabled flag: %d -> %d", 
                 input_enabled_, enable);
        AudioCodec::EnableInput(enable);
        return;
    }
    
    // 原有逻辑：动态 open/close（作为 fallback）
    if (enable) {
        // 在双工模式下，给硬件一些时间稳定（避免快速 close/open）
        vTaskDelay(pdMS_TO_TICKS(10));  // 10ms 延迟
        
        esp_codec_dev_sample_info_t fs = {
            .bits_per_sample = 16,
            .channel = 4,
            .channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0),
            .sample_rate = (uint32_t)input_sample_rate_,  // ✅ 修复：应该用 input_sample_rate_
            .mclk_multiple = 0,
        };
        if (input_reference_) {
            fs.channel_mask |= ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1);
        }
        ESP_LOGI(TAG, "EnableInput: sample_rate=%d (input_sample_rate_), channel=4, channel_mask=0x%x, input_gain=%d, input_reference=%d",
                 fs.sample_rate, fs.channel_mask, input_gain_, input_reference_);
        
        esp_err_t ret = esp_codec_dev_open(input_dev_, &fs);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to open input device: %s (0x%x)", esp_err_to_name(ret), ret);
            return;  // 失败时不更新状态
        }
        
        // 设置所有通道的增益，并验证是否设置成功
        for (int ch = 0; ch < 4; ch++) {
            esp_err_t ret = esp_codec_dev_set_in_channel_gain(input_dev_, ESP_CODEC_DEV_MAKE_CHANNEL_MASK(ch), input_gain_);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "✅ Channel %d gain set to %d dB", ch, input_gain_);
            } else {
                ESP_LOGW(TAG, "⚠️ Channel %d gain set failed: %s", ch, esp_err_to_name(ret));
            }
        }
        ESP_LOGI(TAG, "Input device opened successfully, actual input_sample_rate_=%d", input_sample_rate_);
    } else {
        ESP_ERROR_CHECK(esp_codec_dev_close(input_dev_));
        // 关闭后给硬件时间稳定
        vTaskDelay(pdMS_TO_TICKS(10));  // 10ms 延迟
    }
    AudioCodec::EnableInput(enable);
}

void BoxAudioCodec::EnableOutput(bool enable) {
    std::lock_guard<std::mutex> lock(data_if_mutex_);
    if (enable == output_enabled_) {
        return;
    }
    if (enable) {
        // 在双工模式下，给硬件一些时间稳定（避免快速 close/open）
        vTaskDelay(pdMS_TO_TICKS(10));  // 10ms 延迟
        
        // Play 16bit 1 channel
        esp_codec_dev_sample_info_t fs = {
            .bits_per_sample = 16,
            .channel = 1,
            .channel_mask = 0,
            .sample_rate = (uint32_t)output_sample_rate_,
            .mclk_multiple = 0,
        };
        
        esp_err_t ret = esp_codec_dev_open(output_dev_, &fs);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to open output device: %s (0x%x)", esp_err_to_name(ret), ret);
            return;  // 失败时不更新状态
        }
        
        ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(output_dev_, output_volume_));
        ESP_LOGI(TAG, "🔊 音频输出已启用: 采样率=%d Hz, 音量=%d/100", output_sample_rate_, output_volume_);
    } else {
        ESP_LOGI(TAG, "🔇 音频输出已关闭");
        ESP_ERROR_CHECK(esp_codec_dev_close(output_dev_));
        // 关闭后给硬件时间稳定
        vTaskDelay(pdMS_TO_TICKS(10));  // 10ms 延迟
    }
    AudioCodec::EnableOutput(enable);
}

int BoxAudioCodec::Read(int16_t* dest, int samples) {
    std::lock_guard<std::mutex> lock(data_if_mutex_);
    if (!input_enabled_) {
        // 输入未启用，返回静音数据
        memset(dest, 0, samples * sizeof(int16_t));
        return samples;
    }
    
    esp_err_t ret = esp_codec_dev_read(input_dev_, (void*)dest, samples * sizeof(int16_t));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Read failed: %s (0x%x), input_enabled=%d", 
                 esp_err_to_name(ret), ret, input_enabled_);
        memset(dest, 0, samples * sizeof(int16_t));
    }
    return samples;
}

int BoxAudioCodec::Write(const int16_t* data, int samples) {
    std::lock_guard<std::mutex> lock(data_if_mutex_);
    if (!output_enabled_) {
        // 输出未启用，静默丢弃数据
        return samples;
    }
    
    esp_err_t ret = esp_codec_dev_write(output_dev_, (void*)data, samples * sizeof(int16_t));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Write failed: %s (0x%x), output_enabled=%d", 
                 esp_err_to_name(ret), ret, output_enabled_);
    }
    return samples;
}