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

    /**
     * 热加载自定义 .tflite 模型（运行时替换，无需重启）
     *
     * @param model_data  指向 .tflite 文件内容的指针（调用方持有内存，生命周期必须长于推理引擎）
     * @param model_size  模型字节数
     * @param wake_word_text 要注册的唤醒词文字（对应模型输出 class 2）
     * @return true 成功，false 失败
     */
    bool ReinitWithCustomModel(const uint8_t* model_data, size_t model_size,
                               const std::string& wake_word_text);
    size_t GetFeedSize();
    void EncodeWakeWordData();
    bool GetWakeWordOpus(std::vector<uint8_t>& opus);
    const std::string& GetLastDetectedWakeWord() const { return last_detected_wake_word_; }

private:
    // PlaudSRCommand 推理引擎（平台无关）
    plaud::PlaudSRCommand sr_engine_;
    // 保护 sr_engine_ 在 Feed/ReinitWithCustomModel 之间的并发访问，避免
    // 热加载期间 Feed 线程解引用被释放的 tensor_arena。
    std::mutex sr_engine_mutex_;

    // ESP32 平台相关
    std::function<void(const std::string& wake_word)> wake_word_detected_callback_;
    AudioCodec* codec_ = nullptr;
    std::string last_detected_wake_word_;
    std::atomic<bool> running_ = false;

    // 唤醒词编码相关（与 CustomWakeWord 相同）
    StaticTask_t* wake_word_encode_task_buffer_ = nullptr;
    StackType_t* wake_word_encode_task_stack_ = nullptr;
    std::deque<std::vector<int16_t>> wake_word_pcm_;
    std::deque<std::vector<uint8_t>> wake_word_opus_;

    void StoreWakeWordData(const std::vector<int16_t>& data);
    void OnCommandDetected(const plaud::PlaudSRCommand::Result& result);
};

#endif  // TF_CUSTOM_WAKE_WORD_H

