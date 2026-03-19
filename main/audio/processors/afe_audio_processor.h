#ifndef AFE_AUDIO_PROCESSOR_H
#define AFE_AUDIO_PROCESSOR_H

#include <esp_afe_sr_models.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <string>
#include <vector>
#include <functional>

#include "audio_processor.h"
#include "audio_codec.h"
#include "../wake_words/micro/helpers.h"  // For ExternalRAMAllocator

class AfeAudioProcessor : public AudioProcessor {
public:
    AfeAudioProcessor();
    ~AfeAudioProcessor();

    void Initialize(AudioCodec* codec, int frame_duration_ms, srmodel_list_t* models_list) override;
    void Feed(std::vector<int16_t>&& data) override;
    void Start() override;
    void Stop() override;
    bool IsRunning() override;
    void OnOutput(std::function<void(std::vector<int16_t>&& data)> callback) override;
    void OnVadStateChange(std::function<void(bool speaking)> callback) override;
    size_t GetFeedSize() override;
    void EnableDeviceAec(bool enable) override;

private:
    EventGroupHandle_t event_group_ = nullptr;
    esp_afe_sr_iface_t* afe_iface_ = nullptr;
    esp_afe_sr_data_t* afe_data_ = nullptr;
    std::function<void(std::vector<int16_t>&& data)> output_callback_;
    std::function<void(bool speaking)> vad_state_change_callback_;
    AudioCodec* codec_ = nullptr;
    int frame_samples_ = 0;
    bool is_speaking_ = false;
    bool is_running_ = false;
    bool passthrough_mode_ = false;
    bool afe_uplink_active_ = false;
    int afe_uplink_silence_frames_ = 0;
    int afe_uplink_attack_frames_ = 0;
    int passthrough_input_channels_ = 1;
    int passthrough_selected_channel_ = 0;
    uint32_t passthrough_diag_count_ = 0;
    bool passthrough_voice_active_ = false;
    int passthrough_voice_attack_frames_ = 0;
    int passthrough_silence_frames_ = 0;
    // Use PSRAM allocator for output buffer to save SRAM
    std::vector<int16_t, micro_wake_word::ExternalRAMAllocator<int16_t>> output_buffer_;
    std::vector<int16_t, micro_wake_word::ExternalRAMAllocator<int16_t>> passthrough_buffer_;
    // Prefix ring buffer: stores last N AFE frames before gate opens
    // to recover speech onset that triggers VAD (128-200ms detection latency)
    int prefix_frame_size_ = 0;
    int prefix_write_pos_ = 0;
    int prefix_count_ = 0;
    std::vector<int16_t, micro_wake_word::ExternalRAMAllocator<int16_t>> prefix_ring_;
    TaskHandle_t task_handle_ = nullptr;
    StackType_t* task_stack_ = nullptr;
    StaticTask_t* task_buffer_ = nullptr;

    void AudioProcessorTask();
};

#endif 