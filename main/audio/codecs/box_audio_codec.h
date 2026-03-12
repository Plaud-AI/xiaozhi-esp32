#ifndef _BOX_AUDIO_CODEC_H
#define _BOX_AUDIO_CODEC_H

#include "audio_codec.h"

#include <esp_codec_dev.h>
#include <esp_codec_dev_defaults.h>
#include <mutex>
#include <atomic>


class BoxAudioCodec : public AudioCodec {
private:
    const audio_codec_data_if_t* data_if_ = nullptr;
    const audio_codec_ctrl_if_t* out_ctrl_if_ = nullptr;
    const audio_codec_if_t* out_codec_if_ = nullptr;
    const audio_codec_ctrl_if_t* in_ctrl_if_ = nullptr;
    const audio_codec_if_t* in_codec_if_ = nullptr;
    const audio_codec_gpio_if_t* gpio_if_ = nullptr;

    esp_codec_dev_handle_t output_dev_ = nullptr;
    esp_codec_dev_handle_t input_dev_ = nullptr;
    bool input_device_permanently_open_ = false;

    // Separate mutexes so Read() and Write() can run concurrently.
    // (They use different DMA channel handles — rx_handle_ vs tx_handle_ —
    //  which are independently thread-safe in ESP-IDF duplex mode.)
    std::mutex read_mutex_;
    std::mutex write_mutex_;

    // Software-loopback ring buffer used as AEC reference.
    // Write() pushes decoded TTS PCM into the buffer; Read() pops it and
    // injects it into the reference channel (slot 1) that is fed to the AFE,
    // replacing the useless MIC2 data that would otherwise be there.
    // Size: 4096 samples = 256 ms @ 16 kHz — enough to absorb the I2S DMA
    // pipeline latency (~120 ms) with comfortable headroom.
    static constexpr int kLoopbackBufSize = 4096;
    int16_t loopback_buf_[kLoopbackBufSize] = {};
    std::atomic<int> loopback_write_idx_{0};
    std::atomic<int> loopback_read_idx_{0};

    void CreateDuplexChannels(gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din);

    virtual int Read(int16_t* dest, int samples) override;
    virtual int Write(const int16_t* data, int samples) override;

public:
    BoxAudioCodec(void* i2c_master_handle, int input_sample_rate, int output_sample_rate,
        gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din,
        gpio_num_t pa_pin, uint8_t es8311_addr, uint8_t es7210_addr, bool input_reference);
    virtual ~BoxAudioCodec();

    virtual void SetOutputVolume(int volume) override;
    virtual void EnableInput(bool enable) override;
    virtual void EnableOutput(bool enable) override;
    virtual void Start() override;

    // Drain the loopback buffer (e.g. on tts:stop so stale TTS reference
    // does not linger into the next listen round).
    void ClearLoopbackBuffer();
};

#endif // _BOX_AUDIO_CODEC_H
