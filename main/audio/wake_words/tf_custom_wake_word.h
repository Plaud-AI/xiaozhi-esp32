#ifndef TF_CUSTOM_WAKE_WORD_H
#define TF_CUSTOM_WAKE_WORD_H

#include <esp_attr.h>

#include <deque>
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>

#include "audio_codec.h"
#include "wake_word.h"
#include "feature_provider.h"
#include "micro_model_settings.h"

// Forward declarations for TFLite
namespace tflite {
class MicroInterpreter;
class Model;
}  // namespace tflite

class TFCustomWakeWord : public WakeWord {
public:
    TFCustomWakeWord();
    ~TFCustomWakeWord();

    bool Initialize(AudioCodec* codec, srmodel_list_t* models_list);
    void Feed(const std::vector<int16_t>& data);
    void OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback);
    void Start();
    void Stop();
    size_t GetFeedSize();
    void EncodeWakeWordData();
    bool GetWakeWordOpus(std::vector<uint8_t>& opus);
    const std::string& GetLastDetectedWakeWord() const { return last_detected_wake_word_; }

private:
    struct WakeWordConfig {
        std::string label;      // 唤醒词标签（如 "yes", "no"）
        std::string text;       // 显示文本（如 "hi plaud"）
        std::string action;     // 动作类型（如 "wake"）
        int output_index;       // 模型输出索引
    };

    // TFLite 相关成员变量
    const tflite::Model* model_ = nullptr;
    tflite::MicroInterpreter* interpreter_ = nullptr;
    uint8_t* tensor_arena_ = nullptr;
    static constexpr size_t kTensorArenaSize = 10 * 1024;  // 10KB for wake word model
    
    // 特征提取相关
    FeatureProvider* feature_provider_ = nullptr;
    int8_t* feature_buffer_ = nullptr;
    int32_t last_time_ms_ = 0;
    
    // 唤醒词配置
    std::vector<WakeWordConfig> wake_word_configs_;
    float detection_threshold_ = 0.7f;  // 检测阈值
    int cooldown_ms_ = 2000;            // 冷却时间（避免重复检测）
    int64_t last_detection_time_us_ = 0;
    
    std::function<void(const std::string& wake_word)> wake_word_detected_callback_;
    AudioCodec* codec_ = nullptr;
    std::string last_detected_wake_word_;
    std::atomic<bool> running_ = false;

    // 唤醒词编码相关（与 CustomWakeWord 相同）
    TaskHandle_t wake_word_encode_task_ = nullptr;
    StaticTask_t* wake_word_encode_task_buffer_ = nullptr;
    StackType_t* wake_word_encode_task_stack_ = nullptr;
    std::deque<std::vector<int16_t>> wake_word_pcm_;
    std::deque<std::vector<uint8_t>> wake_word_opus_;
    std::mutex wake_word_mutex_;
    std::condition_variable wake_word_cv_;

    // 音频缓冲区（用于累积音频数据）
    std::vector<int16_t> audio_buffer_;
    size_t samples_per_inference_ = 16000;  // 1 秒的音频用于一次推理

    void StoreWakeWordData(const std::vector<int16_t>& data);
    bool InitializeTFLiteModel();
    bool RunInference(int8_t* features);
    void ProcessDetectionResult(const std::string& detected_label, float score);
};

#endif  // TF_CUSTOM_WAKE_WORD_H

