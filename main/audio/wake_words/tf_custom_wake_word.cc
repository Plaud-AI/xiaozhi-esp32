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
    
    // 1. 配置 PlaudSRCommand（生产阶段参数）
    plaud::PlaudSRCommand::Config config;
    config.num_bins = 40;           // 40-dim fbank features
    config.sample_rate = 16000;     // 16kHz sample rate
    config.frame_length = 400;      // 25ms frame length (400 samples @ 16kHz)
    config.frame_shift = 160;       // 10ms frame shift (160 samples @ 16kHz)
    config.batch_size = 40;         // Process 40 frames (400ms) per inference [生产配置]
    config.default_threshold = 0.7f; // 70% confidence threshold
    config.detection_frames = 2;    // 连续 2 帧确认 (总延迟 800ms) [生产配置]
    config.timeout_ms = 5000;       // 5 秒检测超时
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
    // 模型输出：[silence, unknown, xiao_ai_tong_xue, ni_hao_dong_dong, ni_hao_pu_luo_de]
    sr_engine_.AddCommand(plaud::PlaudSRCommand::Command(0, "silence", 0.0f));  // 忽略 silence
    sr_engine_.AddCommand(plaud::PlaudSRCommand::Command(1, "unknown", 0.0f));  // 忽略 unknown
    sr_engine_.AddCommand(plaud::PlaudSRCommand::Command(2, "xiao ai tong xue", 0.65f)); // 小爱同学
    sr_engine_.AddCommand(plaud::PlaudSRCommand::Command(3, "ni hao dong dong", 0.65f)); // 你好东东
    sr_engine_.AddCommand(plaud::PlaudSRCommand::Command(4, "ni hao pu luo de", 0.65f)); // 你好普罗德
    
    ESP_LOGI(TAG, "Registered %d commands", sr_engine_.GetCommandCount());
    
    ESP_LOGI(TAG, "TFCustomWakeWord initialized successfully");
    
    return true;
}

void TFCustomWakeWord::Feed(const std::vector<int16_t>& data) {
    if (!running_) {
        return;
    }
    
    // 调试：每 100 次 Feed 显示一次音频输入信息
    static int feed_count = 0;
    feed_count++;
    if (feed_count % 100 == 0) {
        ESP_LOGI(TAG, "📥 [Feed #%d] samples=%zu, running=%d", 
                 feed_count, data.size(), running_.load());
    }
    
    // 存储原始音频用于编码（如果需要上传唤醒词音频）
    StoreWakeWordData(data);
    
    // 将音频输入到推理引擎（使用状态机 API）
    plaud::PlaudSRCommand::Result result;
    plaud::SRState state = sr_engine_.Process(data, result);
    
    // 调试：显示状态变化
    static plaud::SRState last_state = plaud::SRState::DETECTING;
    if (state != last_state || state == plaud::SRState::DETECTED) {
        const char* state_names[] = {"DETECTING", "DETECTED", "TIMEOUT"};
        ESP_LOGI(TAG, "🔄 [Feed #%d] State: %s", feed_count, state_names[static_cast<int>(state)]);
        last_state = state;
    }
 
    if (state == plaud::SRState::DETECTING) {
        // 正在检测中，无需处理
        return;
    } 
    else if (state == plaud::SRState::DETECTED) {
        // ✓ 检测到命令！
        OnCommandDetected(result);
        
        // 重置引擎状态以准备下一次检测
        sr_engine_.Reset();
    } 
    else if (state == plaud::SRState::TIMEOUT) {
        // 超时，重置引擎状态
        ESP_LOGD(TAG, "Detection timeout, resetting engine");
        sr_engine_.Reset();
    }
}

void TFCustomWakeWord::OnCommandDetected(const plaud::PlaudSRCommand::Result& result) {
    // 忽略 silence 和 unknown
    if (result.command_id == 0 || result.command_id == 1) {
        ESP_LOGD(TAG, "🔇 Ignoring command ID=%d ('%s')", result.command_id, result.text.c_str());
        return;
    }
    
    ESP_LOGI(TAG, "✅ ═══════════════════════════════════════");
    ESP_LOGI(TAG, "✅ WAKE WORD DETECTED!");
    ESP_LOGI(TAG, "✅   Text: '%s'", result.text.c_str());
    ESP_LOGI(TAG, "✅   Command ID: %d", result.command_id);
    ESP_LOGI(TAG, "✅   Confidence: %.2f%%", result.confidence * 100.0f);
    ESP_LOGI(TAG, "✅   Timestamp: %u ms", result.timestamp_ms);
    ESP_LOGI(TAG, "✅ ═══════════════════════════════════════");
    
    // 设置最后检测到的唤醒词
    last_detected_wake_word_ = result.text;
    
    // 触发上层回调
    if (wake_word_detected_callback_) {
        ESP_LOGI(TAG, "🔔 Triggering wake word callback...");
        wake_word_detected_callback_(result.text);
    } else {
        ESP_LOGW(TAG, "⚠️  No wake word callback registered!");
    }
    
    // 停止当前检测（等待上层重新启动）
    running_ = false;
    ESP_LOGI(TAG, "⏸️  Detection stopped, waiting for restart");
}

