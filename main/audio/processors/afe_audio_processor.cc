#include "afe_audio_processor.h"
#include <esp_log.h>
#include <cmath>
#include <cstring>
#include <cstdint>

#define PROCESSOR_RUNNING 0x01

#define TAG "AfeAudioProcessor"

namespace {
struct PcmDiagStats {
    size_t sample_count = 0;
    float rms = 0.0f;
    int peak = 0;
    uint32_t clip_count = 0;
    uint32_t near_silence_count = 0;
};

static PcmDiagStats CalcPcmDiagStats(const int16_t* data, size_t sample_count) {
    PcmDiagStats stats;
    if (data == nullptr || sample_count == 0) {
        return stats;
    }

    double sum_sq = 0.0;
    for (size_t i = 0; i < sample_count; ++i) {
        const int sample = data[i];
        const int abs_sample = std::abs(sample);
        sum_sq += static_cast<double>(sample) * static_cast<double>(sample);
        if (abs_sample > stats.peak) {
            stats.peak = abs_sample;
        }
        if (abs_sample >= 32760) {
            stats.clip_count++;
        }
        if (abs_sample <= 64) {
            stats.near_silence_count++;
        }
    }

    stats.sample_count = sample_count;
    stats.rms = static_cast<float>(std::sqrt(sum_sq / static_cast<double>(sample_count)));
    return stats;
}

static inline int16_t ApplySoftGainWithLimiter(int16_t sample) {
    constexpr float kGain = 2.0f;
    constexpr int kNoiseFloor = 24;
    const int abs_sample = std::abs(static_cast<int>(sample));
    if (abs_sample <= kNoiseFloor) {
        return sample;
    }
    int boosted = static_cast<int>(static_cast<float>(sample) * kGain);
    if (boosted > 32767) {
        boosted = 32767;
    } else if (boosted < -32768) {
        boosted = -32768;
    }
    return static_cast<int16_t>(boosted);
}
}  // namespace

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
    // 兼容不同 ESP-SR 版本/模型命名：
    // 当前仓库中常见名称为 nsnet1/nsnet2/vadnet1_medium，若前缀过滤未命中，
    // 则从 models_list 里按关键字兜底挑选，避免错误退化到 passthrough。
    if (models != nullptr && (ns_model_name == nullptr || vad_model_name == nullptr)) {
        char* ns_fallback = nullptr;
        char* vad_fallback = nullptr;
        for (int i = 0; i < models->num; ++i) {
            char* name = nullptr;
            if (models->model_name != nullptr) {
                name = models->model_name[i];
            }
            if (name == nullptr && models->model_info != nullptr) {
                name = models->model_info[i];
            }
            if (name == nullptr) {
                continue;
            }
            if (ns_fallback == nullptr && std::strstr(name, "nsnet") != nullptr) {
                ns_fallback = name;
            }
            // Prefer nsnet2 when available.
            if (std::strstr(name, "nsnet2") != nullptr) {
                ns_fallback = name;
            }
            if (vad_fallback == nullptr && std::strstr(name, "vad") != nullptr) {
                vad_fallback = name;
            }
            // Prefer vadnet1_medium when available.
            if (std::strstr(name, "vadnet1_medium") != nullptr) {
                vad_fallback = name;
            }
        }
        if (ns_model_name == nullptr) {
            ns_model_name = ns_fallback;
        }
        if (vad_model_name == nullptr) {
            vad_model_name = vad_fallback;
        }
    }
    
    // 无 VAD/NS 模型且未开启 AEC 时，AFE 实际不会提供有效增强，
    // 反而可能引入额外缓冲/衰减；此时退化为直通 mic 模式。
#if CONFIG_USE_DEVICE_AEC
    constexpr bool kDeviceAecEnabled = true;
#else
    constexpr bool kDeviceAecEnabled = false;
