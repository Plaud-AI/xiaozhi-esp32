#include "tf_custom_wake_word.h"
#include "audio_service.h"
#include "system_info.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>

// TFLite Micro includes
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/schema/schema_generated.h"

// Wake word model data (需要提供实际的模型数据)
// 这里使用一个占位符，实际使用时需要替换为真实的 TFLite 模型
extern const unsigned char g_wake_word_model_data[];
extern const unsigned int g_wake_word_model_data_len;

#define TAG "TFCustomWakeWord"

TFCustomWakeWord::TFCustomWakeWord()
    : wake_word_pcm_(), wake_word_opus_() {
}

TFCustomWakeWord::~TFCustomWakeWord() {
    Stop();
    
    if (feature_provider_ != nullptr) {
        delete feature_provider_;
        feature_provider_ = nullptr;
    }
    
    if (feature_buffer_ != nullptr) {
        delete[] feature_buffer_;
        feature_buffer_ = nullptr;
    }
    
    if (interpreter_ != nullptr) {
        delete interpreter_;
        interpreter_ = nullptr;
    }
    
    if (tensor_arena_ != nullptr) {
        heap_caps_free(tensor_arena_);
        tensor_arena_ = nullptr;
    }

    if (wake_word_encode_task_stack_ != nullptr) {
        heap_caps_free(wake_word_encode_task_stack_);
    }

    if (wake_word_encode_task_buffer_ != nullptr) {
        heap_caps_free(wake_word_encode_task_buffer_);
    }
}

bool TFCustomWakeWord::Initialize(AudioCodec* codec, srmodel_list_t* models_list) {
    codec_ = codec;
    
    ESP_LOGI(TAG, "Initializing TFLite-based wake word detection");
    
    // 配置唤醒词（与 micro_speech 示例相同的类别）
    wake_word_configs_ = {
        {"silence", "silence", "none", 0},
        {"unknown", "unknown", "none", 1},
        {"yes", "hi plaud", "wake", 2},      // "yes" 映射到 "hi plaud"
        {"no", "goodbye", "none", 3}
    };
    
    detection_threshold_ = 0.7f;  // 70% 置信度阈值
    cooldown_ms_ = 2000;          // 2 秒冷却时间
    
    // 分配特征缓冲区
    feature_buffer_ = new int8_t[kFeatureElementCount];
    if (feature_buffer_ == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate feature buffer");
        return false;
    }
    
    // 创建 FeatureProvider
    feature_provider_ = new FeatureProvider(kFeatureElementCount, feature_buffer_);
    if (feature_provider_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create FeatureProvider");
        return false;
    }
    
    // 初始化 TFLite 模型
    if (!InitializeTFLiteModel()) {
        ESP_LOGE(TAG, "Failed to initialize TFLite model");
        return false;
    }
    
    // 创建唤醒词编码任务
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
            auto* self = static_cast<TFCustomWakeWord*>(arg);
            self->EncodeWakeWordData();
        },
        "ww_encode_tf", 8192, this, 2,
        wake_word_encode_task_stack_,
        wake_word_encode_task_buffer_);

    if (wake_word_encode_task_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create wake word encode task");
        return false;
    }
    
    ESP_LOGI(TAG, "TFLite wake word detection initialized successfully");
    ESP_LOGI(TAG, "Detection threshold: %.2f, Cooldown: %d ms", detection_threshold_, cooldown_ms_);
    ESP_LOGI(TAG, "Configured %d wake words:", wake_word_configs_.size());
    for (const auto& config : wake_word_configs_) {
        ESP_LOGI(TAG, "  [%d] %s -> \"%s\" (action: %s)", 
                 config.output_index, config.label.c_str(), 
                 config.text.c_str(), config.action.c_str());
    }
    
    return true;
}

bool TFCustomWakeWord::InitializeTFLiteModel() {
    // 加载模型
    model_ = tflite::GetModel(g_wake_word_model_data);
    if (model_->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "Model schema version %d doesn't match supported version %d",
                 model_->version(), TFLITE_SCHEMA_VERSION);
        return false;
    }
    
    // 分配 Tensor Arena
    tensor_arena_ = (uint8_t*)heap_caps_malloc(kTensorArenaSize, 
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (tensor_arena_ == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate tensor arena");
        return false;
    }
    
    // 创建操作解析器（根据模型需要添加操作）
    static tflite::MicroMutableOpResolver<10> op_resolver;
    op_resolver.AddFullyConnected();
    op_resolver.AddSoftmax();
    op_resolver.AddReshape();
    op_resolver.AddQuantize();
    op_resolver.AddDequantize();
    
    // 创建解释器
    static tflite::MicroInterpreter static_interpreter(
        model_, op_resolver, tensor_arena_, kTensorArenaSize);
    interpreter_ = &static_interpreter;
    
    // 分配张量
    TfLiteStatus allocate_status = interpreter_->AllocateTensors();
    if (allocate_status != kTfLiteOk) {
        ESP_LOGE(TAG, "AllocateTensors() failed");
        return false;
    }
    
    // 验证输入输出张量
    TfLiteTensor* input = interpreter_->input(0);
    TfLiteTensor* output = interpreter_->output(0);
    
    ESP_LOGI(TAG, "Model loaded successfully");
    ESP_LOGI(TAG, "  Input: dims=%d, type=%d, bytes=%d", 
             input->dims->size, input->type, input->bytes);
    ESP_LOGI(TAG, "  Output: dims=%d, type=%d, bytes=%d", 
             output->dims->size, output->type, output->bytes);
    ESP_LOGI(TAG, "  Arena used: %d / %d bytes", 
             interpreter_->arena_used_bytes(), kTensorArenaSize);
    
    return true;
}

