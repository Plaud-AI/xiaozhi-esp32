#include "dual_i2s_audio_codec.h"

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <driver/i2s_std.h>

#define TAG "DualI2sAudioCodec"
//xxx
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
    duplex_ = true;
    input_reference_ = false;  // 单麦克风，无回声消除
    input_channels_ = 1;
    input_sample_rate_ = input_sample_rate;
    output_sample_rate_ = output_sample_rate;
    input_gain_ = 30;  // 默认增益
    
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

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = (i2c_port_t)1,
        .addr = es8311_addr,
        .bus_handle = i2c_master_handle,
    };
    out_ctrl_if_ = audio_codec_new_i2c_ctrl(&i2c_cfg);
    assert(out_ctrl_if_ != nullptr);

    out_gpio_if_ = audio_codec_new_gpio();
    assert(out_gpio_if_ != nullptr);

    es8311_codec_cfg_t es8311_cfg = {};
    es8311_cfg.ctrl_if = out_ctrl_if_;
    es8311_cfg.gpio_if = out_gpio_if_;
    es8311_cfg.codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC;
    es8311_cfg.pa_pin = pa_pin;
    es8311_cfg.use_mclk = true;
    es8311_cfg.hw_gain.pa_voltage = 5.0;
    es8311_cfg.hw_gain.codec_dac_voltage = 3.3;
    out_codec_if_ = es8311_codec_new(&es8311_cfg);
    assert(out_codec_if_ != nullptr);

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = out_codec_if_,
        .data_if = out_data_if_,
    };
    output_dev_ = esp_codec_dev_new(&dev_cfg);
    assert(output_dev_ != nullptr);
    
    ESP_LOGI(TAG, "ES8311 (DAC) 初始化完成");

    // ========== 初始化 ES7210 (ADC/输入) ==========
    audio_codec_i2s_cfg_t i2s_in_cfg = {
        .port = I2S_NUM_1,
        .rx_handle = rx_handle_i2s1_,
        .tx_handle = nullptr,
    };
    in_data_if_ = audio_codec_new_i2s_data(&i2s_in_cfg);
    assert(in_data_if_ != nullptr);

    i2c_cfg.addr = es7210_addr;
    in_ctrl_if_ = audio_codec_new_i2c_ctrl(&i2c_cfg);
    assert(in_ctrl_if_ != nullptr);

    es7210_codec_cfg_t es7210_cfg = {};
    es7210_cfg.ctrl_if = in_ctrl_if_;
    es7210_cfg.mic_selected = ES7210_SEL_MIC1;  // 使用单麦克风
    in_codec_if_ = es7210_codec_new(&es7210_cfg);
    assert(in_codec_if_ != nullptr);

    dev_cfg.dev_type = ESP_CODEC_DEV_TYPE_IN;
    dev_cfg.codec_if = in_codec_if_;
    dev_cfg.data_if = in_data_if_;
    input_dev_ = esp_codec_dev_new(&dev_cfg);
    assert(input_dev_ != nullptr);
    
    ESP_LOGI(TAG, "ES7210 (ADC) 初始化完成");
    ESP_LOGI(TAG, "DualI2sAudioCodec 完全初始化");
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
            .clk_src = I2S_CLK_SRC_DEFAULT,
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
    ESP_LOGI(TAG, "ES8311 I2S0 通道创建成功");
}

void DualI2sAudioCodec::CreateEs7210Channel(gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, 
                                            gpio_num_t din) {
    ESP_LOGI(TAG, "创建 ES7210 I2S1 通道: MCLK=%d, BCLK=%d, WS=%d, DIN=%d", 
             mclk, bclk, ws, din);

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

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = (uint32_t)input_sample_rate_,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .ext_clk_freq_hz = 0,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_MONO,  // 单声道
            .slot_mask = I2S_STD_SLOT_LEFT,   // 使用左声道
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
            .dout = I2S_GPIO_UNUSED,
            .din = din,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle_i2s1_, &std_cfg));
    ESP_LOGI(TAG, "ES7210 I2S1 通道创建成功");
}

int DualI2sAudioCodec::Read(int16_t* dest, int samples) {
    if (!input_dev_) {
        return 0;
    }
    
    std::lock_guard<std::mutex> lock(data_if_mutex_);
    int bytes_read = esp_codec_dev_read(input_dev_, dest, samples * sizeof(int16_t));
    if (bytes_read < 0) {
        ESP_LOGE(TAG, "读取音频数据失败: %d", bytes_read);
        return 0;
    }
    return bytes_read / sizeof(int16_t);
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
    input_enabled_ = enable;
    if (input_dev_) {
        if (enable) {
            esp_codec_dev_open(input_dev_, nullptr);
            ESP_LOGI(TAG, "启用输入");
        } else {
            esp_codec_dev_close(input_dev_);
            ESP_LOGI(TAG, "禁用输入");
        }
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