#endif
    if (vad_model_name == nullptr && ns_model_name == nullptr && !kDeviceAecEnabled) {
        passthrough_mode_ = true;
        passthrough_input_channels_ = codec_->input_channels();
        passthrough_buffer_.clear();
        passthrough_buffer_.reserve(frame_samples_);
        ESP_LOGW(TAG, "⚠️ No VAD/NS models, fallback to passthrough mic mode (skip AFE fetch)");
        return;
    }

    afe_config_t* afe_config = afe_config_init(input_format.c_str(), NULL, AFE_TYPE_VC, AFE_MODE_HIGH_PERF);
    afe_config->aec_mode = AEC_MODE_VOIP_HIGH_PERF;
    afe_config->vad_mode = VAD_MODE_3;
    afe_config->vad_min_noise_ms = 180;
    
    // ⚠️ CRITICAL: 只有在找到有效的 VAD 模型时才设置模型名称
    if (vad_model_name != nullptr) {
        afe_config->vad_model_name = vad_model_name;
        ESP_LOGI(TAG, "✅ VAD model found: %s", vad_model_name);
    } else {
        ESP_LOGW(TAG, "⚠️ No VAD model found in partition table");
    }

    if (ns_model_name != nullptr) {
        afe_config->ns_init = true;
        afe_config->ns_model_name = ns_model_name;
        afe_config->afe_ns_mode = AFE_NS_MODE_NET;
        ESP_LOGI(TAG, "✅ NS model found: %s", ns_model_name);
    } else {
        afe_config->ns_init = false;
        ESP_LOGW(TAG, "⚠️ No NS model found, noise suppression disabled");
    }

    afe_config->agc_init = false;
    afe_config->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;

#ifdef CONFIG_USE_DEVICE_AEC
    afe_config->aec_init = true;
    afe_config->vad_init = false;
    ESP_LOGI(TAG, "Device AEC enabled, VAD disabled");
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

    afe_iface_ = esp_afe_handle_from_config(afe_config);
    afe_data_ = afe_iface_->create_from_config(afe_config);
    
    ESP_LOGI(TAG, "Creating AFE processor task (stack: 8192 bytes)...");
    
    // Allocate task stack in PSRAM to save SRAM
    if (!task_stack_) task_stack_ = (StackType_t*)heap_caps_malloc(8192, MALLOC_CAP_SPIRAM);
    if (!task_buffer_) task_buffer_ = (StaticTask_t*)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    if (task_stack_ && task_buffer_) {
        // Move AFE task to Core 1 to offload Core 0
        task_handle_ = xTaskCreateStaticPinnedToCore([](void* arg) {
            auto this_ = (AfeAudioProcessor*)arg;
            ESP_LOGI("AfeAudioProcessor", "🚀 AFE task started on core %d!", xPortGetCoreID());
            this_->AudioProcessorTask();
            vTaskDelete(NULL);
        }, "afe_proc", 8192, this, 4, task_stack_, task_buffer_, 1);
        
        ESP_LOGI(TAG, "✅ AFE task created (stack: 8192, core: 1, prio: 4, PSRAM)");
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
    if (passthrough_mode_) {
        return frame_samples_;
    }
    if (afe_data_ == nullptr) {
        return 0;
    }
    return afe_iface_->get_feed_chunksize(afe_data_);
}