void TFCustomWakeWord::Feed(const std::vector<int16_t>& data) {
    if (!running_) {
        return;
    }
    
    // 存储唤醒词数据（用于后续编码）
    StoreWakeWordData(data);
    
    // 累积音频数据
    audio_buffer_.insert(audio_buffer_.end(), data.begin(), data.end());
    
    // 当累积足够的音频数据时进行推理
    if (audio_buffer_.size() >= samples_per_inference_) {
        int32_t current_time_ms = esp_timer_get_time() / 1000;
        int how_many_new_slices = 0;
        
        // 使用 FeatureProvider 提取特征
        TfLiteStatus feature_status = feature_provider_->PopulateFeatureData(
            last_time_ms_, current_time_ms, &how_many_new_slices);
        
        if (feature_status != kTfLiteOk) {
            ESP_LOGW(TAG, "Feature extraction failed");
            // 清空部分缓冲区，保留最后 500ms 的数据
            if (audio_buffer_.size() > 8000) {
                audio_buffer_.erase(audio_buffer_.begin(), 
                                   audio_buffer_.begin() + (audio_buffer_.size() - 8000));
            }
            return;
        }
        
        last_time_ms_ = current_time_ms;
        
        // 运行推理
        if (how_many_new_slices > 0) {
            RunInference(feature_buffer_);
        }
        
        // 清空缓冲区，保留最后 500ms 的数据（用于滑动窗口）
        if (audio_buffer_.size() > 8000) {
            audio_buffer_.erase(audio_buffer_.begin(), 
                               audio_buffer_.begin() + (audio_buffer_.size() - 8000));
        }
    }
}

bool TFCustomWakeWord::RunInference(int8_t* features) {
    // 将特征数据复制到输入张量
    TfLiteTensor* input = interpreter_->input(0);
    memcpy(input->data.int8, features, kFeatureElementCount);
    
    // 运行推理
    TfLiteStatus invoke_status = interpreter_->Invoke();
    if (invoke_status != kTfLiteOk) {
        ESP_LOGE(TAG, "Invoke failed");
        return false;
    }
    
    // 获取输出
    TfLiteTensor* output = interpreter_->output(0);
    int8_t* output_data = output->data.int8;
    
    // 找到最高置信度的类别
    int max_index = 0;
    int8_t max_score = output_data[0];
    
    for (int i = 1; i < kCategoryCount; ++i) {
        if (output_data[i] > max_score) {
            max_score = output_data[i];
            max_index = i;
        }
    }
    
    // 转换量化输出到浮点数 (假设输出量化参数: scale=1/128, zero_point=0)
    float score = static_cast<float>(max_score) / 128.0f;
    
    // 检查是否超过阈值
    if (score >= detection_threshold_) {
        // 查找对应的唤醒词配置
        for (const auto& config : wake_word_configs_) {
            if (config.output_index == max_index && config.action == "wake") {
                ProcessDetectionResult(config.text, score);
                break;
            }
        }
    }
    
    return true;
}

void TFCustomWakeWord::ProcessDetectionResult(const std::string& detected_label, float score) {
    // 检查冷却时间
    int64_t current_time_us = esp_timer_get_time();
    if (current_time_us - last_detection_time_us_ < cooldown_ms_ * 1000) {
        ESP_LOGD(TAG, "Detection in cooldown period, ignoring");
        return;
    }
    
    last_detection_time_us_ = current_time_us;
    last_detected_wake_word_ = detected_label;
    running_ = false;  // 停止检测
    
    ESP_LOGI(TAG, "✓ Wake word detected: \"%s\" (score: %.2f)", 
             detected_label.c_str(), score);
    
    // 触发回调
    if (wake_word_detected_callback_) {
        wake_word_detected_callback_(detected_label);
    }
}

void TFCustomWakeWord::StoreWakeWordData(const std::vector<int16_t>& data) {
    std::unique_lock<std::mutex> lock(wake_word_mutex_);
    wake_word_pcm_.push_back(data);
    
    // 限制缓冲区大小（保留最近 3 秒的数据）
    while (wake_word_pcm_.size() > 300) {  // 假设 10ms per chunk, 300 chunks = 3s
        wake_word_pcm_.pop_front();
    }
}

void TFCustomWakeWord::EncodeWakeWordData() {
    ESP_LOGI(TAG, "Wake word encode task started");
    
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
        
        // 编码为 Opus
        if (codec_ != nullptr) {
            std::vector<uint8_t> opus;
            if (codec_->Encode(pcm, opus)) {
                lock.lock();
                wake_word_opus_.push_back(std::move(opus));
                
                // 限制 Opus 缓冲区大小
                while (wake_word_opus_.size() > 300) {
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

void TFCustomWakeWord::OnWakeWordDetected(
    std::function<void(const std::string& wake_word)> callback) {
    wake_word_detected_callback_ = callback;
}

void TFCustomWakeWord::Start() {
    ESP_LOGI(TAG, "Starting TFLite wake word detection");
    running_ = true;
    last_detection_time_us_ = 0;
    last_time_ms_ = 0;
    audio_buffer_.clear();
}

void TFCustomWakeWord::Stop() {
    ESP_LOGI(TAG, "Stopping TFLite wake word detection");
    running_ = false;
}

size_t TFCustomWakeWord::GetFeedSize() {
    // 返回 10ms 的音频数据大小（16kHz * 0.01s * 2 bytes）
    return 320;  // 160 samples * 2 bytes = 320 bytes
}

