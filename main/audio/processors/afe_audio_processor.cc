#include "afe_audio_processor.h"
#include <esp_log.h>

#define PROCESSOR_RUNNING 0x01

#define TAG "AfeAudioProcessor"

AfeAudioProcessor::AfeAudioProcessor()
    : afe_data_(nullptr) {
    event_group_ = xEventGroupCreate();
}

void AfeAudioProcessor::Initialize(AudioCodec* codec, int frame_duration_ms, srmodel_list_t* models_list) {
    // 防止重复初始化
    if (afe_data_ != nullptr) {
        ESP_LOGW(TAG, "AfeAudioProcessor already initialized, skipping");
        return;
    }

    codec_ = codec;
    frame_samples_ = frame_duration_ms * 16000 / 1000;

    // Pre-allocate output buffer capacity
    output_buffer_.reserve(frame_samples_);

    int ref_num = codec_->input_reference() ? 1 : 0;

    std::string input_format;
    for (int i = 0; i < codec_->input_channels() - ref_num; i++) {
        input_format.push_back('M');
    }
    for (int i = 0; i < ref_num; i++) {
        input_format.push_back('R');
    }

    srmodel_list_t *models;
    if (models_list == nullptr) {
        models = esp_srmodel_init("model");
    } else {
        models = models_list;
    }

    char* ns_model_name = esp_srmodel_filter(models, ESP_NSNET_PREFIX, NULL);
    char* vad_model_name = esp_srmodel_filter(models, ESP_VADN_PREFIX, NULL);
    
    // ═══════════════════════════════════════════════════════════════════════════
    // AFE 配置：使用 LOW_COST 模式以减少 CPU 占用
    // ═══════════════════════════════════════════════════════════════════════════
    // HIGH_PERF 模式包含非线性噪声抑制，处理延迟非常大（每帧 >50ms）
    // LOW_COST 模式处理延迟小（每帧 <10ms），足够用于实时打断检测
    // ═══════════════════════════════════════════════════════════════════════════
    afe_config_t* afe_config = afe_config_init(input_format.c_str(), NULL, AFE_TYPE_VC, AFE_MODE_LOW_COST);
    
    // ═══════════════════════════════════════════════════════════════════════════
    // AEC 参数调整：降低消除强度，让用户语音能"泄漏"出来
    // ═══════════════════════════════════════════════════════════════════════════
    // 
    // 问题：VOIP 模式的 AEC 消除效果过强，把用户语音也消除了
    //      导致服务端 VAD 无法检测到用户说话，打断功能失效
    // 
    // 解决方案：
    //   1. 使用 SR (Speech Recognition) 模式而不是 VOIP 模式
    //      SR 模式设计用于语音识别，更注重保留语音特征
    //   2. 减小 filter_length（默认 4）到 2
    //      filter_length 越小，回声消除效果越弱，但用户语音保留更多
    // 
    // 权衡：会有一些回声残留，但服务端 VAD 能检测到用户语音
    // 
    // ═══════════════════════════════════════════════════════════════════════════
    afe_config->aec_mode = AEC_MODE_SR_LOW_COST;  // 语音识别模式，消除效果更温和
    afe_config->aec_filter_length = 2;            // 减小滤波器长度（默认 4）
    
    afe_config->vad_mode = VAD_MODE_0;
    afe_config->vad_min_noise_ms = 100;
    
    ESP_LOGI(TAG, "🎚️ AEC: SR_LOW_COST mode, filter_length=2 (reduced for barge-in)");
    
    // 设置 AFE 任务运行在 Core 1，优先级 5（高于默认值，确保实时处理）
    afe_config->afe_perferred_core = 1;
    afe_config->afe_perferred_priority = 5;
    
    // ⚠️ CRITICAL: 只有在找到有效的 VAD 模型时才设置模型名称
    if (vad_model_name != nullptr) {
        afe_config->vad_model_name = vad_model_name;
        ESP_LOGI(TAG, "✅ VAD model found: %s", vad_model_name);
    } else {
        ESP_LOGW(TAG, "⚠️ No VAD model found in partition table");
    }

    // 禁用 NS（噪声抑制）以减少 CPU 占用
    // 对于实时打断检测，NS 不是必需的
    afe_config->ns_init = false;
    if (ns_model_name != nullptr) {
        ESP_LOGI(TAG, "ℹ️  NS model found: %s (disabled to save CPU)", ns_model_name);
    } else {
        ESP_LOGW(TAG, "⚠️ No NS model found in partition table");
    }

    afe_config->agc_init = false;
    afe_config->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;

// ═══════════════════════════════════════════════════════════════════════════
// AEC 配置：始终启用 AEC，但通过清零参考通道来控制行为
// 
// 方案：在 AudioInputTask 中，当没有 TTS 播放时，将参考通道清零
// 这样：
// - TTS 播放时：参考通道有有效信号，AEC 正常消除回声
// - 聆听模式：参考通道是零，AEC 不会消除任何东西
// 
// 这比动态调用 enable_aec()/disable_aec() 更可靠！
// ═══════════════════════════════════════════════════════════════════════════
#ifdef CONFIG_USE_DEVICE_AEC
    afe_config->aec_init = true;
    afe_config->vad_init = false;
    ESP_LOGI(TAG, "✅ AEC enabled (reference channel will be zeroed when not playing)");
#else
    afe_config->aec_init = false;
    // ⚠️ CRITICAL: 只有在有有效 VAD 模型时才启用 VAD
    // 如果没有模型但 vad_init = true，会导致 vad_trigger_detect 崩溃 (LoadProhibited)
    if (vad_model_name != nullptr) {
        afe_config->vad_init = true;
        ESP_LOGI(TAG, "✅ VAD enabled with model");
    } else {
        afe_config->vad_init = false;
        ESP_LOGW(TAG, "⚠️ VAD disabled (no model available)");
    }
#endif

    ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║   🎛️  AFE Configuration (Optimized for Realtime Interrupt)   ║");
    ESP_LOGI(TAG, "╠══════════════════════════════════════════════════════════════╣");
    ESP_LOGI(TAG, "║   Mode: LOW_COST (reduced CPU usage)                         ║");
    ESP_LOGI(TAG, "║   AEC:  VOIP_LOW_COST                                        ║");
    ESP_LOGI(TAG, "║   NS:   Disabled (save CPU for realtime processing)          ║");
    ESP_LOGI(TAG, "║   Core: 1, Priority: 5                                       ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════════════╝");
    
    afe_iface_ = esp_afe_handle_from_config(afe_config);
    afe_data_ = afe_iface_->create_from_config(afe_config);
    
    // AEC 始终启用，通过清零参考通道来控制（在 AudioInputTask 中）
    // 初始状态：aec_enabled_ = true（AEC 处理开启）
    // 但 playback_mode_ 默认 false，所以参考通道会被清零，AEC 实际无效果
    aec_enabled_ = true;
    ESP_LOGI(TAG, "🎤 AEC initialized (reference channel control via playback_mode)");
    
    ESP_LOGI(TAG, "Creating AFE processor task (stack: 8192 bytes)...");
    
    // Allocate task stack in PSRAM to save SRAM
    if (!task_stack_) task_stack_ = (StackType_t*)heap_caps_malloc(8192, MALLOC_CAP_SPIRAM);
    if (!task_buffer_) task_buffer_ = (StaticTask_t*)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    if (task_stack_ && task_buffer_) {
        // Move AFE task to Core 1 to offload Core 0
        // Priority 5: higher than audio output (4) to ensure realtime processing
        task_handle_ = xTaskCreateStaticPinnedToCore([](void* arg) {
            auto this_ = (AfeAudioProcessor*)arg;
            ESP_LOGI("AfeAudioProcessor", "🚀 AFE task started on core %d!", xPortGetCoreID());
            this_->AudioProcessorTask();
            vTaskDelete(NULL);
        }, "afe_proc", 8192, this, 5, task_stack_, task_buffer_, 1);
        
        ESP_LOGI(TAG, "✅ AFE task created (stack: 8192, core: 1, prio: 5, PSRAM)");
    } else {
        ESP_LOGE(TAG, "❌ Failed to allocate AFE task stack in PSRAM!");
    }
}

