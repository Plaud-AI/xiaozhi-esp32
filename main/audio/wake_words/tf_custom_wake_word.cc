#include "tf_custom_wake_word.h"
#include "audio_service.h"
#include "system_info.h"
#include "wake_word_model_data.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <opus_encoder.h>

#define TAG "TFCustomWakeWord"

TFCustomWakeWord::TFCustomWakeWord()
    : sr_engine_(),
      wake_word_pcm_(),
      wake_word_opus_() {
}

TFCustomWakeWord::~TFCustomWakeWord() {
    Stop();

    if (wake_word_encode_task_stack_ != nullptr) {
        heap_caps_free(wake_word_encode_task_stack_);
    }

    if (wake_word_encode_task_buffer_ != nullptr) {
        heap_caps_free(wake_word_encode_task_buffer_);
    }
}

bool TFCustomWakeWord::Initialize(AudioCodec* codec, srmodel_list_t* models_list) {
    codec_ = codec;
    
    ESP_LOGI(TAG, "Initializing TFCustomWakeWord (TFLite + PlaudSRCommand)");
    
    // 1. 配置 PlaudSRCommand
    xiaozhi::PlaudSRCommand::Config config;
    config.num_bins = 40;           // 40-dim fbank features
    config.sample_rate = 16000;     // 16kHz sample rate
    config.frame_length = 400;      // 25ms frame length (400 samples @ 16kHz)
    config.frame_shift = 160;       // 10ms frame shift (160 samples @ 16kHz)
    config.batch_size = 80;         // Process 80 frames (800ms) per inference
    config.default_threshold = 0.7f; // 70% confidence threshold
    config.model_data = g_wake_word_model_data;
    config.model_size = g_wake_word_model_data_len;
    config.tensor_arena_size = 100 * 1024;  // 100KB tensor arena
    
    // 2. 初始化推理引擎
    if (!sr_engine_.Initialize(config)) {
        ESP_LOGE(TAG, "Failed to initialize PlaudSRCommand");
        return false;
    }
    
    // 3. 添加唤醒词指令
    // 注意：这里的 command.id 应该与模型输出的类别索引对应
    // 假设模型输出：[silence, unknown, wake_word_1, wake_word_2, ...]
    sr_engine_.AddCommand(xiaozhi::PlaudSRCommand::Command(0, "silence", 0.0f));  // 忽略 silence
    sr_engine_.AddCommand(xiaozhi::PlaudSRCommand::Command(1, "unknown", 0.0f));  // 忽略 unknown
    sr_engine_.AddCommand(xiaozhi::PlaudSRCommand::Command(2, "hi plaud", 0.65f)); // 唤醒词 1
    sr_engine_.AddCommand(xiaozhi::PlaudSRCommand::Command(3, "hi nicebuild", 0.70f)); // 唤醒词 2
    
    ESP_LOGI(TAG, "Registered %d commands", sr_engine_.GetCommandCount());
    
    // 4. 创建 Opus 编码任务（ESP32 平台相关）
    wake_word_encode_task_stack_ = (StackType_t*)heap_caps_malloc(
        8192 * sizeof(StackType_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    wake_word_encode_task_buffer_ = (StaticTask_t*)heap_caps_malloc(
        sizeof(StaticTask_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (wake_word_encode_task_stack_ == nullptr || wake_word_encode_task_buffer_ == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate memory for wake word encode task");
        return false;
    }

    wake_word_encode_task_ = xTaskCreateStatic(
        [](void* arg) {
            static_cast<TFCustomWakeWord*>(arg)->EncodeWakeWordData();
        },
        "wake_word_encode", 8192, this, 5, wake_word_encode_task_stack_,
        wake_word_encode_task_buffer_);

    if (wake_word_encode_task_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create wake word encode task");
        return false;
    }

    ESP_LOGI(TAG, "TFCustomWakeWord initialized successfully");
    
    return true;
}

void TFCustomWakeWord::Feed(const std::vector<int16_t>& data) {
    if (!running_) {
        return;
    }
    
    // 将音频输入到推理引擎
    xiaozhi::PlaudSRCommand::Result result;
    
    if (sr_engine_.Process(data, result)) {
        // 检测到指令！
        OnCommandDetected(result);
    }
    
    // 存储原始音频用于编码（如果需要上传唤醒词音频）
    StoreWakeWordData(data);
}

void TFCustomWakeWord::OnCommandDetected(const xiaozhi::PlaudSRCommand::Result& result) {
    // 忽略 silence 和 unknown
    if (result.command_id == 0 || result.command_id == 1) {
        ESP_LOGD(TAG, "Ignoring command ID=%d ('%s')", result.command_id, result.text.c_str());
        return;
    }
    
    ESP_LOGI(TAG, "✓ Command detected: '%s' (ID=%d, confidence=%.2f)",
             result.text.c_str(), result.command_id, result.confidence);
    
    // 设置最后检测到的唤醒词
    last_detected_wake_word_ = result.text;
    
    // 触发上层回调
    if (wake_word_detected_callback_) {
        wake_word_detected_callback_(result.text);
    }
    
    // 停止当前检测（等待上层重新启动）
    running_ = false;
}

void TFCustomWakeWord::OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback) {
    wake_word_detected_callback_ = callback;
}

void TFCustomWakeWord::Start() {
    running_ = true;
    sr_engine_.Reset();  // 重置引擎状态
    ESP_LOGI(TAG, "TFCustomWakeWord started");
}

void TFCustomWakeWord::Stop() {
    running_ = false;
    ESP_LOGI(TAG, "TFCustomWakeWord stopped");
}

size_t TFCustomWakeWord::GetFeedSize() {
    return sr_engine_.GetFeedSize();
}

void TFCustomWakeWord::StoreWakeWordData(const std::vector<int16_t>& data) {
    std::lock_guard<std::mutex> lock(wake_word_mutex_);
    wake_word_pcm_.push_back(data);
    
    // 限制缓冲区大小（保留最近 3 秒的数据）
    // 假设每帧 10ms，300 帧 = 3 秒
    while (wake_word_pcm_.size() > 300) {
        wake_word_pcm_.pop_front();
    }
    
    wake_word_cv_.notify_one();
}

void TFCustomWakeWord::EncodeWakeWordData() {
    ESP_LOGI(TAG, "Wake word encode task started");
    
    // 创建 Opus 编码器（16kHz, 单声道, 60ms 帧）
    OpusEncoderWrapper encoder(16000, 1, 60);
    encoder.SetComplexity(0);  // 最快速度
    
    // Opus 编码器期望 960 samples (60ms @ 16kHz)
    // 我们的音频块是 160 samples (10ms @ 16kHz)
    // 需要合并 6 个块来满足 Opus 的要求
    const size_t opus_frame_size = 960;  // 60ms @ 16kHz
    std::vector<int16_t> buffer;
    buffer.reserve(opus_frame_size);
    
    while (true) {
        std::unique_lock<std::mutex> lock(wake_word_mutex_);
        wake_word_cv_.wait(lock, [this] { 
            return !wake_word_pcm_.empty() || wake_word_encode_task_ == nullptr; 
        });
        
        if (wake_word_encode_task_ == nullptr) {
            break;
        }
        
        if (wake_word_pcm_.empty()) {
            continue;
        }
        
        auto pcm = std::move(wake_word_pcm_.front());
        wake_word_pcm_.pop_front();
        lock.unlock();
        
        // 累积音频数据直到达到 Opus 帧大小
        buffer.insert(buffer.end(), pcm.begin(), pcm.end());
        
        // 当缓冲区足够大时，编码为 Opus
        while (buffer.size() >= opus_frame_size) {
            std::vector<int16_t> frame(buffer.begin(), buffer.begin() + opus_frame_size);
            buffer.erase(buffer.begin(), buffer.begin() + opus_frame_size);
            
            std::vector<uint8_t> opus;
            if (encoder.Encode(std::move(frame), opus)) {
                lock.lock();
                wake_word_opus_.push_back(std::move(opus));
                
                // 限制 Opus 缓冲区大小（保留最近 3 秒的数据）
                while (wake_word_opus_.size() > 50) {  // 50 * 60ms = 3s
                    wake_word_opus_.pop_front();
                }
                lock.unlock();
            }
        }
    }
    
    ESP_LOGI(TAG, "Wake word encode task stopped");
}

bool TFCustomWakeWord::GetWakeWordOpus(std::vector<uint8_t>& opus) {
    std::lock_guard<std::mutex> lock(wake_word_mutex_);
    if (wake_word_opus_.empty()) {
        return false;
    }
    opus = std::move(wake_word_opus_.front());
    wake_word_opus_.pop_front();
    return true;
}
