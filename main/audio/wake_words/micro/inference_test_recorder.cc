#include "inference_test_recorder.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <algorithm>

static const char* TAG = "InferenceTestRecorder";

namespace micro_wake_word {

InferenceTestRecorder::InferenceTestRecorder() {
    // 预分配内存，避免运行时频繁分配
    pcm_data_.reserve(kMaxPCMSamples);
    probabilities_.reserve(kMaxProbabilities);
    ESP_LOGI(TAG, "InferenceTestRecorder created (ring buffer: %u samples = %u ms, %u KB)", 
             (unsigned)kMaxPCMSamples, 
             (unsigned)kMaxDurationMs,
             (unsigned)(kMaxPCMSamples * sizeof(int16_t) / 1024));
}

InferenceTestRecorder::~InferenceTestRecorder() {
    ESP_LOGI(TAG, "InferenceTestRecorder destroyed");
}

void InferenceTestRecorder::OnDetectionStart() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    Reset();
    recording_ = true;
    start_time_ms_ = esp_timer_get_time() / 1000;
    
    ESP_LOGI(TAG, "📹 Recording started at %lu ms", (unsigned long)start_time_ms_);
}

InferenceTestRecorder::UploadPacket InferenceTestRecorder::OnDetectionEnd(
    const std::string& wake_word, float probability) {
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    recording_ = false;
    uint32_t end_time_ms = esp_timer_get_time() / 1000;
    uint32_t duration_ms = end_time_ms - start_time_ms_;
    
    uint32_t pcm_count = (uint32_t)pcm_data_.size();
    uint32_t prob_count = (uint32_t)probabilities_.size();
    uint32_t audio_ms = pcm_count * 1000 / kSampleRate;
    
    ESP_LOGI(TAG, "📹 Recording ended:");
    ESP_LOGI(TAG, "   - Duration: %lu ms", (unsigned long)duration_ms);
    ESP_LOGI(TAG, "   - PCM samples: %lu (%lu ms audio)", 
             (unsigned long)pcm_count, (unsigned long)audio_ms);
    ESP_LOGI(TAG, "   - Probabilities: %lu inference results", (unsigned long)prob_count);
    ESP_LOGI(TAG, "   - Wake word: %s", wake_word.c_str());
    ESP_LOGI(TAG, "   - Final probability: %.3f", probability);
    
    // 打包数据
    UploadPacket packet;
    packet.pcm_data = std::move(pcm_data_);
    packet.probabilities = std::move(probabilities_);
    packet.wake_word = wake_word;
    packet.final_probability = probability;
    packet.duration_ms = duration_ms;
    
    // 重置内部状态
    Reset();
    
    return packet;
}

void InferenceTestRecorder::OnDetectionCancelled() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (recording_) {
        ESP_LOGI(TAG, "📹 Recording cancelled (PCM: %lu samples, Prob: %lu values)",
                 (unsigned long)pcm_data_.size(), (unsigned long)probabilities_.size());
    }
    
    recording_ = false;
    Reset();
}

void InferenceTestRecorder::RecordPCM(const int16_t* data, size_t samples) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!recording_) {
        return;
    }
    
    // 使用环形缓冲策略：如果超出最大容量，删除最旧的数据
    // 这样始终保留最近 kMaxPCMSamples 个样本（即最近 N 秒的音频）
    pcm_data_.insert(pcm_data_.end(), data, data + samples);
    
    if (pcm_data_.size() > kMaxPCMSamples) {
        // 删除最旧的数据，只保留最近的
        size_t excess = pcm_data_.size() - kMaxPCMSamples;
        pcm_data_.erase(pcm_data_.begin(), pcm_data_.begin() + excess);
    }
}

void InferenceTestRecorder::RecordProbability(uint8_t raw_probability) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!recording_) {
        return;
    }
    
    // 使用环形缓冲策略：如果超出最大容量，删除最旧的数据
    probabilities_.push_back(raw_probability);
    
    if (probabilities_.size() > kMaxProbabilities) {
        probabilities_.erase(probabilities_.begin());
    }
}

size_t InferenceTestRecorder::GetPCMSampleCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pcm_data_.size();
}

size_t InferenceTestRecorder::GetProbabilityCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return probabilities_.size();
}

void InferenceTestRecorder::Reset() {
    pcm_data_.clear();
    probabilities_.clear();
    start_time_ms_ = 0;
}

}  // namespace micro_wake_word