AfeAudioProcessor::~AfeAudioProcessor() {
    if (afe_data_ != nullptr) {
        afe_iface_->destroy(afe_data_);
    }
    vEventGroupDelete(event_group_);
    
    if (task_stack_) {
        heap_caps_free(task_stack_);
        task_stack_ = nullptr;
    }
    if (task_buffer_) {
        heap_caps_free(task_buffer_);
        task_buffer_ = nullptr;
    }
}

size_t AfeAudioProcessor::GetFeedSize() {
    if (afe_data_ == nullptr) {
        return 0;
    }
    return afe_iface_->get_feed_chunksize(afe_data_);
}

void AfeAudioProcessor::Feed(std::vector<int16_t>&& data) {
    if (afe_data_ == nullptr) {
        return;
    }
    afe_iface_->feed(afe_data_, data.data());
}

void AfeAudioProcessor::Start() {
    xEventGroupSetBits(event_group_, PROCESSOR_RUNNING);
}

void AfeAudioProcessor::Stop() {
    ESP_LOGI(TAG, "Stopping AfeAudioProcessor...");
    
    // 1. 清除事件位，通知任务停止
    xEventGroupClearBits(event_group_, PROCESSOR_RUNNING);
    
    // 2. 重置 AFE 缓冲区，中断可能阻塞的 fetch()
    if (afe_data_ != nullptr && afe_iface_ != nullptr) {
        afe_iface_->reset_buffer(afe_data_);
    }
    
    // 3. 等待一小段时间，让任务真正停止
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // 4. 再次重置缓冲区（以防万一）
    if (afe_data_ != nullptr && afe_iface_ != nullptr) {
        afe_iface_->reset_buffer(afe_data_);
    }
    
    ESP_LOGI(TAG, "AfeAudioProcessor stopped");
}