void TFCustomWakeWord::OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback) {
    wake_word_detected_callback_ = callback;
}

void TFCustomWakeWord::Start() {
    ESP_LOGI(TAG, "▶️  ═══════════════════════════════════════");
    ESP_LOGI(TAG, "▶️  STARTING TFCustomWakeWord");
    running_ = true;
    sr_engine_.Reset();  // 重置引擎状态
    ESP_LOGI(TAG, "▶️  Detection engine ready, listening for wake words...");
    ESP_LOGI(TAG, "▶️  ═══════════════════════════════════════");
}

void TFCustomWakeWord::Stop() {
    ESP_LOGI(TAG, "⏸️  ═══════════════════════════════════════");
    ESP_LOGI(TAG, "⏸️  STOPPING TFCustomWakeWord");
    running_ = false;
    ESP_LOGI(TAG, "⏸️  Detection stopped");
    ESP_LOGI(TAG, "⏸️  ═══════════════════════════════════════");
}

size_t TFCustomWakeWord::GetFeedSize() {
    return sr_engine_.GetFeedSize();
}

void TFCustomWakeWord::StoreWakeWordData(const std::vector<int16_t>& data) {
    // Store PCM data for wake word encoding
    wake_word_pcm_.push_back(data);
    
    // Keep only the last 2 seconds (16000 Hz * 2 / 160 ≈ 200 frames)
    while (wake_word_pcm_.size() > 200) {
        wake_word_pcm_.pop_front();
    }
}

void TFCustomWakeWord::EncodeWakeWordData() {
    // 参考 custom_wake_word.cc 的实现：一次性编码任务
    const size_t stack_size = 4096 * 7;
    wake_word_opus_.clear();
    
    if (wake_word_encode_task_stack_ == nullptr) {
        wake_word_encode_task_stack_ = (StackType_t*)heap_caps_malloc(stack_size, MALLOC_CAP_SPIRAM);
        if (wake_word_encode_task_stack_ == nullptr) {
            ESP_LOGE(TAG, "Failed to allocate encode task stack");
            return;
        }
    }
    if (wake_word_encode_task_buffer_ == nullptr) {
        wake_word_encode_task_buffer_ = (StaticTask_t*)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL);
        if (wake_word_encode_task_buffer_ == nullptr) {
            ESP_LOGE(TAG, "Failed to allocate encode task buffer");
            return;
        }
    }
    
    xTaskCreateStatic([](void* arg) {
        auto this_ = (TFCustomWakeWord*)arg;
        OpusEncoderWrapper encoder(16000, 1, 60);
        
        // Opus 编码器期望 960 samples (60ms @ 16kHz)
        // TFCustomWakeWord 每个块是 160 samples (10ms @ 16kHz)
        // 需要合并多个块来满足 Opus 的要求
        const size_t opus_frame_size = 960; // 60ms @ 16kHz
        std::vector<int16_t> buffer;
        
        for (auto& pcm : this_->wake_word_pcm_) {
            // 将数据添加到缓冲区
            buffer.insert(buffer.end(), pcm.begin(), pcm.end());
            
            // 当缓冲区有足够数据时，编码一帧
            while (buffer.size() >= opus_frame_size) {
                std::vector<int16_t> frame(buffer.begin(), buffer.begin() + opus_frame_size);
                buffer.erase(buffer.begin(), buffer.begin() + opus_frame_size);
                
                std::vector<uint8_t> opus;
                if (encoder.Encode(std::move(frame), opus)) {
                    this_->wake_word_opus_.push_back(std::move(opus));
                }
            }
        }
        
        // 如果还有剩余数据（不足一帧），补零后编码
        if (!buffer.empty()) {
            buffer.resize(opus_frame_size, 0); // 补零到正确大小
            std::vector<uint8_t> opus;
            if (encoder.Encode(std::move(buffer), opus)) {
                this_->wake_word_opus_.push_back(std::move(opus));
            }
        }
        
        ESP_LOGI(TAG, "Wake word encode task completed, encoded %d frames", 
                 this_->wake_word_opus_.size());
        
        vTaskDelete(NULL);
    }, "encode_wake_word", stack_size, this, 3, wake_word_encode_task_stack_, wake_word_encode_task_buffer_);
}

bool TFCustomWakeWord::GetWakeWordOpus(std::vector<uint8_t>& opus) {
    if (wake_word_opus_.empty()) {
        return false;
    }
    opus = std::move(wake_word_opus_.front());
    wake_word_opus_.pop_front();
    return true;
}
