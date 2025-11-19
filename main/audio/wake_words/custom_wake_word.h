#ifndef CUSTOM_WAKE_WORD_H
#define CUSTOM_WAKE_WORD_H

#include <esp_attr.h>
#include <esp_mn_iface.h>
#include <esp_mn_models.h>
#include <esp_afe_sr_models.h>
#include <model_path.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#include <deque>
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>

#include "audio_codec.h"
#include "wake_word.h"

class CustomWakeWord : public WakeWord {
public:
    CustomWakeWord();
    ~CustomWakeWord();

    bool Initialize(AudioCodec* codec, srmodel_list_t* models_list);
    void Feed(const std::vector<int16_t>& data);
    void OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback);
    void Start();
    void Stop();
    size_t GetFeedSize();
    void EncodeWakeWordData();
    bool GetWakeWordOpus(std::vector<uint8_t>& opus);
    const std::string& GetLastDetectedWakeWord() const { return last_detected_wake_word_; }

    // 动态命令管理接口（用于蓝牙配置）
    void ClearCommands();
    void AddCommand(const std::string& phoneme, const std::string& text, const std::string& action);
    void SetThreshold(float threshold);
    bool UpdateCommands();  // 批量更新命令到 MultiNet（运行时生效）
    int GetCommandCount() const { return commands_.size(); }

private:
    struct Command {
        std::string command;
        std::string text;
        std::string action;
    };

    // AFE (Audio Front-End) 相关成员 - 用于降噪、波束成形、AEC
    esp_afe_sr_iface_t* afe_iface_ = nullptr;
    esp_afe_sr_data_t* afe_data_ = nullptr;
    EventGroupHandle_t event_group_ = nullptr;
    bool use_afe_ = true;  // 是否使用 AFE 预处理（默认启用）

    // multinet 相关成员变量
    esp_mn_iface_t* multinet_ = nullptr;
    model_iface_data_t* multinet_model_data_ = nullptr;
    srmodel_list_t *models_ = nullptr;
    char* mn_name_ = nullptr;
    std::string language_ = "en";
    int duration_ = 3000;
    float multinet_threshold_ = 0.05;  // MultiNet 内部阈值（低阈值获取所有结果）
    float app_threshold_ = 0.20;       // 应用层阈值（3 个唤醒词时推荐 0.20-0.25）
    std::deque<Command> commands_;
 
    std::function<void(const std::string& wake_word)> wake_word_detected_callback_;
    AudioCodec* codec_ = nullptr;
    std::string last_detected_wake_word_;
    std::atomic<bool> running_ = false;

    TaskHandle_t wake_word_encode_task_ = nullptr;
    StaticTask_t* wake_word_encode_task_buffer_ = nullptr;
    StackType_t* wake_word_encode_task_stack_ = nullptr;
    std::deque<std::vector<int16_t>> wake_word_pcm_;
    std::deque<std::vector<uint8_t>> wake_word_opus_;
    std::mutex wake_word_mutex_;
    std::condition_variable wake_word_cv_;

    void StoreWakeWordData(const std::vector<int16_t>& data);
    void ParseWakenetModelConfig();
    void AudioDetectionTask();  // AFE + MultiNet 检测任务
};

#endif