bool AfeAudioProcessor::IsRunning() {
    return xEventGroupGetBits(event_group_) & PROCESSOR_RUNNING;
}

void AfeAudioProcessor::OnOutput(std::function<void(std::vector<int16_t>&& data)> callback) {
    output_callback_ = callback;
}

void AfeAudioProcessor::OnVadStateChange(std::function<void(bool speaking)> callback) {
    vad_state_change_callback_ = callback;
}

void AfeAudioProcessor::AudioProcessorTask() {
    auto fetch_size = afe_iface_->get_fetch_chunksize(afe_data_);
    auto feed_size = afe_iface_->get_feed_chunksize(afe_data_);
    ESP_LOGI(TAG, "Audio communication task started, feed size: %d fetch size: %d",
        feed_size, fetch_size);

    int loop_count = 0;
    while (true) {
        xEventGroupWaitBits(event_group_, PROCESSOR_RUNNING, pdFALSE, pdTRUE, portMAX_DELAY);

        // ═══════════════════════════════════════════════════════════════════════════
        // 【旁路模式】bypass_mode_ = true 时，AFE 任务暂停处理
        // ═══════════════════════════════════════════════════════════════════════════
        // 在 Listening 模式下，不需要 AEC 处理，直接旁路 AFE 节省 CPU
        // AFE 任务进入等待状态，每 100ms 检查一次是否退出旁路模式
        // ═══════════════════════════════════════════════════════════════════════════
        if (bypass_mode_) {
            static int bypass_log_count = 0;
            bypass_log_count++;
            if (bypass_log_count % 50 == 1) {
                ESP_LOGI(TAG, "💤 AFE in bypass mode, sleeping... (count=%d)", bypass_log_count);
            }
            vTaskDelay(pdMS_TO_TICKS(100));  // 休眠 100ms，节省 CPU
            continue;
        }

        loop_count++;
        if (loop_count % 100 == 0) {
            ESP_LOGI(TAG, "📥 AFE fetch loop running (count=%d)...", loop_count);
        }

        // 使用较短的超时时间进行 fetch，这样可以及时响应 Stop() 调用
        auto res = afe_iface_->fetch_with_delay(afe_data_, pdMS_TO_TICKS(100));
        if ((xEventGroupGetBits(event_group_) & PROCESSOR_RUNNING) == 0) {
            ESP_LOGD(TAG, "  PROCESSOR_RUNNING bit cleared, skipping...");
            continue;
        }
        if (res == nullptr || res->ret_value == ESP_FAIL) {
            // 超时或失败，继续等待（不打印日志避免刷屏）
            continue;
        }

        // VAD state change
        if (vad_state_change_callback_) {
            if (res->vad_state == VAD_SPEECH && !is_speaking_) {
                is_speaking_ = true;
                vad_state_change_callback_(true);
            } else if (res->vad_state == VAD_SILENCE && is_speaking_) {
                is_speaking_ = false;
                vad_state_change_callback_(false);
            }
        }

        if (output_callback_) {
            size_t samples = res->data_size / sizeof(int16_t);
            
            // Add data to buffer
            output_buffer_.insert(output_buffer_.end(), res->data, res->data + samples);
            
            // Output complete frames when buffer has enough data
            while (output_buffer_.size() >= frame_samples_) {
                if (output_buffer_.size() == frame_samples_) {
                    // If buffer size equals frame size, copy the entire buffer and clear
                    output_callback_(std::vector<int16_t>(output_buffer_.begin(), output_buffer_.end()));
                    output_buffer_.clear();
                    output_buffer_.reserve(frame_samples_);
                } else {
                    // If buffer size exceeds frame size, copy one frame and remove it
                    output_callback_(std::vector<int16_t>(output_buffer_.begin(), output_buffer_.begin() + frame_samples_));
                    output_buffer_.erase(output_buffer_.begin(), output_buffer_.begin() + frame_samples_);
                }
            }
        }
    }
}

