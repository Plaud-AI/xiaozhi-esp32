#ifndef _DUAL_I2S_AUDIO_CODEC_H
#define _DUAL_I2S_AUDIO_CODEC_H

#include "audio/audio_codec.h"

#include <esp_codec_dev.h>
#include <esp_codec_dev_defaults.h>
#include <mutex>

/**
 * DualI2sAudioCodec - 双独立 I2S 总线音频编解码器
 * 
 * 硬件架构：
 * - ES8311 (DAC): 使用 I2S0，独立的 MCLK/BCLK/WS/DOUT/DIN 引脚
 * - ES7210 (ADC): 使用 I2S1，独立的 MCLK/BCLK/WS/SDOUT 引脚
 * - I2C 总线：两个芯片共用同一个 I2C 总线（不同地址）
 */
class DualI2sAudioCodec : public AudioCodec {
private:
    // ES8311 (DAC) 数据接口 - 使用 I2S0
    const audio_codec_data_if_t* out_data_if_ = nullptr;
    const audio_codec_ctrl_if_t* out_ctrl_if_ = nullptr;
    const audio_codec_if_t* out_codec_if_ = nullptr;
    const audio_codec_gpio_if_t* out_gpio_if_ = nullptr;
    esp_codec_dev_handle_t output_dev_ = nullptr;
    
    // ES7210 (ADC) 数据接口 - 使用 I2S1
    const audio_codec_data_if_t* in_data_if_ = nullptr;
    const audio_codec_ctrl_if_t* in_ctrl_if_ = nullptr;
    const audio_codec_if_t* in_codec_if_ = nullptr;
    esp_codec_dev_handle_t input_dev_ = nullptr;
    
    std::mutex data_if_mutex_;
    
    // I2S 通道句柄
    i2s_chan_handle_t tx_handle_i2s0_ = nullptr;  // ES8311 输出
    i2s_chan_handle_t rx_handle_i2s1_ = nullptr;  // ES7210 输入

    // 创建 ES8311 (DAC) 的 I2S0 通道
    void CreateEs8311Channel(gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, 
                             gpio_num_t dout, gpio_num_t din);
    
    // 创建 ES7210 (ADC) 的 I2S1 通道
    void CreateEs7210Channel(gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, 
                             gpio_num_t din);

    virtual int Read(int16_t* dest, int samples) override;
    virtual int Write(const int16_t* data, int samples) override;

public:
    /**
     * 构造函数
     * @param i2c_master_handle I2C 总线句柄
     * @param input_sample_rate 输入采样率 (ES7210)
     * @param output_sample_rate 输出采样率 (ES8311)
     * @param es8311_mclk ES8311 MCLK 引脚
     * @param es8311_bclk ES8311 BCLK 引脚
     * @param es8311_ws ES8311 WS 引脚
     * @param es8311_dout ES8311 DOUT 引脚
     * @param es8311_din ES8311 DIN 引脚
     * @param es7210_mclk ES7210 MCLK 引脚
     * @param es7210_bclk ES7210 BCLK 引脚
     * @param es7210_ws ES7210 WS 引脚
     * @param es7210_din ES7210 SDOUT 引脚
     * @param pa_pin 功放使能引脚（如果没有则为 GPIO_NUM_NC）
     * @param es8311_addr ES8311 I2C 地址
     * @param es7210_addr ES7210 I2C 地址
     */
    DualI2sAudioCodec(
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
    );
    
    virtual ~DualI2sAudioCodec();

    // 覆盖基类的 Start() 方法，避免重复启用 I2S 通道
    // 因为我们已经在构造函数中启用了 I2S（为了提供 MCLK）
    virtual void Start() override;

    virtual void SetOutputVolume(int volume) override;
    virtual void EnableInput(bool enable) override;
    virtual void EnableOutput(bool enable) override;
};

#endif // _DUAL_I2S_AUDIO_CODEC_H

