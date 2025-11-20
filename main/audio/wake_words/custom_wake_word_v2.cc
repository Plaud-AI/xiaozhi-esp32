#include "custom_wake_word_v2.h"
#include "audio_service.h"

#include <esp_log.h>
#include <esp_mn_speech_commands.h>
#include <freertos/task.h>

#define DETECTION_RUNNING_EVENT 1

#define TAG "CustomWakeWordV2"

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// CustomWakeWordV2: 基于 MultiNet 的唤醒词检测
// 数据流：音频 -> AFE 预处理（降噪/AEC） -> MultiNet 检测
// 参考：AfeWakeWord（使用 WakeNet）
// 差异：检测模型从 WakeNet 改为 MultiNet
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

CustomWakeWordV2::CustomWakeWordV2()
    : afe_data_(nullptr),
      wake_word_pcm_(),
      wake_word_opus_() {
    ESP_LOGI(TAG, "CustomWakeWordV2 constructor called");
    event_group_ = xEventGroupCreate();
}

CustomWakeWordV2::~CustomWakeWordV2() {
    ESP_LOGI(TAG, "CustomWakeWordV2 destructor called");
    
    // 清理 AFE（与 AfeWakeWord 相同）
    if (afe_data_ != nullptr) {
        afe_iface_->destroy(afe_data_);
    }
    
    // 清理 MultiNet（CustomWakeWordV2 特有）
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

bool CustomWakeWordV2::Initialize(AudioCodec* codec, srmodel_list_t* models_list) {
    ESP_LOGI(TAG, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    ESP_LOGI(TAG, "CustomWakeWordV2::Initialize START");
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
    
    // 设置 MultiNet 内部阈值（合理阈值，过滤低概率噪声）
    // 关键修复：从 0.05 提高到 0.30，避免暴露大量低概率"垃圾结果"
    // 这样可以防止低概率检测持续触发 DETECTED，导致超时计时器不断重置
    multinet_->set_det_threshold(multinet_model_data_, multinet_threshold_);
    ESP_LOGI(TAG, "MultiNet internal threshold: %.3f (filter low-prob noise)", multinet_threshold_);
    ESP_LOGI(TAG, "Application layer threshold: %.3f (final confirmation)", app_threshold_);

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // 添加唤醒命令（如果已有命令则使用，否则使用默认）
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    if (commands_.empty()) {
        ESP_LOGI(TAG, "No commands pre-configured, using default wake words (MultiNet6 Grapheme)");
        
        // 使用组合词（更适合 MultiNet6）
        // ✅ 测试验证：相同主体名称的变体效果最好
        commands_.push_back({"HI XIAOZHI", "hi xiaozhi", "wake"});
        commands_.push_back({"HEY XIAOZHI", "hey xiaozhi", "wake"});
        commands_.push_back({"HELLO XIAOZHI", "hello xiaozhi", "wake"});
        
        // 测试结果：
        // ❌ HELLO/OK/YES - 识别率极低（0.05-0.11），误识别严重
        // ⏳ HI XIAOZHI - 待测试（组合词，预期更好）
    } else {
        ESP_LOGI(TAG, "Using %d pre-configured wake word commands", commands_.size());
    }
    
    ESP_LOGI(TAG, "Adding %d wake word commands to MultiNet:", commands_.size());
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
        auto this_ = (CustomWakeWordV2*)arg;
        this_->AudioDetectionTask();
        vTaskDelete(NULL);
    }, "audio_detection", 4096, this, 3, nullptr);

    ESP_LOGI(TAG, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    ESP_LOGI(TAG, "CustomWakeWordV2 initialization completed!");
    ESP_LOGI(TAG, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    return true;
}

void CustomWakeWordV2::OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback) {
    wake_word_detected_callback_ = callback;
    ESP_LOGI(TAG, "Wake word callback registered");
}

void CustomWakeWordV2::Start() {
    ESP_LOGI(TAG, "Starting wake word detection...");
    
    // ⚠️ 重要：清理 MultiNet 和 AFE 状态，防止上次检测的残留
    if (multinet_model_data_ != nullptr && multinet_ != nullptr) {
        multinet_->clean(multinet_model_data_);
        ESP_LOGD(TAG, "MultiNet state cleaned on start");
    }
    
    if (afe_data_ != nullptr && afe_iface_ != nullptr) {
        afe_iface_->reset_buffer(afe_data_);
        ESP_LOGD(TAG, "AFE buffer reset on start");
    }
    
    // 🔥 冷却时间：延迟200ms后再启动检测，让扬声器余音和回声消散
    // 官方 demo 没有这个延迟，但我们用 MultiNet 做唤醒，需要防止回声误触发
    vTaskDelay(pdMS_TO_TICKS(200));
    ESP_LOGD(TAG, "Wake word detection cooldown completed (200ms)");
    
    xEventGroupSetBits(event_group_, DETECTION_RUNNING_EVENT);
}

void CustomWakeWordV2::Stop() {
    ESP_LOGI(TAG, "Stopping wake word detection...");
    
    // 清除检测运行标志位（与 AfeWakeWord 相同）
    xEventGroupClearBits(event_group_, DETECTION_RUNNING_EVENT);
    
    // 重置 AFE 缓冲区（幂等操作，可以重复调用）
    if (afe_data_ != nullptr && afe_iface_ != nullptr) {
        afe_iface_->reset_buffer(afe_data_);
        ESP_LOGI(TAG, "AFE buffer reset");
    }
}

void CustomWakeWordV2::Feed(const std::vector<int16_t>& data) {
    if (afe_data_ == nullptr) {
        return;
    }
    
    // ⚠️ 修复：如果检测已停止，不要继续 feed，避免 AFE ringbuffer 溢出
    EventBits_t bits = xEventGroupGetBits(event_group_);
    if ((bits & DETECTION_RUNNING_EVENT) == 0) {
        // 检测未运行，不 feed 数据
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
        int64_t now_ms = esp_timer_get_time() / 1000;
        ESP_LOGI(TAG, "📥 Feed (count %d, time %d ms): avg=%d, max=%d, samples=%d", 
                 feed_count, (int)now_ms, avg, max_val, (int)data.size());
        if (max_val < 100) {
            ESP_LOGW(TAG, "  ⚠️ Audio level is very low! Microphone may not be working!");
        }
    }
    
    // 将音频数据送入 AFE 处理（与 AfeWakeWord 完全相同）
    afe_iface_->feed(afe_data_, data.data());
}

size_t CustomWakeWordV2::GetFeedSize() {
    if (afe_data_ == nullptr) {
        return 0;
    }
    return afe_iface_->get_feed_chunksize(afe_data_);
}

void CustomWakeWordV2::AudioDetectionTask() {
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
        int64_t fetch_start = esp_timer_get_time();
        // ⚠️ 优化：减少超时时间从 100ms 到 50ms，降低延迟
        auto res = afe_iface_->fetch_with_delay(afe_data_, pdMS_TO_TICKS(50));
        int64_t fetch_duration = (esp_timer_get_time() - fetch_start) / 1000;  // ms
        
        if (res == nullptr || res->ret_value == ESP_FAIL) {
            continue;
        }
        
        // 每 100 次记录 fetch 延迟
        static int fetch_count = 0;
        if (++fetch_count % 100 == 0) {
            ESP_LOGI(TAG, "⏱️  AFE fetch delay: %d ms (count %d)", (int)fetch_duration, fetch_count);
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
            int64_t now_ms = esp_timer_get_time() / 1000;
            
            ESP_LOGI(TAG, "📤 Detection loop %d (time %d ms): data_size=%d, avg=%d, max=%d", 
                     loop_count, (int)now_ms, (int)res->data_size, avg, max_val);
        }

        // 存储唤醒词数据（与 AfeWakeWord 相同）
        StoreWakeWordData(res->data, res->data_size / sizeof(int16_t));

        // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        // 使用 MultiNet 检测（替代 WakeNet）
        // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        if (multinet_ == nullptr || multinet_model_data_ == nullptr) {
            continue;
        }
        
        // 🔥 移除定期清理：参考官方 demo，只在超时时清理
        // 官方 demo 依赖 MultiNet 的内置超时机制（5秒）
        // 定期清理可能在用户说话时打断，导致需要重新累积状态
        // 
        // MultiNet 自身超时机制：
        // - 5秒无有效语音 → ESP_MN_STATE_TIMEOUT
        // - 超时后我们会 clean，见下面的 TIMEOUT 处理
        // 
        // 因此不需要额外的定期清理
        
        // 测量 MultiNet detect() 调用时间
        int64_t detect_start = esp_timer_get_time();
        esp_mn_state_t mn_state = multinet_->detect(multinet_model_data_, res->data);
        int64_t detect_duration = (esp_timer_get_time() - detect_start) / 1000;  // ms
        
        // 每 100 次记录 detect 耗时
        static int detect_count = 0;
        if (++detect_count % 100 == 0) {
            ESP_LOGI(TAG, "⏱️  MultiNet detect() took: %d ms (count %d)", (int)detect_duration, detect_count);
        }
        
        // 调试：每 50 次显示一次状态（提高频率）
        static int state_count = 0;
        if (++state_count % 50 == 0) {
            ESP_LOGI(TAG, "🔄 MultiNet state=%d (0=DETECTING, 1=DETECTED, 2=TIMEOUT), loop=%d", 
                     mn_state, state_count);
        }
        
        if (mn_state == ESP_MN_STATE_DETECTING) {
            // 正在检测中，继续下一次循环
            continue;
        }
        else if (mn_state == ESP_MN_STATE_DETECTED) {
            // ✓ MultiNet 检测到命令
            int64_t now_ms = esp_timer_get_time() / 1000;
            ESP_LOGI(TAG, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
            ESP_LOGI(TAG, "🎯 MultiNet DETECTED at %d ms (loop %d)", (int)now_ms, loop_count);
            ESP_LOGI(TAG, "⏱️  Detection timing: fetch=%d ms, detect=%d ms", 
                     (int)fetch_duration, (int)detect_duration);
            
            // 计算当前音频块的能量（仅用于数据收集，不影响判断）
            int64_t sum = 0;
            int max_val = 0;
            int num_samples = res->data_size / sizeof(int16_t);
            for (int i = 0; i < num_samples; i++) {
                int val = abs(res->data[i]);
                sum += val;
                if (val > max_val) max_val = val;
            }
            int avg_energy = num_samples > 0 ? sum / num_samples : 0;
            
            ESP_LOGI(TAG, "📊 Audio energy: avg=%d, max=%d (for reference only)", avg_energy, max_val);
            
            esp_mn_results_t* mn_result = multinet_->get_results(multinet_model_data_);
            
            if (mn_result != NULL && mn_result->num > 0) {
                int command_id = mn_result->phrase_id[0];
                float best_prob = mn_result->prob[0];
                
                // 显示所有检测结果（方便分析模型表现）
                ESP_LOGI(TAG, "📊 Detection results (num=%d, registered_commands=%d):", 
                         mn_result->num, commands_.size());
                ESP_LOGI(TAG, "   ✅ All results passed MultiNet internal threshold (%.3f)", 
                         multinet_threshold_);
                
                if (mn_result->num < commands_.size()) {
                    ESP_LOGW(TAG, "  ⚠️ Only %d/%d commands returned (others below MultiNet threshold %.3f)", 
                             mn_result->num, commands_.size(), multinet_threshold_);
                }
                for (int i = 0; i < mn_result->num && i < 5; i++) {
                    int result_cmd_id = mn_result->phrase_id[i];
                    float result_prob = mn_result->prob[i];
                    int result_array_index = result_cmd_id - 1;
                    
                    if (result_array_index >= 0 && result_array_index < commands_.size()) {
                        ESP_LOGI(TAG, "  [%d] ID=%d \"%s\" (cmd=\"%s\"): prob=%.3f %s",
                                 i, result_cmd_id,
                                 commands_[result_array_index].text.c_str(),
                                 commands_[result_array_index].command.c_str(),
                                 result_prob,
                                 (i == 0) ? "← BEST" : "");
                    } else {
                        ESP_LOGW(TAG, "  [%d] ID=%d (INVALID INDEX %d): prob=%.3f",
                                 i, result_cmd_id, result_array_index, result_prob);
                    }
                }
                
                // 简单的固定阈值过滤（让模型自己说话）
                // 诊断：显示时间戳帮助分析触发间隔
                int64_t now_ms = esp_timer_get_time() / 1000;
                ESP_LOGI(TAG, "🔍 Threshold check: best_prob=%.3f vs app_threshold=%.3f (time: %d ms)", 
                         best_prob, app_threshold_, (int)now_ms);
                
                if (best_prob < app_threshold_) {
                    float gap = app_threshold_ - best_prob;
                    ESP_LOGW(TAG, "❌ Rejected by application layer: prob %.3f < threshold %.3f (gap: %.3f)", 
                             best_prob, app_threshold_, gap);
                    ESP_LOGI(TAG, "   💡 Note: This should be rare now (MultiNet internal threshold = %.3f)", 
                             multinet_threshold_);
                    
                    // 🔥 兜底机制：清理 MultiNet 状态
                    // 
                    // 说明：
                    // - MultiNet 内部阈值已从 0.05 提高到 0.30，大幅减少低概率结果暴露
                    // - 理论上，能到达这里的 prob 应该在 [0.30, 0.38) 之间（很窄的区间）
                    // - 如果频繁触发此分支，说明内部阈值可能需要进一步调整
                    // 
                    // 清理逻辑：
                    // - prob < app_threshold 说明这是边缘情况（模糊发音、弱信号等）
                    // - 清理状态，让 MultiNet 从干净状态重新开始
                    // - 避免 RNN 状态累积错误特征
                    multinet_->clean(multinet_model_data_);
                    ESP_LOGI(TAG, "🧹 Cleaned MultiNet state (fallback mechanism)");
                    ESP_LOGI(TAG, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
                    continue;
                }
                
                ESP_LOGI(TAG, "✅ Accepted: prob %.3f >= threshold %.3f", 
                         best_prob, app_threshold_);
                
                // 检查命令 ID 是否有效
                int array_index = command_id - 1;
                if (array_index >= 0 && array_index < commands_.size()) {
                    auto& command = commands_[array_index];
                    
                    if (command.action == "wake") {
                        last_detected_wake_word_ = command.text;
                        
                        // 诊断：记录检测间隔，帮助分析误触发
                        static int64_t last_wake_time_ms = 0;
                        int64_t current_wake_time_ms = esp_timer_get_time() / 1000;
                        int interval_sec = 0;
                        if (last_wake_time_ms > 0) {
                            interval_sec = (current_wake_time_ms - last_wake_time_ms) / 1000;
                        }
                        last_wake_time_ms = current_wake_time_ms;
                        
                        ESP_LOGI(TAG, "*** WAKE WORD DETECTED! ***");
                        ESP_LOGI(TAG, "Wake word: \"%s\" (prob: %.3f)", 
                                 last_detected_wake_word_.c_str(), best_prob);
                        if (interval_sec > 0) {
                            ESP_LOGI(TAG, "⏰ Time since last wake: %d seconds", interval_sec);
                        }
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
            
            // 成功检测后清理 MultiNet 状态
            // 注意：Stop() 已调用，任务将停止，这个 clean 是为了确保下次 Start() 时状态干净
            // 实际上 Start() 中也会调用 clean，所以这里是双保险
            multinet_->clean(multinet_model_data_);
        }
        else if (mn_state == ESP_MN_STATE_TIMEOUT) {
            // 超时，只清理 MultiNet 状态
            // 不清理 AFE buffer，让音频流继续，避免延迟
            // 
            // 说明：
            // - MultiNet 内部阈值提高到 0.30 后，超时机制应该能正常工作
            // - 只有 prob >= 0.30 的检测才会触发 DETECTED 并重置计时器
            // - 环境噪声（prob < 0.30）不会影响超时
            ESP_LOGI(TAG, "⏱️  MultiNet TIMEOUT (5s no valid detection), cleaning state");
            multinet_->clean(multinet_model_data_);
        }
    }
}

void CustomWakeWordV2::StoreWakeWordData(const int16_t* data, size_t samples) {
    // 存储 PCM 数据用于编码（与 AfeWakeWord 完全相同）
    wake_word_pcm_.emplace_back(std::vector<int16_t>(data, data + samples));
    
    // 保留约 2 秒数据（检测周期 30ms，采样率 16000Hz，chunk 512）
    while (wake_word_pcm_.size() > 2000 / 30) {
        wake_word_pcm_.pop_front();
    }
}

void CustomWakeWordV2::EncodeWakeWordData() {
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
        auto this_ = (CustomWakeWordV2*)arg;
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

bool CustomWakeWordV2::GetWakeWordOpus(std::vector<uint8_t>& opus) {
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

void CustomWakeWordV2::ClearCommands() {
    commands_.clear();
    ESP_LOGI(TAG, "Cleared all wake word commands");
}

void CustomWakeWordV2::AddCommand(const std::string& command, 
                                const std::string& text, 
                                const std::string& action) {
    commands_.push_back({command, text, action});
    ESP_LOGI(TAG, "Added command: command='%s', text='%s', action='%s'", 
             command.c_str(), text.c_str(), action.c_str());
}

void CustomWakeWordV2::SetThreshold(float threshold) {
    app_threshold_ = threshold;
    ESP_LOGI(TAG, "Set application layer threshold to %.3f", app_threshold_);
}

bool CustomWakeWordV2::UpdateCommands() {
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

