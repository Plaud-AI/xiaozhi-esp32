#include "custom_wake_word.h"
#include "audio_service.h"

#include <esp_log.h>
#include <esp_mn_speech_commands.h>

#define DETECTION_RUNNING_EVENT 1

#define TAG "CustomWakeWord"

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// CustomWakeWord: 基于 MultiNet 的唤醒词检测
// 数据流：音频 -> AFE 预处理（降噪/AEC） -> MultiNet 检测
// 参考：AfeWakeWord（使用 WakeNet）
// 差异：检测模型从 WakeNet 改为 MultiNet
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

CustomWakeWord::CustomWakeWord()
    : afe_data_(nullptr),
      wake_word_pcm_(),
      wake_word_opus_() {
    ESP_LOGI(TAG, "CustomWakeWord constructor called");
    event_group_ = xEventGroupCreate();
}

CustomWakeWord::~CustomWakeWord() {
    ESP_LOGI(TAG, "CustomWakeWord destructor called");
    
    // 清理 AFE（与 AfeWakeWord 相同）
    if (afe_data_ != nullptr) {
        afe_iface_->destroy(afe_data_);
    }
    
    // 清理 MultiNet（CustomWakeWord 特有）
    if (multinet_model_data_ != nullptr && multinet_ != nullptr) {
        multinet_->destroy(multinet_model_data_);
        multinet_model_data_ = nullptr;
    }

    // 清理编码任务资源（与 AfeWakeWord 相同）
    if (wake_word_encode_task_stack_ != nullptr) {
        heap_caps_free(wake_word_encode_task_stack_);
    }

    if (wake_word_encode_task_buffer_ != nullptr) {
        heap_caps_free(wake_word_encode_task_buffer_);
    }

    // 清理模型列表（与 AfeWakeWord 相同）
    if (models_ != nullptr) {
        esp_srmodel_deinit(models_);
    }

    vEventGroupDelete(event_group_);
}