void AfeAudioProcessor::EnableDeviceAec(bool enable) {
    // ═══════════════════════════════════════════════════════════════════════════
    // AEC 控制方式：通过清零参考通道，而不是调用 enable_aec()/disable_aec()
    // 
    // 原因：ESP-ADF 的动态 AEC 控制有问题，会导致 AFE 输出异常
    // 
    // 新方案：AEC 始终启用，但在 AudioInputTask 中根据 playback_mode_ 决定
    // 是否清零参考通道：
    // - playback_mode_ = true → 参考通道保持原样，AEC 正常工作
    // - playback_mode_ = false → 参考通道清零，AEC 无效果
    // ═══════════════════════════════════════════════════════════════════════════
    aec_enabled_ = enable;
    if (enable) {
        ESP_LOGI(TAG, "🔊 AEC mode: reference channel ACTIVE (echo cancellation ON)");
    } else {
        ESP_LOGI(TAG, "🎤 AEC mode: reference channel ZEROED (echo cancellation OFF)");
    }
    // 实际控制在 AudioInputTask 中通过 playback_mode_ 完成
}

void AfeAudioProcessor::SetBypassMode(bool bypass) {
    // ═══════════════════════════════════════════════════════════════════════════
    // 旁路模式控制：
    // ═══════════════════════════════════════════════════════════════════════════
    // 
    // bypass = true (Listening 模式)：
    //   - AFE 任务暂停处理，进入休眠
    //   - 音频数据直接从麦克风输出，不经过 AFE
    //   - 节省大量 CPU（AFE 处理是 CPU 密集型的）
    // 
    // bypass = false (Speaking 模式)：
    //   - AFE 任务正常工作
    //   - 启用 AEC 消除扬声器回声
    //   - 用于打断检测
    // 
    // ═══════════════════════════════════════════════════════════════════════════
    if (bypass_mode_ != bypass) {
        bypass_mode_ = bypass;
        if (bypass) {
            ESP_LOGI(TAG, "💤 AFE bypass mode ENABLED (Listening, save CPU)");
        } else {
            ESP_LOGI(TAG, "🔊 AFE bypass mode DISABLED (Speaking, AEC active)");
        }
    }
}