void AfeAudioProcessor::Feed(std::vector<int16_t>&& data) {
    if (passthrough_mode_) {
        if (!is_running_ || !output_callback_) {
            return;
        }
        passthrough_diag_count_++;
        std::vector<int16_t> mono_frame;
        mono_frame.reserve(frame_samples_);

        if (passthrough_input_channels_ <= 1) {
            mono_frame = std::move(data);
        } else {
            const size_t frames = data.size() / static_cast<size_t>(passthrough_input_channels_);
            mono_frame.reserve(frames);
            double ch0_sum_sq = 0.0;
            double ch1_sum_sq = 0.0;
            for (size_t i = 0; i < frames; ++i) {
                const int16_t ch0 = data[i * static_cast<size_t>(passthrough_input_channels_)];
                const int16_t ch1 = data[i * static_cast<size_t>(passthrough_input_channels_) + 1];
                ch0_sum_sq += static_cast<double>(ch0) * static_cast<double>(ch0);
                ch1_sum_sq += static_cast<double>(ch1) * static_cast<double>(ch1);
            }

            const float ch0_rms = frames > 0 ? static_cast<float>(std::sqrt(ch0_sum_sq / static_cast<double>(frames))) : 0.0f;
            const float ch1_rms = frames > 0 ? static_cast<float>(std::sqrt(ch1_sum_sq / static_cast<double>(frames))) : 0.0f;
            constexpr float kSwitchRatio = 1.35f;
            if (passthrough_selected_channel_ == 0) {
                if (ch1_rms > ch0_rms * kSwitchRatio) {
                    passthrough_selected_channel_ = 1;
                }
            } else {
                if (ch0_rms > ch1_rms * kSwitchRatio) {
                    passthrough_selected_channel_ = 0;
                }
            }

            if (passthrough_diag_count_ <= 8 || passthrough_diag_count_ % 50 == 0) {
                ESP_LOGI(
                    TAG,
                    "[PT-CH#%u] ch0_rms=%.1f ch1_rms=%.1f selected=%d",
                    static_cast<unsigned>(passthrough_diag_count_),
                    ch0_rms,
                    ch1_rms,
                    passthrough_selected_channel_
                );
            }
            for (size_t i = 0; i < frames; ++i) {
                const size_t base = i * static_cast<size_t>(passthrough_input_channels_);
                const int16_t mic = data[base + static_cast<size_t>(passthrough_selected_channel_)];
                mono_frame.push_back(mic);
            }
        }

        for (auto& sample : mono_frame) {
            sample = ApplySoftGainWithLimiter(sample);
        }

        // 噪声场景轻量门控：使用“双阈值”避免语音被切碎。
        // 1) 起声阈值较高，降低噪声误触发；
        // 2) 进入语音态后使用更低的维持阈值，保留轻声/尾音连续性。
        const PcmDiagStats frame_stats = CalcPcmDiagStats(mono_frame.data(), mono_frame.size());
        constexpr float kVoiceRmsStartThreshold = 260.0f;
        constexpr int kVoicePeakStartThreshold = 1000;
        constexpr float kVoiceRmsKeepThreshold = 170.0f;
        constexpr int kVoicePeakKeepThreshold = 700;
        constexpr int kVoiceAttackFrames = 3;  // require ~180ms sustained voice
        constexpr int kHangoverFrames = 10;    // keep ~600ms tail
        const bool voice_like_start =
            (frame_stats.rms >= kVoiceRmsStartThreshold) || (frame_stats.peak >= kVoicePeakStartThreshold);
        const bool voice_like_keep =
            (frame_stats.rms >= kVoiceRmsKeepThreshold) || (frame_stats.peak >= kVoicePeakKeepThreshold);

        if (!passthrough_voice_active_) {
            if (voice_like_start) {
                passthrough_voice_attack_frames_++;
                if (passthrough_voice_attack_frames_ >= kVoiceAttackFrames) {
                    passthrough_voice_active_ = true;
                    passthrough_silence_frames_ = 0;
                    passthrough_voice_attack_frames_ = 0;
                }
            } else {
                passthrough_voice_attack_frames_ = 0;
            }
        } else if (voice_like_keep) {
            passthrough_silence_frames_ = 0;
        } else {
            passthrough_silence_frames_++;
            if (passthrough_silence_frames_ >= kHangoverFrames) {
                passthrough_voice_active_ = false;
                passthrough_silence_frames_ = 0;
            }
        }

        if (passthrough_voice_active_) {
            output_callback_(std::move(mono_frame));
        }
        return;
    }
    if (afe_data_ == nullptr) {
        return;
    }
    afe_iface_->feed(afe_data_, data.data());
}

void AfeAudioProcessor::Start() {
    if (passthrough_mode_) {
        is_running_ = true;
        passthrough_selected_channel_ = 0;
        passthrough_diag_count_ = 0;
        passthrough_voice_active_ = false;
        passthrough_voice_attack_frames_ = 0;
        passthrough_silence_frames_ = 0;
        return;
    }
    afe_uplink_active_ = false;
    afe_uplink_silence_frames_ = 0;
    afe_uplink_attack_frames_ = 0;
    xEventGroupSetBits(event_group_, PROCESSOR_RUNNING);
}

