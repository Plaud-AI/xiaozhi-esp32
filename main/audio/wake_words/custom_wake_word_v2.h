#ifndef CUSTOM_WAKE_WORD_V2_H
#define CUSTOM_WAKE_WORD_V2_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <esp_afe_sr_models.h>
#include <esp_mn_iface.h>
#include <esp_mn_models.h>
#include <model_path.h>

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "audio_codec.h"
#include "wake_word.h"

// CustomWakeWordV2: 基于 MultiNet 模型的唤醒词检测
// 数据流：音频 -> AFE 预处理 -> MultiNet 检测
// 与 AfeWakeWord 的区别：使用 MultiNet 而非 WakeNet
class CustomWakeWordV2 : public WakeWord {
public:
    CustomWakeWordV2();
    ~CustomWakeWordV2();

    bool Initialize(AudioCodec* codec, srmodel_list_t* models_list);
    void Feed(const std::vector<int16_t>& data);
    void OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback);
    void Start();
    void Stop();
    size_t GetFeedSize();
    void EncodeWakeWordData();
    bool GetWakeWordOpus(std::vector<uint8_t>& opus);
    const std::string& GetLastDetectedWakeWord() const { return last_detected_wake_word_; }

    // 动态命令管理接口（MultiNet 特有）
    void ClearCommands();
    void AddCommand(const std::string& command, const std::string& text, const std::string& action);
    void SetThreshold(float threshold);
    bool UpdateCommands();
    int GetCommandCount() const { return commands_.size(); }

private:
    // 命令结构体（MultiNet 特有）
    struct Command {
        std::string command;  // MultiNet 命令格式（如 "COMPUTER"）
        std::string text;     // 显示文本（如 "computer"）
        std::string action;   // 动作类型（如 "wake"）
    };

    // AFE 相关（与 AfeWakeWord 相同）
    srmodel_list_t* models_ = nullptr;
    esp_afe_sr_iface_t* afe_iface_ = nullptr;
    esp_afe_sr_data_t* afe_data_ = nullptr;
    EventGroupHandle_t event_group_;
    
    // MultiNet 相关（替代 WakeNet）
    esp_mn_iface_t* multinet_ = nullptr;
    model_iface_data_t* multinet_model_data_ = nullptr;
    char* multinet_model_name_ = nullptr;
    std::string language_ = "en";
    int duration_ = 5000;
    float multinet_threshold_ = 0.40;  // MultiNet 内部阈值（保守配置，优先准确率）
    float app_threshold_ = 0.50;       // 应用层阈值（保守配置，减少误触发）
    std::deque<Command> commands_;
    
    // 回调和状态（与 AfeWakeWord 相同）
    std::function<void(const std::string& wake_word)> wake_word_detected_callback_;
    AudioCodec* codec_ = nullptr;
    std::string last_detected_wake_word_;

    // 编码相关（与 AfeWakeWord 相同）
    TaskHandle_t wake_word_encode_task_ = nullptr;
    StaticTask_t* wake_word_encode_task_buffer_ = nullptr;
    StackType_t* wake_word_encode_task_stack_ = nullptr;
    std::deque<std::vector<int16_t>> wake_word_pcm_;
    std::deque<std::vector<uint8_t>> wake_word_opus_;
    std::mutex wake_word_mutex_;
    std::condition_variable wake_word_cv_;

    // 内部方法
    void StoreWakeWordData(const int16_t* data, size_t samples);
    void AudioDetectionTask();
};

#endif