bool CustomWakeWord::Initialize(AudioCodec* codec, srmodel_list_t* models_list) {
    ESP_LOGI(TAG, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    ESP_LOGI(TAG, "CustomWakeWord::Initialize START");
    ESP_LOGI(TAG, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    
    codec_ = codec;
    int ref_num = codec_->input_reference() ? 1 : 0;

    ESP_LOGI(TAG, "Parameters: codec=%p, models_list=%p, ref_num=%d", 
             codec, models_list, ref_num);

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // 初始化模型列表（与 AfeWakeWord 相同）
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    if (models_list == nullptr) {
        ESP_LOGI(TAG, "models_list is NULL, calling esp_srmodel_init(\"model\")");
        models_ = esp_srmodel_init("model");
    } else {
        ESP_LOGI(TAG, "Using provided models_list");
        models_ = models_list;
    }

    if (models_ == nullptr || models_->num == -1) {
        ESP_LOGE(TAG, "Failed to initialize model, models_=%p, num=%d", 
                 models_, models_ ? models_->num : -999);
        return false;
    }
    
    ESP_LOGI(TAG, "Found %d models in list", models_->num);
    for (int i = 0; i < models_->num; i++) {
        ESP_LOGI(TAG, "  Model %d: %s", i, models_->model_name[i]);
    }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // 查找并初始化 MultiNet 模型
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    multinet_model_name_ = esp_srmodel_filter(models_, ESP_MN_PREFIX, language_.c_str());
    if (multinet_model_name_ == nullptr) {
        ESP_LOGE(TAG, "Failed to find MultiNet model for language: %s", language_.c_str());
        ESP_LOGE(TAG, "Please ensure MultiNet model is selected in sdkconfig");
        return false;
    }

    ESP_LOGI(TAG, "Found MultiNet model: %s (language: %s)", multinet_model_name_, language_.c_str());
    
    // 确认模型版本
    if (strstr(multinet_model_name_, "mn6") != nullptr) {
        ESP_LOGI(TAG, "  ✓ Using MultiNet6 - Grapheme format required");
    } else if (strstr(multinet_model_name_, "mn5") != nullptr) {
        ESP_LOGW(TAG, "  ⚠️ Using MultiNet5 - Phoneme format required");
    } else if (strstr(multinet_model_name_, "mn7") != nullptr) {
        ESP_LOGI(TAG, "  ✓ Using MultiNet7 - Grapheme format");
    }

    multinet_ = esp_mn_handle_from_name(multinet_model_name_);
    if (multinet_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create MultiNet handle");
        return false;
    }

    multinet_model_data_ = multinet_->create(multinet_model_name_, duration_);
    if (multinet_model_data_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create MultiNet model data");
        return false;
    }
    
    int chunk_size = multinet_->get_samp_chunksize(multinet_model_data_);
    int sample_rate = multinet_->get_samp_rate(multinet_model_data_);
    ESP_LOGI(TAG, "MultiNet parameters: chunk_size=%d, sample_rate=%d", chunk_size, sample_rate);
    
    // 设置 MultiNet 内部阈值（低阈值，让它返回所有结果）
    multinet_->set_det_threshold(multinet_model_data_, multinet_threshold_);
    ESP_LOGI(TAG, "MultiNet internal threshold: %.3f (low to get all results)", multinet_threshold_);
    ESP_LOGI(TAG, "Application layer threshold: %.3f (for filtering)", app_threshold_);

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // 添加默认唤醒命令
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    commands_.clear();
    commands_.push_back({"COMPUTER", "computer", "wake"});
    commands_.push_back({"ASSISTANT", "assistant", "wake"});
    commands_.push_back({"HI DEVICE", "hi device", "wake"});
    
    ESP_LOGI(TAG, "Adding %d wake word commands:", commands_.size());
    esp_mn_commands_clear();
    for (int i = 0; i < commands_.size(); i++) {
        ESP_LOGI(TAG, "  [%d] command=\"%s\", text=\"%s\", action=\"%s\"", 
                 i + 1, commands_[i].command.c_str(), commands_[i].text.c_str(), commands_[i].action.c_str());
        esp_mn_commands_add(i + 1, commands_[i].command.c_str());
    }
    
    esp_mn_error_t* err = esp_mn_commands_update();
    if (err) {
        ESP_LOGE(TAG, "Failed to update commands, %d errors:", err->num);
        for (int i = 0; i < err->num; i++) {
            ESP_LOGE(TAG, "  Error command ID: %d, content: '%s'", 
                     err->phrases[i]->command_id, err->phrases[i]->string);
        }
        return false;
    }
    
    ESP_LOGI(TAG, "✓ Successfully added %d commands to MultiNet", commands_.size());

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // 初始化 AFE（与 AfeWakeWord 完全相同）
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    std::string input_format;
    for (int i = 0; i < codec_->input_channels() - ref_num; i++) {
        input_format.push_back('M');
    }
    for (int i = 0; i < ref_num; i++) {
        input_format.push_back('R');
    }
    ESP_LOGI(TAG, "Input format: %s, channels=%d, ref_num=%d", 
             input_format.c_str(), codec_->input_channels(), ref_num);
    
    afe_config_t* afe_config = afe_config_init(input_format.c_str(), models_, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    if (afe_config == nullptr) {
        ESP_LOGE(TAG, "Failed to init AFE config!");
        return false;
    }
    
    afe_config->aec_init = codec_->input_reference();
    afe_config->aec_mode = AEC_MODE_SR_HIGH_PERF;
    afe_config->afe_perferred_core = 1;
    afe_config->afe_perferred_priority = 1;
    afe_config->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    
    // 禁用 WakeNet（因为我们使用 MultiNet）
    afe_config->wakenet_init = false;
    
    ESP_LOGI(TAG, "AFE config: aec_init=%d, aec_mode=%d, wakenet_init=%d", 
             afe_config->aec_init, afe_config->aec_mode, afe_config->wakenet_init);
    
    afe_iface_ = esp_afe_handle_from_config(afe_config);
    if (afe_iface_ == nullptr) {
        ESP_LOGE(TAG, "Failed to get AFE interface handle!");
        return false;
    }
    
    afe_data_ = afe_iface_->create_from_config(afe_config);
    if (afe_data_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create AFE data from config!");
        return false;
    }
    ESP_LOGI(TAG, "AFE interface created successfully, feed_size=%d", 
             afe_iface_->get_feed_chunksize(afe_data_));

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // 创建音频检测任务（与 AfeWakeWord 相同）
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    xTaskCreate([](void* arg) {
        auto this_ = (CustomWakeWord*)arg;
        this_->AudioDetectionTask();
        vTaskDelete(NULL);
    }, "audio_detection", 4096, this, 3, nullptr);

    ESP_LOGI(TAG, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    ESP_LOGI(TAG, "CustomWakeWord initialization completed!");
    ESP_LOGI(TAG, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    return true;
}

void CustomWakeWord::OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback) {
    wake_word_detected_callback_ = callback;
    ESP_LOGI(TAG, "Wake word callback registered");
}

void CustomWakeWord::Start() {
    ESP_LOGI(TAG, "Starting wake word detection...");
    xEventGroupSetBits(event_group_, DETECTION_RUNNING_EVENT);
}

void CustomWakeWord::Stop() {
    ESP_LOGI(TAG, "Stopping wake word detection...");
    
    // 清除检测运行标志位（与 AfeWakeWord 相同）
    xEventGroupClearBits(event_group_, DETECTION_RUNNING_EVENT);
    
    // 重置 AFE 缓冲区（幂等操作，可以重复调用）
    if (afe_data_ != nullptr && afe_iface_ != nullptr) {
        afe_iface_->reset_buffer(afe_data_);
        ESP_LOGI(TAG, "AFE buffer reset");
    }
}

void CustomWakeWord::Feed(const std::vector<int16_t>& data) {
    if (afe_data_ == nullptr) {
        return;
    }
    
    // 计算音频能量，用于检测麦克风是否工作（与 AfeWakeWord 相同）
    static int feed_count = 0;
    if (++feed_count % 100 == 0) {
        int64_t sum = 0;
        int max_val = 0;
        for (const auto& sample : data) {
            sum += abs(sample);
            if (abs(sample) > max_val) {
                max_val = abs(sample);
            }
        }
        int avg = data.empty() ? 0 : sum / data.size();
        ESP_LOGI(TAG, "Audio level check (feed %d): avg=%d, max=%d, samples=%d", 
                 feed_count, avg, max_val, data.size());
        if (max_val < 100) {
            ESP_LOGW(TAG, "  ⚠️ Audio level is very low! Microphone may not be working!");
        }
    }
    
    // 将音频数据送入 AFE 处理（与 AfeWakeWord 完全相同）
    afe_iface_->feed(afe_data_, data.data());
}

size_t CustomWakeWord::GetFeedSize() {
    if (afe_data_ == nullptr) {
        return 0;
    }
    return afe_iface_->get_feed_chunksize(afe_data_);
}

void CustomWakeWord::AudioDetectionTask() {
    ESP_LOGI(TAG, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    ESP_LOGI(TAG, "Audio detection task started (AFE + MultiNet)");
    ESP_LOGI(TAG, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    
    auto fetch_size = afe_iface_->get_fetch_chunksize(afe_data_);
    auto feed_size = afe_iface_->get_feed_chunksize(afe_data_);
    ESP_LOGI(TAG, "AFE parameters: feed_size=%d, fetch_size=%d", feed_size, fetch_size);

    int loop_count = 0;
    while (true) {
        // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        // 等待检测启用事件（与 AfeWakeWord 相同）
        // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        xEventGroupWaitBits(event_group_, DETECTION_RUNNING_EVENT, pdFALSE, pdTRUE, portMAX_DELAY);

        // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        // 从 AFE 获取处理后的音频（与 AfeWakeWord 相同）
        // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        auto res = afe_iface_->fetch_with_delay(afe_data_, pdMS_TO_TICKS(100));
        if (res == nullptr || res->ret_value == ESP_FAIL) {
            continue;
        }

        // 每处理 100 次打印一次日志（与 AfeWakeWord 相同）
        loop_count++;
        if (loop_count % 100 == 0) {
            // 计算 AFE 处理后的音频能量
            int64_t sum = 0;
            int max_val = 0;
            int num_samples = res->data_size / sizeof(int16_t);
            for (int i = 0; i < num_samples; i++) {
                int val = abs(res->data[i]);
                sum += val;
                if (val > max_val) max_val = val;
            }
            int avg = num_samples > 0 ? sum / num_samples : 0;
            
            ESP_LOGI(TAG, "Detection running (loop %d): data_size=%d, avg=%d, max=%d", 
                     loop_count, res->data_size, avg, max_val);
        }

        // 存储唤醒词数据（与 AfeWakeWord 相同）
        StoreWakeWordData(res->data, res->data_size / sizeof(int16_t));

        // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        // 使用 MultiNet 检测（替代 WakeNet）
        // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        if (multinet_ == nullptr || multinet_model_data_ == nullptr) {
            continue;
        }
        
        esp_mn_state_t mn_state = multinet_->detect(multinet_model_data_, res->data);
        
        // 调试：每 100 次显示一次状态
        static int state_count = 0;
        if (++state_count % 100 == 0) {
            ESP_LOGD(TAG, "MultiNet state: %d (0=DETECTING, 1=DETECTED, 2=TIMEOUT)", mn_state);
        }
        
        if (mn_state == ESP_MN_STATE_DETECTING) {
            // 正在检测中，继续下一次循环
            continue;
        }
        else if (mn_state == ESP_MN_STATE_DETECTED) {
            // ✓ MultiNet 检测到命令
            ESP_LOGI(TAG, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
            ESP_LOGI(TAG, "MultiNet DETECTED");
            
            esp_mn_results_t* mn_result = multinet_->get_results(multinet_model_data_);
            
            if (mn_result != NULL && mn_result->num > 0) {
                int command_id = mn_result->phrase_id[0];
                float best_prob = mn_result->prob[0];
                
                // 显示所有检测结果
                ESP_LOGI(TAG, "Detection results (num=%d):", mn_result->num);
                for (int i = 0; i < mn_result->num && i < 5; i++) {
                    int result_cmd_id = mn_result->phrase_id[i];
                    float result_prob = mn_result->prob[i];
                    int result_array_index = result_cmd_id - 1;
                    
                    if (result_array_index >= 0 && result_array_index < commands_.size()) {
                        ESP_LOGI(TAG, "  [%d] ID=%d \"%s\": prob=%.3f %s",
                                 i, result_cmd_id,
                                 commands_[result_array_index].text.c_str(),
                                 result_prob,
                                 (i == 0) ? "← BEST" : "");
                    }
                }
                
                // 应用层阈值过滤
                ESP_LOGI(TAG, "Threshold check: best_prob=%.3f, app_threshold=%.3f", 
                         best_prob, app_threshold_);
                
                if (best_prob < app_threshold_) {
                    ESP_LOGW(TAG, "⚠️ Probability %.3f < threshold %.3f, ignoring", 
                             best_prob, app_threshold_);
                    ESP_LOGI(TAG, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
                    multinet_->clean(multinet_model_data_);
                    continue;
                }
                
                ESP_LOGI(TAG, "✓ Passed threshold check");
                
                // 检查命令 ID 是否有效
                int array_index = command_id - 1;
                if (array_index >= 0 && array_index < commands_.size()) {
                    auto& command = commands_[array_index];
                    
                    if (command.action == "wake") {
                        last_detected_wake_word_ = command.text;
                        
                        ESP_LOGI(TAG, "*** WAKE WORD DETECTED! ***");
                        ESP_LOGI(TAG, "Wake word: \"%s\" (prob: %.3f)", 
                                 last_detected_wake_word_.c_str(), best_prob);
                        ESP_LOGI(TAG, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
                        
                        // 停止检测（与 AfeWakeWord 相同）
                        Stop();
                        
                        // 触发回调（与 AfeWakeWord 相同）
                        if (wake_word_detected_callback_) {
                            wake_word_detected_callback_(last_detected_wake_word_);
                        } else {
                            ESP_LOGW(TAG, "Wake word detected but callback is NULL!");
                        }
                    }
                } else {
                    ESP_LOGW(TAG, "Invalid command ID: %d (array index: %d, total: %d)", 
                             command_id, array_index, commands_.size());
                }
            } else {
                ESP_LOGW(TAG, "MultiNet detected but no results");
            }
            
            // 清理 MultiNet 状态
            multinet_->clean(multinet_model_data_);
        }
        else if (mn_state == ESP_MN_STATE_TIMEOUT) {
            // 超时，清理状态
            ESP_LOGD(TAG, "MultiNet timeout");
            multinet_->clean(multinet_model_data_);
        }
    }
}

void CustomWakeWord::StoreWakeWordData(const int16_t* data, size_t samples) {
    // 存储 PCM 数据用于编码（与 AfeWakeWord 完全相同）
    wake_word_pcm_.emplace_back(std::vector<int16_t>(data, data + samples));
    
    // 保留约 2 秒数据（检测周期 30ms，采样率 16000Hz，chunk 512）
    while (wake_word_pcm_.size() > 2000 / 30) {
        wake_word_pcm_.pop_front();
    }
}

void CustomWakeWord::EncodeWakeWordData() {
    // 与 AfeWakeWord 完全相同的实现
    const size_t stack_size = 4096 * 7;
    wake_word_opus_.clear();
    if (wake_word_encode_task_stack_ == nullptr) {
        wake_word_encode_task_stack_ = (StackType_t*)heap_caps_malloc(stack_size, MALLOC_CAP_SPIRAM);
        assert(wake_word_encode_task_stack_ != nullptr);
    }
    if (wake_word_encode_task_buffer_ == nullptr) {
        wake_word_encode_task_buffer_ = (StaticTask_t*)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL);
        assert(wake_word_encode_task_buffer_ != nullptr);
    }

    wake_word_encode_task_ = xTaskCreateStatic([](void* arg) {
        auto this_ = (CustomWakeWord*)arg;
        {
            auto start_time = esp_timer_get_time();
            auto encoder = std::make_unique<OpusEncoderWrapper>(16000, 1, OPUS_FRAME_DURATION_MS);
            encoder->SetComplexity(0); // 0 is the fastest

            int packets = 0;
            for (auto& pcm: this_->wake_word_pcm_) {
                encoder->Encode(std::move(pcm), [this_](std::vector<uint8_t>&& opus) {
                    std::lock_guard<std::mutex> lock(this_->wake_word_mutex_);
                    this_->wake_word_opus_.emplace_back(std::move(opus));
                    this_->wake_word_cv_.notify_all();
                });
                packets++;
            }
            this_->wake_word_pcm_.clear();

            auto end_time = esp_timer_get_time();
            ESP_LOGI(TAG, "Encode wake word opus %d packets in %ld ms", 
                     packets, (long)((end_time - start_time) / 1000));

            std::lock_guard<std::mutex> lock(this_->wake_word_mutex_);
            this_->wake_word_opus_.push_back(std::vector<uint8_t>());
            this_->wake_word_cv_.notify_all();
        }
        vTaskDelete(NULL);
    }, "encode_wake_word", stack_size, this, 2, wake_word_encode_task_stack_, wake_word_encode_task_buffer_);
}

bool CustomWakeWord::GetWakeWordOpus(std::vector<uint8_t>& opus) {
    // 与 AfeWakeWord 完全相同的实现
    std::unique_lock<std::mutex> lock(wake_word_mutex_);
    wake_word_cv_.wait(lock, [this]() {
        return !wake_word_opus_.empty();
    });
    opus.swap(wake_word_opus_.front());
    wake_word_opus_.pop_front();
    return !opus.empty();
}

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// 动态命令管理接口（MultiNet 特有功能）
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

void CustomWakeWord::ClearCommands() {
    commands_.clear();
    ESP_LOGI(TAG, "Cleared all wake word commands");
}

void CustomWakeWord::AddCommand(const std::string& command, 
                                const std::string& text, 
                                const std::string& action) {
    commands_.push_back({command, text, action});
    ESP_LOGI(TAG, "Added command: command='%s', text='%s', action='%s'", 
             command.c_str(), text.c_str(), action.c_str());
}

void CustomWakeWord::SetThreshold(float threshold) {
    app_threshold_ = threshold;
    ESP_LOGI(TAG, "Set application layer threshold to %.3f", app_threshold_);
}

bool CustomWakeWord::UpdateCommands() {
    if (multinet_model_data_ == nullptr) {
        ESP_LOGW(TAG, "MultiNet not initialized, cannot update commands");
        return false;
    }
    
    ESP_LOGI(TAG, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    ESP_LOGI(TAG, "Updating commands to MultiNet...");
    ESP_LOGI(TAG, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    
    // 清空并重新添加命令
    esp_mn_commands_clear();
    
    ESP_LOGI(TAG, "Adding %d wake word commands:", commands_.size());
    for (int i = 0; i < commands_.size(); i++) {
        ESP_LOGI(TAG, "  [%d] command=\"%s\", text=\"%s\", action=\"%s\"", 
                 i + 1, commands_[i].command.c_str(), 
                 commands_[i].text.c_str(), commands_[i].action.c_str());
        esp_mn_commands_add(i + 1, commands_[i].command.c_str());
    }
    
    // 更新命令到模型
    esp_mn_error_t* err = esp_mn_commands_update();
    if (err) {
        ESP_LOGE(TAG, "Failed to update commands, %d errors:", err->num);
        for (int i = 0; i < err->num; i++) {
            ESP_LOGE(TAG, "  Error command ID: %d, content: %s", 
                     err->phrases[i]->command_id, err->phrases[i]->string);
        }
        return false;
    }
    
    ESP_LOGI(TAG, "✓ Successfully updated %d commands to MultiNet!", commands_.size());
    ESP_LOGI(TAG, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    
    return true;
}