void AfeAudioProcessor::Stop() {
    if (passthrough_mode_) {
        is_running_ = false;
        passthrough_buffer_.clear();
        passthrough_selected_channel_ = 0;
        passthrough_diag_count_ = 0;
        passthrough_voice_active_ = false;
        passthrough_voice_attack_frames_ = 0;
        passthrough_silence_frames_ = 0;
        ESP_LOGI(TAG, "Passthrough processor stopped");
        return;
    }
    afe_uplink_active_ = false;
    afe_uplink_silence_frames_ = 0;
    afe_uplink_attack_frames_ = 0;
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
    if (passthrough_mode_) {
        return is_running_;
    }
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

        const size_t samples = res->data_size / sizeof(int16_t);
        const PcmDiagStats frame_stats = CalcPcmDiagStats(res->data, samples);

        // Segment-level uplink gate:
        // 1) VAD must say "speech"
        // 2) and signal energy must be high enough
        // This avoids long false-positive uploads when noise keeps VAD active.
        constexpr float kSpeechRmsThreshold = 160.0f;
        constexpr int kSpeechPeakThreshold = 600;
        constexpr int kUplinkHangoverFrames = 15; // ~480ms at 32ms/frame
        constexpr int kUplinkAttackFrames = 3;    // ~96ms at 32ms/frame
        const bool speech_like = (res->vad_state == VAD_SPEECH) &&
            (frame_stats.rms >= kSpeechRmsThreshold || frame_stats.peak >= kSpeechPeakThreshold);

        if (speech_like) {
            afe_uplink_silence_frames_ = 0;
            if (!afe_uplink_active_) {
                afe_uplink_attack_frames_++;
                if (afe_uplink_attack_frames_ >= kUplinkAttackFrames) {
                    ESP_LOGI(TAG, "Gate OPEN: rms=%.1f peak=%d", frame_stats.rms, frame_stats.peak);
                    afe_uplink_active_ = true;
                    afe_uplink_attack_frames_ = 0;
                }
            }
        } else {
            afe_uplink_attack_frames_ = 0;
            if (afe_uplink_active_) {
                afe_uplink_silence_frames_++;
                if (afe_uplink_silence_frames_ >= kUplinkHangoverFrames) {
                    ESP_LOGI(TAG, "Gate CLOSE");
                    afe_uplink_active_ = false;
                    afe_uplink_silence_frames_ = 0;
                }
            }
        }

        if (!afe_uplink_active_) {
            continue;
        }

        if (output_callback_) {
            static uint32_t s_post_afe_diag_count = 0;
            s_post_afe_diag_count++;
            if (s_post_afe_diag_count <= 5 || (s_post_afe_diag_count % 100 == 0)) {
                const float clip_pct = frame_stats.sample_count > 0
                    ? (100.0f * static_cast<float>(frame_stats.clip_count) / static_cast<float>(frame_stats.sample_count))
                    : 0.0f;
                const float silence_pct = frame_stats.sample_count > 0
                    ? (100.0f * static_cast<float>(frame_stats.near_silence_count) / static_cast<float>(frame_stats.sample_count))
                    : 0.0f;
                ESP_LOGI(TAG,
                         "[POST-AFE#%u] samples=%u rms=%.1f peak=%d clip=%u(%.2f%%) silence=%u(%.2f%%) vad_state=%d gate=%d",
                         static_cast<unsigned>(s_post_afe_diag_count),
                         static_cast<unsigned>(frame_stats.sample_count),
                         frame_stats.rms,
                         frame_stats.peak,
                         static_cast<unsigned>(frame_stats.clip_count),
                         clip_pct,
                         static_cast<unsigned>(frame_stats.near_silence_count),
                         silence_pct,
                         static_cast<int>(res->vad_state),
                         afe_uplink_active_ ? 1 : 0);
            }
            
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
    if (passthrough_mode_) {
        ESP_LOGW(TAG, "EnableDeviceAec ignored in passthrough mode");
        return;
    }
    if (enable) {
#if CONFIG_USE_DEVICE_AEC
        afe_iface_->disable_vad(afe_data_);
        afe_iface_->enable_aec(afe_data_);
#else
        ESP_LOGE(TAG, "Device AEC is not supported");
#endif
    } else {
        afe_iface_->disable_aec(afe_data_);
        afe_iface_->enable_vad(afe_data_);
    }
}
