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
#include "plaud_sr_command.h"

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
    // PlaudSRCommand 推理引擎（平台无关）
    xiaozhi::PlaudSRCommand sr_engine_;
    
    // ESP32 平台相关
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

    void StoreWakeWordData(const std::vector<int16_t>& data);
    void OnCommandDetected(const xiaozhi::PlaudSRCommand::Result& result);
};

#endif  // TF_CUSTOM_WAKE_WORD_H

