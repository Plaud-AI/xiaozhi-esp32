#include "audio_service.h"
#include "wake_word_manager.h"
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <cstring>
#include <cmath>

#if CONFIG_USE_AUDIO_PROCESSOR
#include "processors/afe_audio_processor.h"
#else
#include "processors/no_audio_processor.h"
#endif

#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32P4
#include "wake_words/afe_wake_word.h"
#include "wake_words/custom_wake_word.h"
#include "wake_words/tf_custom_wake_word.h"
#include "wake_words/micro/micro_wake_word.h"
#if CONFIG_USE_MICRO_WAKE_WORD
// Micro Wake Word Models - All models are compiled in, enabled/disabled at runtime via BLE

// ESPHome official v2 model "Okay Nabu" (always enabled)
#include "wake_words/micro/okay_nabu.h"

// Custom "Hey Ploud" model (default enabled)
#include "wake_words/micro/hey_ploud.h"

// Custom "Hey HelloKitty" model
#include "wake_words/micro/hey_hellokitty.h"

// Custom "Hey IronMan" model
#include "wake_words/micro/hey_ironman.h"

// Custom "Hey Luigi" model
#include "wake_words/micro/hey_luigi.h"

// Custom "Hey Stitch" model
#include "wake_words/micro/hey_stitch.h"

#endif  // CONFIG_USE_MICRO_WAKE_WORD
#else
#include "wake_words/esp_wake_word.h"
#endif

#define TAG "AudioService"


AudioService::AudioService() {
    event_group_ = xEventGroupCreate();
    if (!event_group_) {
        ESP_LOGE(TAG, "❌ Failed to create event group!");
    } else {
        ESP_LOGI(TAG, "✅ AudioService event_group_ created at %p", event_group_);
    }
}

AudioService::~AudioService() {
    ESP_LOGW(TAG, "⚠️  AudioService destructor called! This should NOT happen during normal operation!");
    
    // 先停止服务，确保所有任务退出
    Stop();
    
    // 等待任务真正退出
    vTaskDelay(pdMS_TO_TICKS(100));
    
    if (event_group_ != nullptr) {
        ESP_LOGI(TAG, "Deleting event_group_ at %p", event_group_);
        vEventGroupDelete(event_group_);
        event_group_ = nullptr;
    }

    // Free task stacks
    if (audio_input_task_stack_) { heap_caps_free(audio_input_task_stack_); audio_input_task_stack_ = nullptr; }
    if (audio_input_task_buffer_) { heap_caps_free(audio_input_task_buffer_); audio_input_task_buffer_ = nullptr; }
    if (audio_output_task_stack_) { heap_caps_free(audio_output_task_stack_); audio_output_task_stack_ = nullptr; }
    if (audio_output_task_buffer_) { heap_caps_free(audio_output_task_buffer_); audio_output_task_buffer_ = nullptr; }
    if (opus_codec_task_stack_) { heap_caps_free(opus_codec_task_stack_); opus_codec_task_stack_ = nullptr; }
    if (opus_codec_task_buffer_) { heap_caps_free(opus_codec_task_buffer_); opus_codec_task_buffer_ = nullptr; }
}


void AudioService::Initialize(AudioCodec* codec) {
    codec_ = codec;
    codec_->Start();

    /* Setup the audio codec */
    opus_decoder_ = std::make_unique<OpusDecoderWrapper>(codec->output_sample_rate(), 1, OPUS_FRAME_DURATION_MS);
    opus_encoder_ = std::make_unique<OpusEncoderWrapper>(16000, 1, OPUS_FRAME_DURATION_MS);
    opus_encoder_->SetComplexity(0);

    if (codec->input_sample_rate() != 16000) {
        input_resampler_.Configure(codec->input_sample_rate(), 16000);
        reference_resampler_.Configure(codec->input_sample_rate(), 16000);
    }

#if CONFIG_USE_AUDIO_PROCESSOR
    audio_processor_ = std::make_unique<AfeAudioProcessor>();
#else
    audio_processor_ = std::make_unique<NoAudioProcessor>();
#endif

    audio_processor_->OnOutput([this](std::vector<int16_t>&& data) {
        // ═══════════════════════════════════════════════════════════════════════════
        // 【Speaking 模式】AFE + AEC 输出回调 - 自适应降频策略
        // ═══════════════════════════════════════════════════════════════════════════
        // 
        // 策略：根据音频能量动态调整发送频率
        // 
        //   RMS > 200 (有声音)：每 2 帧发 1 帧 (120ms 间隔)
        //   RMS ≤ 200 (静音)  ：每 4 帧发 1 帧 (240ms 间隔)
        // 
        // 效果：
        //   • 减少 60-70% 发送量，避免 TCP 缓冲区堆积
        //   • 用户说话时仍有足够音频触发服务端 VAD
        //   • 保持音频流连续性，不完全丢弃静音帧
        // 
        // ═══════════════════════════════════════════════════════════════════════════
        
        static int output_count = 0;
        static int sent_count = 0;
        static int skipped_count = 0;
        static int voice_frame_idx = 0;    // 有声帧计数器
        static int silence_frame_idx = 0;  // 静音帧计数器
        output_count++;
        
        // 计算 RMS 能量
        int64_t sum_sq = 0;
        for (size_t i = 0; i < data.size(); i++) {
            sum_sq += (int64_t)data[i] * data[i];
        }
        int32_t rms = (int32_t)sqrt((double)sum_sq / data.size());
        
        // 自适应降频参数
        const int VOICE_THRESHOLD = 200;      // RMS 阈值：区分有声/静音
        const int VOICE_SEND_EVERY = 2;       // 有声帧：每 2 帧发 1 帧 (50%)
        const int SILENCE_SEND_EVERY = 4;     // 静音帧：每 4 帧发 1 帧 (25%)
        
        bool is_voice = (rms > VOICE_THRESHOLD);
        bool should_send = false;
        
        if (is_voice) {
            voice_frame_idx++;
            silence_frame_idx = 0;  // 重置静音计数器
            // 有声帧：每 VOICE_SEND_EVERY 帧发 1 帧
            should_send = (voice_frame_idx % VOICE_SEND_EVERY == 1);
        } else {
            silence_frame_idx++;
            voice_frame_idx = 0;  // 重置有声计数器
            // 静音帧：每 SILENCE_SEND_EVERY 帧发 1 帧
            should_send = (silence_frame_idx % SILENCE_SEND_EVERY == 1);
        }
        
        if (!should_send) {
            skipped_count++;
            return;  // 跳过该帧
        }
        
        sent_count++;
        
        // 定期输出统计信息（每 30 个发送的帧，约 3-6 秒）
        if (sent_count % 30 == 1) {
            const char* energy_status = (rms < 100) ? "🔇 SILENT" : 
                                        (rms < 500) ? "🔉 LOW" : 
                                        (rms < 2000) ? "🔊 NORMAL" : "📢 LOUD";
            int send_rate = (output_count > 0) ? (sent_count * 100 / output_count) : 0;
            std::lock_guard<std::mutex> lock(audio_queue_mutex_);
            ESP_LOGI(TAG, "🎙️ [AEC] #%d: RMS=%d %s, sent=%d, skip=%d (%d%%) → q=%d", 
                     output_count, rms, energy_status, sent_count, skipped_count, send_rate,
                     (int)audio_encode_queue_.size());
        }
        
        PushTaskToEncodeQueue(kAudioTaskTypeEncodeToSendQueue, std::move(data));
    });

    audio_processor_->OnVadStateChange([this](bool speaking) {
        voice_detected_ = speaking;
        if (callbacks_.on_vad_change) {
            callbacks_.on_vad_change(speaking);
        }
    });

    // esp_timer_create_args_t audio_power_timer_args = {
    //     .callback = [](void* arg) {
    //         AudioService* audio_service = (AudioService*)arg;
    //         audio_service->CheckAndUpdateAudioPowerState();
    //     },
    //     .arg = this,
    //     .dispatch_method = ESP_TIMER_TASK,
    //     .name = "audio_power_timer",
    //     .skip_unhandled_events = true,
    // };
    // esp_timer_create(&audio_power_timer_args, &audio_power_timer_);
}

void AudioService::Start() {
    service_stopped_ = false;
    xEventGroupClearBits(event_group_, AS_EVENT_AUDIO_TESTING_RUNNING | AS_EVENT_WAKE_WORD_RUNNING | AS_EVENT_AUDIO_PROCESSOR_RUNNING);

    // esp_timer_start_periodic(audio_power_timer_, 1000000);

    // Allocate stacks
    // AudioInputTask: 16KB (Move back to SRAM for stability/speed, we have enough SRAM now)
    // PSRAM stack caused IWDT crashes during high bus load (ThorVG + WiFi)
    if (!audio_input_task_stack_) audio_input_task_stack_ = (StackType_t*)heap_caps_malloc(16384, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!audio_input_task_buffer_) audio_input_task_buffer_ = (StaticTask_t*)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    
    // AudioOutputTask: 8KB (Keep in PSRAM, less critical)
    if (!audio_output_task_stack_) audio_output_task_stack_ = (StackType_t*)heap_caps_malloc(8192, MALLOC_CAP_SPIRAM);
    if (!audio_output_task_buffer_) audio_output_task_buffer_ = (StaticTask_t*)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    // OpusCodecTask: 32KB (was 26KB SRAM/PSRAM?)
    if (!opus_codec_task_stack_) opus_codec_task_stack_ = (StackType_t*)heap_caps_malloc(32768, MALLOC_CAP_SPIRAM);
    if (!opus_codec_task_buffer_) opus_codec_task_buffer_ = (StaticTask_t*)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    
    if (!audio_input_task_stack_ || !audio_input_task_buffer_ || 
        !audio_output_task_stack_ || !audio_output_task_buffer_ ||
        !opus_codec_task_stack_ || !opus_codec_task_buffer_) {
        ESP_LOGE(TAG, "Failed to allocate task stacks in PSRAM! Attempting to continue but crash likely.");
    }

#if CONFIG_USE_AUDIO_PROCESSOR
    // ═══════════════════════════════════════════════════════════════════════════
    // AEC 模式任务分配策略（修订版）：
    // ═══════════════════════════════════════════════════════════════════════════
    // Core 0: AudioInputTask (读 I2S，高优先级)
    // Core 1: AFE task (AEC 处理) - 独占核心保证实时性
    // 不绑核心: AudioOutputTask + OpusCodecTask - 让调度器自动选择
    // 
    // 注意：AudioOutputTask 不能绑定到 Core 0，否则会和 WiFi/I2S 冲突导致阻塞
    // ═══════════════════════════════════════════════════════════════════════════
    
    /* Start the audio input task */
    if (audio_input_task_stack_ && audio_input_task_buffer_) {
        // ═══════════════════════════════════════════════════════════════════════════
        // CRITICAL: AudioInputTask must NOT be on Core 0 when WiFi is active!
        // ═══════════════════════════════════════════════════════════════════════════
        // Problem: I2S DMA and WiFi DMA both use Core 0's resources.
        // When AudioInputTask runs on Core 0, it blocks WiFi TCP receive,
        // causing WebSocket frames to be lost (only first few frames received).
        // 
        // Solution: Run AudioInputTask on Core 1 alongside AFE task.
        // Both are related to audio processing and can share Core 1.
        // Core 0 is left free for WiFi/TCP/WebSocket operations.
        // 
        // Priority 4: Lower than AFE task (5) to not starve AFE processing
        // ═══════════════════════════════════════════════════════════════════════════
        audio_input_task_handle_ = xTaskCreateStaticPinnedToCore([](void* arg) {
            AudioService* audio_service = (AudioService*)arg;
            audio_service->AudioInputTask();
            vTaskDelete(NULL);
        }, "audio_input", 16384, this, 4, audio_input_task_stack_, audio_input_task_buffer_, 1);
        ESP_LOGI(TAG, "✅ AudioInputTask created (Core 1, Priority 4)");
    }

    /* Start the audio output task */
    if (audio_output_task_stack_ && audio_output_task_buffer_) {
        // AudioOutputTask 不绑定核心，让调度器自动选择
        // 绑定到 Core 0 会导致和 I2S/WiFi 冲突，OutputData 阻塞
        audio_output_task_handle_ = xTaskCreateStatic([](void* arg) {
            AudioService* audio_service = (AudioService*)arg;
            audio_service->AudioOutputTask();
            vTaskDelete(NULL);
        }, "audio_output", 8192, this, 5, audio_output_task_stack_, audio_output_task_buffer_);
        ESP_LOGI(TAG, "✅ AudioOutputTask created (no core affinity, Priority 5)");
    }
#else
    /* Start the audio input task */
    if (audio_input_task_stack_ && audio_input_task_buffer_) {
        audio_input_task_handle_ = xTaskCreateStatic([](void* arg) {
            AudioService* audio_service = (AudioService*)arg;
            audio_service->AudioInputTask();
            vTaskDelete(NULL);
        }, "audio_input", 16384, this, 8, audio_input_task_stack_, audio_input_task_buffer_);
    }

    /* Start the audio output task */
    if (audio_output_task_stack_ && audio_output_task_buffer_) {
        audio_output_task_handle_ = xTaskCreateStatic([](void* arg) {
            AudioService* audio_service = (AudioService*)arg;
            audio_service->AudioOutputTask();
            vTaskDelete(NULL);
        }, "audio_output", 8192, this, 4, audio_output_task_stack_, audio_output_task_buffer_);
    }
#endif

    /* Start the opus codec task */
    // Priority 6: higher than AudioOutputTask (5) to ensure encoding keeps up
    // No core affinity: let scheduler balance the load
    if (opus_codec_task_stack_ && opus_codec_task_buffer_) {
        opus_codec_task_handle_ = xTaskCreateStatic([](void* arg) {
            AudioService* audio_service = (AudioService*)arg;
            audio_service->OpusCodecTask();
            vTaskDelete(NULL);
        }, "opus_codec", 32768, this, 6, opus_codec_task_stack_, opus_codec_task_buffer_);
        ESP_LOGI(TAG, "✅ OpusCodecTask created (no core affinity, Priority 6)");
    }
    
    ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║   📊 Audio Task Distribution for AEC Mode                    ║");
    ESP_LOGI(TAG, "╠══════════════════════════════════════════════════════════════╣");
    ESP_LOGI(TAG, "║   Core 0: WiFi/TCP ONLY - Dedicated for network operations   ║");
    ESP_LOGI(TAG, "║   Core 1: AudioInput(4) + AFE/AEC(5) - Audio processing      ║");
    ESP_LOGI(TAG, "║   Float:  AudioOutput(5) + OpusCodec(6) - Auto scheduled     ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════════════╝");
}

void AudioService::Stop() {
    // esp_timer_stop(audio_power_timer_);
    service_stopped_ = true;
    xEventGroupSetBits(event_group_, AS_EVENT_AUDIO_TESTING_RUNNING |
        AS_EVENT_WAKE_WORD_RUNNING |
        AS_EVENT_AUDIO_PROCESSOR_RUNNING);

    std::lock_guard<std::mutex> lock(audio_queue_mutex_);
    audio_encode_queue_.clear();
    audio_decode_queue_.clear();
    audio_playback_queue_.clear();
    audio_testing_queue_.clear();
    audio_queue_cv_.notify_all();
}

bool AudioService::ReadAudioData(std::vector<int16_t>& data, int sample_rate, int samples) {
    if (!codec_->input_enabled()) {
        // esp_timer_stop(audio_power_timer_);
        // esp_timer_start_periodic(audio_power_timer_, AUDIO_POWER_CHECK_INTERVAL_MS * 1000);
        codec_->EnableInput(true);
    }

    if (codec_->input_sample_rate() != sample_rate) {
        data.resize(samples * codec_->input_sample_rate() / sample_rate * codec_->input_channels());
        if (!codec_->InputData(data)) {
            return false;
        }
        if (codec_->input_channels() == 2) {
            auto mic_channel = std::vector<int16_t>(data.size() / 2);
            auto reference_channel = std::vector<int16_t>(data.size() / 2);
            for (size_t i = 0, j = 0; i < mic_channel.size(); ++i, j += 2) {
                mic_channel[i] = data[j];
                reference_channel[i] = data[j + 1];
            }
            auto resampled_mic = std::vector<int16_t>(input_resampler_.GetOutputSamples(mic_channel.size()));
            auto resampled_reference = std::vector<int16_t>(reference_resampler_.GetOutputSamples(reference_channel.size()));
            input_resampler_.Process(mic_channel.data(), mic_channel.size(), resampled_mic.data());
            reference_resampler_.Process(reference_channel.data(), reference_channel.size(), resampled_reference.data());
            data.resize(resampled_mic.size() + resampled_reference.size());
            for (size_t i = 0, j = 0; i < resampled_mic.size(); ++i, j += 2) {
                data[j] = resampled_mic[i];
                data[j + 1] = resampled_reference[i];
            }
        } else {
            // ✅ 原始逻辑：其他情况直接重采样（不做通道提取）
            auto resampled = std::vector<int16_t>(input_resampler_.GetOutputSamples(data.size()));
            input_resampler_.Process(data.data(), data.size(), resampled.data());
            data = std::move(resampled);
        }
    } else {
        // ✅ 原始逻辑：采样率匹配，直接返回所有通道的数据
        data.resize(samples * codec_->input_channels());
        if (!codec_->InputData(data)) {
            return false;
        }
    }

    /* Update the last input time */
    last_input_time_ = std::chrono::steady_clock::now();
    debug_statistics_.input_count++;
    

#if CONFIG_USE_AUDIO_DEBUGGER
    // 音频调试：发送原始音频数据
    if (audio_debugger_ == nullptr) {
        audio_debugger_ = std::make_unique<AudioDebugger>();
    }
    audio_debugger_->Feed(data);
#endif

    return true;
}

void AudioService::AudioInputTask() {    
    while (true) {
        // 关键修复：检查 event_group_ 有效性，防止内存损坏导致崩溃
        if (!event_group_) {
            ESP_LOGE(TAG, "❌ event_group_ is NULL! Memory corruption detected!");
            break;
        }
        
        EventBits_t bits = xEventGroupWaitBits(event_group_, AS_EVENT_AUDIO_TESTING_RUNNING |
            AS_EVENT_WAKE_WORD_RUNNING | AS_EVENT_AUDIO_PROCESSOR_RUNNING,
            pdFALSE, pdFALSE, portMAX_DELAY);

        if (service_stopped_) {
            break;
        }
        if (audio_input_need_warmup_) {
            audio_input_need_warmup_ = false;
            vTaskDelay(pdMS_TO_TICKS(120));
            continue;
        }

        /* Used for audio testing in NetworkConfiguring mode by clicking the BOOT button */
        if (bits & AS_EVENT_AUDIO_TESTING_RUNNING) {
            if (audio_testing_queue_.size() >= AUDIO_TESTING_MAX_DURATION_MS / OPUS_FRAME_DURATION_MS) {
                ESP_LOGW(TAG, "Audio testing queue is full, stopping audio testing");
                EnableAudioTesting(false);
                continue;
            }
            std::vector<int16_t> data;
            int samples = OPUS_FRAME_DURATION_MS * 16000 / 1000;
            if (ReadAudioData(data, 16000, samples)) {
                // If input channels is 2, we need to fetch the left channel data
                if (codec_->input_channels() == 2) {
                    auto mono_data = std::vector<int16_t>(data.size() / 2);
                    for (size_t i = 0, j = 0; i < mono_data.size(); ++i, j += 2) {
                        mono_data[i] = data[j];
                    }
                    data = std::move(mono_data);
                }
                PushTaskToEncodeQueue(kAudioTaskTypeEncodeToTestingQueue, std::move(data));
                continue;
            }
        }

        /* Feed the wake word */
        if (bits & AS_EVENT_WAKE_WORD_RUNNING) {
            std::vector<int16_t> data;
            int samples = wake_word_->GetFeedSize();
            if (samples > 0) {
                if (ReadAudioData(data, 16000, samples)) {
                    // 🔧 如果是2通道，需要提取左声道（麦克风通道）
                    if (codec_->input_channels() == 2) {
                        auto mono_data = std::vector<int16_t>(data.size() / 2);
                        for (size_t i = 0, j = 0; i < mono_data.size(); ++i, j += 2) {
                            mono_data[i] = data[j];  // 提取 Ch0 (左声道/麦克风)
                        }
                        data = std::move(mono_data);
                    }
                    wake_word_->Feed(data);
                    continue;
                } else {
                    ESP_LOGW(TAG, "Failed to read audio data for wake word!");
                }
            } else {
                ESP_LOGW(TAG, "Wake word feed size is 0!");
            }
        }

        /* Feed the audio processor */
        if (bits & AS_EVENT_AUDIO_PROCESSOR_RUNNING) {
            static int feed_count = 0;
            static int64_t input_rms_sum = 0;
            static int input_rms_count = 0;
            static std::vector<int16_t> direct_output_buffer;  // 直接输出累积缓冲区
            feed_count++;
            
            std::vector<int16_t> data;
            int samples = audio_processor_->GetFeedSize();
            if (samples > 0) {
                if (ReadAudioData(data, 16000, samples)) {
                    // ═══════════════════════════════════════════════════════════════════════════
                    // 双模式音频处理：
                    // ═══════════════════════════════════════════════════════════════════════════
                    // 
                    // 【Listening 模式】playback_mode_ = false
                    //   - 完全旁路 AFE，节省 CPU
                    //   - 直接提取麦克风数据
                    //   - 每帧都发送，用于语音识别
                    // 
                    // 【Speaking 模式】playback_mode_ = true
                    //   - 通过 AFE + AEC 处理
                    //   - 消除扬声器回声
                    //   - 用于打断检测
                    // 
                    // ═══════════════════════════════════════════════════════════════════════════
                    
#if CONFIG_USE_DEVICE_AEC
                    // Speaking 模式的累积缓冲区（需要在这里声明以便模式切换时清空）
                    static std::vector<int16_t> afe_accumulator;
                    static int afe_feed_throttle = 0;
                    static int afe_actual_feed_count = 0;
                    
                    if (!playback_mode_) {
                        // ═══════════════════════════════════════════════════════════════════════
                        // 【Listening 模式】旁路 AFE，直接输出麦克风数据，节省 CPU
                        // ═══════════════════════════════════════════════════════════════════════
                        
                        // 清空 Speaking 模式的累积缓冲区（模式切换时）
                        if (!afe_accumulator.empty()) {
                            ESP_LOGI(TAG, "🔄 Mode switch: clearing AFE accumulator (%d samples)", 
                                     (int)afe_accumulator.size());
                            afe_accumulator.clear();
                            afe_feed_throttle = 0;
                        }
                        
                        // 提取麦克风通道数据（Ch0）
                        std::vector<int16_t> mic_data;
                        if (codec_->input_channels() == 2) {
                            // 双通道交错格式：[Mic0, Ref0, Mic1, Ref1, ...]
                            mic_data.resize(data.size() / 2);
                            for (size_t i = 0; i < mic_data.size(); i++) {
                                mic_data[i] = data[i * 2];  // 提取麦克风通道
                            }
                        } else {
                            mic_data = std::move(data);
                        }
                        
                        // 累积到 960 samples（OPUS 帧大小）
                        direct_output_buffer.insert(direct_output_buffer.end(), 
                                                    mic_data.begin(), mic_data.end());
                        
                        // 当累积够 960 samples 时，直接送入编码队列
                        while (direct_output_buffer.size() >= 960) {
                            // 提取 960 samples
                            std::vector<int16_t> frame(direct_output_buffer.begin(), 
                                                       direct_output_buffer.begin() + 960);
                            direct_output_buffer.erase(direct_output_buffer.begin(), 
                                                       direct_output_buffer.begin() + 960);
                            
                            // 计算 RMS 用于日志（每 50 帧）
                            static int direct_output_count = 0;
                            direct_output_count++;
                            if (direct_output_count % 50 == 1) {
                                int64_t sum_sq = 0;
                                for (int16_t sample : frame) {
                                    sum_sq += (int64_t)sample * sample;
                                }
                                int32_t rms = (int32_t)sqrt((double)sum_sq / frame.size());
                                
                                const char* status = (rms < 100) ? "⚠️ SILENT" : 
                                                     (rms < 500) ? "🔇 LOW" : "✅ OK";
                                
                                ESP_LOGI(TAG, "🎤 [DIRECT] output #%d: 960 samples, RMS=%d %s (bypass AFE, save CPU)", 
                                         direct_output_count, rms, status);
                            }
                            
                            // 直接送入编码队列
                            PushTaskToEncodeQueue(kAudioTaskTypeEncodeToSendQueue, std::move(frame));
                        }
                        
                        portYIELD();
                        continue;
                    }
#endif
                    
                    // ═══════════════════════════════════════════════════════════════════════════
                    // 【Speaking 模式】通过 AFE + AEC 处理
                    // ═══════════════════════════════════════════════════════════════════════════
                    // 
                    // 优化策略：
                    // 1. 每 4 次读取只 feed 一次 AFE（累积 1024 samples = 64ms）
                    // 2. 减少 AFE 处理频率，给网络任务更多 CPU 时间
                    // 3. 打断检测响应时间 ~100ms 仍然足够快
                    // 
                    // ═══════════════════════════════════════════════════════════════════════════
                    
                    // 累积音频数据，减少 AFE 调用频率
                    // (static 变量已在上面声明)
                    const int AFE_FEED_EVERY_N = 4;  // 每 4 次读取 feed 一次 AFE
                    
                    afe_accumulator.insert(afe_accumulator.end(), data.begin(), data.end());
                    afe_feed_throttle++;
                    
                    if (afe_feed_throttle < AFE_FEED_EVERY_N) {
                        // 还没累积够，等待更多数据
                        // 只 yield，不 delay（避免过度延迟）
                        portYIELD();
                        continue;
                    }
                    
                    // 累积够了，feed AFE
                    afe_feed_throttle = 0;
                    
                    // 输入音频能量监控（每 25 次 feed，约 1.6 秒）
                    // (static 变量已在上面声明)
                    afe_actual_feed_count++;
                    if (afe_actual_feed_count % 25 == 1) {
                        // 计算麦克风通道的 RMS 能量
                        int64_t sum_sq = 0;
                        size_t data_size = afe_accumulator.size();
                        int mic_samples = codec_->input_channels() == 2 ? data_size / 2 : data_size;
                        for (int i = 0; i < mic_samples; i++) {
                            int idx = codec_->input_channels() == 2 ? i * 2 : i;
                            sum_sq += (int64_t)afe_accumulator[idx] * afe_accumulator[idx];
                        }
                        int32_t input_rms = (int32_t)sqrt((double)sum_sq / mic_samples);
                        
                        const char* input_status = (input_rms < 50) ? "🔇 VERY_LOW" : 
                                                   (input_rms < 200) ? "🔉 LOW" : 
                                                   (input_rms < 1000) ? "🔊 NORMAL" : "📢 LOUD";
                        
                        ESP_LOGI(TAG, "🎤 [AFE+AEC] feed #%d: %d samples, INPUT_RMS=%d %s", 
                                 afe_actual_feed_count, (int)afe_accumulator.size(), input_rms, input_status);
                    }
                    
                    // Feed 累积的数据给 AFE（分批 feed）
                    while (afe_accumulator.size() >= (size_t)samples) {
                        std::vector<int16_t> chunk(afe_accumulator.begin(), 
                                                   afe_accumulator.begin() + samples);
                        afe_accumulator.erase(afe_accumulator.begin(), 
                                              afe_accumulator.begin() + samples);
                        audio_processor_->Feed(std::move(chunk));
                    }
                    
                    // 每次批量 feed 后 yield，让其他任务有机会运行
                    portYIELD();
                    
                    continue;
                } else {
                    ESP_LOGW(TAG, "⚠️  ReadAudioData failed for feed #%d", feed_count);
                }
            } else {
                ESP_LOGW(TAG, "⚠️  GetFeedSize returned 0");
            }
        }

        ESP_LOGE(TAG, "Should not be here, bits: %lx", bits);
        break;
    }

    ESP_LOGW(TAG, "Audio input task stopped");
}

void AudioService::AudioOutputTask() {
    static int playback_count = 0;
    
    while (true) {
        std::unique_lock<std::mutex> lock(audio_queue_mutex_);
        audio_queue_cv_.wait(lock, [this]() { return !audio_playback_queue_.empty() || service_stopped_; });
        if (service_stopped_) {
            break;
        }

        playback_count++;
        size_t playback_q_size = audio_playback_queue_.size();
        
        auto task = std::move(audio_playback_queue_.front());
        audio_playback_queue_.pop_front();
        audio_queue_cv_.notify_all();
        lock.unlock();

        // 每个包都打印日志（调试 TTS 播放问题）
            ESP_LOGI(TAG, "🔊 Playing #%d: pcm_size=%d samples, playback_q=%d", 
                     playback_count, (int)task->pcm.size(), (int)playback_q_size);

        if (!codec_->output_enabled()) {
            ESP_LOGI(TAG, "🔊 Enabling audio output...");
            // esp_timer_stop(audio_power_timer_);
            // esp_timer_start_periodic(audio_power_timer_, AUDIO_POWER_CHECK_INTERVAL_MS * 1000);
            codec_->EnableOutput(true);
        }
        
        int64_t start_time = esp_timer_get_time();
        codec_->OutputData(task->pcm);
        int64_t elapsed = (esp_timer_get_time() - start_time) / 1000;
        
        // 如果播放耗时超过 100ms，打印警告
        if (elapsed > 100) {
            ESP_LOGW(TAG, "⚠️  OutputData took %d ms (samples=%d)", (int)elapsed, (int)task->pcm.size());
        }

        /* Update the last output time */
        last_output_time_ = std::chrono::steady_clock::now();
        debug_statistics_.playback_count++;

#if CONFIG_USE_SERVER_AEC
        /* Record the timestamp for server AEC */
        if (task->timestamp > 0) {
            lock.lock();
            timestamp_queue_.push_back(task->timestamp);
        }
#endif
    }

    ESP_LOGW(TAG, "Audio output task stopped");
}

void AudioService::OpusCodecTask() {
    ESP_LOGI(TAG, "🔧 OpusCodecTask started");
    int loop_count = 0;
    
    while (true) {
        std::unique_lock<std::mutex> lock(audio_queue_mutex_);
        
        // Use timeout to detect blocking issues
        // Note: We no longer block on send_queue being full - we'll drop old packets instead
        bool notified = audio_queue_cv_.wait_for(lock, std::chrono::seconds(5), [this]() {
            return service_stopped_ ||
                !audio_encode_queue_.empty() ||  // Always process encode if available
                (!audio_decode_queue_.empty() && audio_playback_queue_.size() < MAX_PLAYBACK_TASKS_IN_QUEUE);
        });
        
        if (!notified) {
            // Timeout - log queue status
            ESP_LOGW(TAG, "⏰ OpusCodecTask timeout! encode_q=%d, send_q=%d/%d, decode_q=%d, playback_q=%d",
                     (int)audio_encode_queue_.size(), (int)audio_send_queue_.size(), MAX_SEND_PACKETS_IN_QUEUE,
                     (int)audio_decode_queue_.size(), (int)audio_playback_queue_.size());
            continue;
        }
        
        if (service_stopped_) {
            break;
        }
        
        loop_count++;
        if (loop_count % 100 == 1) {
            ESP_LOGI(TAG, "🔄 OpusCodecTask loop #%d: encode_q=%d, send_q=%d, decode_q=%d, playback_q=%d",
                     loop_count, (int)audio_encode_queue_.size(), (int)audio_send_queue_.size(),
                     (int)audio_decode_queue_.size(), (int)audio_playback_queue_.size());
        }

        /* Decode the audio from decode queue */
        if (!audio_decode_queue_.empty() && audio_playback_queue_.size() < MAX_PLAYBACK_TASKS_IN_QUEUE) {
            static int decode_count = 0;
            decode_count++;
            size_t decode_q_size = audio_decode_queue_.size();
            size_t playback_q_size = audio_playback_queue_.size();
            
            auto packet = std::move(audio_decode_queue_.front());
            audio_decode_queue_.pop_front();
            audio_queue_cv_.notify_all();
            lock.unlock();

            // 每个包都打印日志（调试 TTS 播放问题）
            ESP_LOGI(TAG, "🎵 Decoding #%d: decode_q=%d, playback_q=%d, payload=%d bytes", 
                     decode_count, (int)decode_q_size, (int)playback_q_size, (int)packet->payload.size());

            auto task = std::make_unique<AudioTask>();
            task->type = kAudioTaskTypeDecodeToPlaybackQueue;
            task->timestamp = packet->timestamp;

            SetDecodeSampleRate(packet->sample_rate, packet->frame_duration);
            if (opus_decoder_->Decode(std::move(packet->payload), task->pcm)) {
                // Resample if the sample rate is different
                if (opus_decoder_->sample_rate() != codec_->output_sample_rate()) {
                    int target_size = output_resampler_.GetOutputSamples(task->pcm.size());
                    std::vector<int16_t> resampled(target_size);
                    output_resampler_.Process(task->pcm.data(), task->pcm.size(), resampled.data());
                    task->pcm = std::move(resampled);
                }

                // Save pcm size before moving task
                size_t pcm_size = task->pcm.size();
                
                lock.lock();
                audio_playback_queue_.push_back(std::move(task));
                audio_queue_cv_.notify_all();
                
                if (decode_count % 10 == 1) {
                    ESP_LOGI(TAG, "✅ Decoded #%d: pcm_size=%d samples, playback_q now=%d", 
                             decode_count, (int)pcm_size, (int)audio_playback_queue_.size());
                }
            } else {
                ESP_LOGE(TAG, "Failed to decode audio");
                lock.lock();
            }
            debug_statistics_.decode_count++;
        }
        
        /* Encode the audio to send queue - always process, drop old if queue full */
        if (!audio_encode_queue_.empty()) {
            static int encode_count = 0;
            static int drop_count = 0;
            static int silence_skip_count = 0;
            static int playback_frame_count = 0;
            encode_count++;
            
            auto task = std::move(audio_encode_queue_.front());
            audio_encode_queue_.pop_front();
            audio_queue_cv_.notify_all();
            lock.unlock();
            
            // ============================================================
            // PLAYBACK MODE: Yield CPU for TCP receive task
            // ============================================================
            // In playback mode (Speaking + AEC), the device runs AFE + encode + 
            // decode + playback simultaneously. This can starve TCP receive task.
            // Yield CPU on every frame to ensure TTS audio can be received.
            if (playback_mode_) {
                playback_frame_count++;
                vTaskDelay(pdMS_TO_TICKS(2));  // 2ms delay to yield CPU
                
                // Log periodically
                if (playback_frame_count % 100 == 0) {
                    ESP_LOGI(TAG, "🎤 Playback mode: processed %d frames", playback_frame_count);
                }
            }
            
            // ============================================================
            // 不再做静音检测！所有帧都发送给服务器
            // ============================================================
            // 
            // 原因：
            // 1. 服务端 VAD 比设备端更准确
            // 2. Speaking 模式下需要连续音频流来检测打断
            // 3. 静音检测可能误判，导致用户无法打断 TTS
            // 
            // ============================================================
            
            // ============================================================
            // OPUS ENCODING (only for non-silent or keepalive frames)
            // ============================================================
            auto packet = std::make_unique<AudioStreamPacket>();
            packet->frame_duration = OPUS_FRAME_DURATION_MS;
            packet->sample_rate = 16000;
            packet->timestamp = task->timestamp;
            if (!opus_encoder_->Encode(std::move(task->pcm), packet->payload)) {
                ESP_LOGE(TAG, "Failed to encode audio");
                continue;
            }
            
            // ============================================================
            // 不跳过 DTX 包！发送所有包给服务器
            // ============================================================
            // DTX 包虽然很小，但仍然携带信息，服务端需要它们来
            // 正确处理音频流和 VAD 检测
            // ============================================================

            if (task->type == kAudioTaskTypeEncodeToSendQueue) {
                {
                    std::lock_guard<std::mutex> lock(audio_queue_mutex_);
                    
                    // If send_queue is full, drop oldest packets to make room (realtime audio priority)
                    while (audio_send_queue_.size() >= MAX_SEND_PACKETS_IN_QUEUE) {
                        audio_send_queue_.pop_front();  // Drop oldest
                        drop_count++;
                        if (drop_count % 10 == 1) {
                            ESP_LOGW(TAG, "⚠️ send_queue full, dropped old packet #%d", drop_count);
                        }
                    }
                    
                    audio_send_queue_.push_back(std::move(packet));
                    
                    // Log every 50 encodes
                    if (encode_count % 50 == 1) {
                        ESP_LOGI(TAG, "📤 Encoded #%d → send_queue (size=%d, dropped=%d)", 
                                 encode_count, (int)audio_send_queue_.size(), drop_count);
                    }
                }
                if (callbacks_.on_send_queue_available) {
                    callbacks_.on_send_queue_available();
                }
            } else if (task->type == kAudioTaskTypeEncodeToTestingQueue) {
                std::lock_guard<std::mutex> lock(audio_queue_mutex_);
                audio_testing_queue_.push_back(std::move(packet));
            }
            debug_statistics_.encode_count++;
            lock.lock();
        }
    }

    ESP_LOGW(TAG, "Opus codec task stopped");
}

void AudioService::SetDecodeSampleRate(int sample_rate, int frame_duration) {
    if (opus_decoder_->sample_rate() == sample_rate && opus_decoder_->duration_ms() == frame_duration) {
        return;
    }

    opus_decoder_.reset();
    opus_decoder_ = std::make_unique<OpusDecoderWrapper>(sample_rate, 1, frame_duration);

    auto codec = Board::GetInstance().GetAudioCodec();
    if (opus_decoder_->sample_rate() != codec->output_sample_rate()) {
        ESP_LOGI(TAG, "Resampling audio from %d to %d", opus_decoder_->sample_rate(), codec->output_sample_rate());
        output_resampler_.Configure(opus_decoder_->sample_rate(), codec->output_sample_rate());
    }
}

void AudioService::PushTaskToEncodeQueue(AudioTaskType type, std::vector<int16_t>&& pcm) {
    auto task = std::make_unique<AudioTask>();
    task->type = type;
    task->pcm = std::move(pcm);
    
    /* Push the task to the encode queue */
    std::unique_lock<std::mutex> lock(audio_queue_mutex_);

    /* If the task is to send queue, we need to set the timestamp */
    if (type == kAudioTaskTypeEncodeToSendQueue && !timestamp_queue_.empty()) {
        if (timestamp_queue_.size() <= MAX_TIMESTAMPS_IN_QUEUE) {
            task->timestamp = timestamp_queue_.front();
        } else {
            ESP_LOGW(TAG, "Timestamp queue (%u) is full, dropping timestamp", timestamp_queue_.size());
        }
        timestamp_queue_.pop_front();
    }

    // Non-blocking: drop OLDEST frames if queue is full (realtime priority)
    // Keep newest audio for better conversation continuity
    static int drop_count = 0;
    while (audio_encode_queue_.size() >= MAX_ENCODE_TASKS_IN_QUEUE) {
        audio_encode_queue_.pop_front();  // Drop oldest frame
        drop_count++;
        if (drop_count % 10 == 1) {
            ESP_LOGW(TAG, "⚠️  Encode queue full, dropped oldest frame #%d (queue=%d/%d)", 
                     drop_count, (int)audio_encode_queue_.size(), MAX_ENCODE_TASKS_IN_QUEUE);
        }
    }
    
    audio_encode_queue_.push_back(std::move(task));
    audio_queue_cv_.notify_all();
}

bool AudioService::PushPacketToDecodeQueue(std::unique_ptr<AudioStreamPacket> packet, bool wait) {
    static int push_count = 0;
    push_count++;
    
    std::unique_lock<std::mutex> lock(audio_queue_mutex_);
    size_t decode_size = audio_decode_queue_.size();
    size_t playback_size = audio_playback_queue_.size();
    
    // 每 10 个包或队列满时打印日志
    if (push_count % 10 == 1 || decode_size >= MAX_DECODE_PACKETS_IN_QUEUE - 1) {
        ESP_LOGI(TAG, "📥 PushToDecodeQueue #%d: decode_q=%d/%d, playback_q=%d/%d", 
                 push_count, (int)decode_size, MAX_DECODE_PACKETS_IN_QUEUE, 
                 (int)playback_size, MAX_PLAYBACK_TASKS_IN_QUEUE);
    }
    
    if (audio_decode_queue_.size() >= MAX_DECODE_PACKETS_IN_QUEUE) {
        ESP_LOGW(TAG, "⚠️  Decode queue FULL! Waiting... (decode_q=%d, playback_q=%d)", 
                 (int)decode_size, (int)playback_size);
        if (wait) {
            audio_queue_cv_.wait(lock, [this]() { return audio_decode_queue_.size() < MAX_DECODE_PACKETS_IN_QUEUE; });
            ESP_LOGI(TAG, "✅ Decode queue has space now");
        } else {
            return false;
        }
    }
    audio_decode_queue_.push_back(std::move(packet));
    audio_queue_cv_.notify_all();
    return true;
}

std::unique_ptr<AudioStreamPacket> AudioService::PopPacketFromSendQueue() {
    std::lock_guard<std::mutex> lock(audio_queue_mutex_);
    if (audio_send_queue_.empty()) {
        return nullptr;
    }
    auto packet = std::move(audio_send_queue_.front());
    audio_send_queue_.pop_front();
    audio_queue_cv_.notify_all();
    return packet;
}

void AudioService::EncodeWakeWord() {
    if (wake_word_) {
        wake_word_->EncodeWakeWordData();
    }
}

const std::string& AudioService::GetLastWakeWord() const {
    return wake_word_->GetLastDetectedWakeWord();
}

std::unique_ptr<AudioStreamPacket> AudioService::PopWakeWordPacket() {
    auto packet = std::make_unique<AudioStreamPacket>();
    if (wake_word_->GetWakeWordOpus(packet->payload)) {
        return packet;
    }
    return nullptr;
}

void AudioService::EnableWakeWordDetection(bool enable) {
    if (!wake_word_) {
        ESP_LOGW(TAG, "Wake word object is NULL, cannot enable detection!");
        return;
    }

    ESP_LOGI(TAG, "%s wake word detection", enable ? "Enabling" : "Disabling");
    if (enable) {
        if (!wake_word_initialized_) {
            ESP_LOGI(TAG, "Initializing wake word with codec input_sample_rate=%d, models_list=%p", 
                     codec_->input_sample_rate(), models_list_);
            if (!wake_word_->Initialize(codec_, models_list_)) {
                ESP_LOGE(TAG, "Failed to initialize wake word!");
                return;
            }
            wake_word_initialized_ = true;
            ESP_LOGI(TAG, "Wake word initialized successfully, feed_size=%d", wake_word_->GetFeedSize());
            
#if CONFIG_USE_CUSTOM_WAKE_WORD
            // 只有 CustomWakeWord 需要从 NVS 加载配置
            // MicroWakeWord 使用编译时配置，不需要 WakeWordManager
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "🔄 尝试从 NVS 加载保存的唤醒词配置...");
            auto& manager = WakeWordManager::GetInstance();
            if (manager.LoadFromNVS()) {
                ESP_LOGI(TAG, "✅ 从 NVS 加载了 %d 个唤醒词配置", manager.GetCount());
                
                // 尝试转换为 CustomWakeWord
                CustomWakeWord* custom_wake_word = dynamic_cast<CustomWakeWord*>(wake_word_.get());
                if (custom_wake_word) {
                    // 应用保存的配置（运行时更新）
                    if (manager.ApplyToCustomWakeWord(custom_wake_word)) {
                        ESP_LOGI(TAG, "✅✅✅ 唤醒词配置已从 NVS 加载并应用成功！");
                        ESP_LOGI(TAG, "     当前激活 %d 个唤醒词，阈值 %.3f", 
                                 manager.GetCount(), manager.GetThreshold());
                    } else {
                        ESP_LOGW(TAG, "⚠️  应用唤醒词配置失败，将使用默认配置");
                    }
                } else {
                    ESP_LOGW(TAG, "⚠️  WakeWord 不是 CustomWakeWord 类型，无法应用配置");
                }
            } else {
                ESP_LOGI(TAG, "ℹ️  NVS 中没有保存的唤醒词配置，使用默认配置");
            }
            ESP_LOGI(TAG, "");
#else
            // MicroWakeWord 或其他唤醒词引擎不需要 WakeWordManager
            ESP_LOGD(TAG, "Using built-in wake word model, WakeWordManager not needed");
#endif
        }
        wake_word_->Start();
        xEventGroupSetBits(event_group_, AS_EVENT_WAKE_WORD_RUNNING);
        ESP_LOGI(TAG, "Wake word detection started, event bit set");
    } else {
        // 先清除事件位，让 AudioInputTask 停止 feed
        xEventGroupClearBits(event_group_, AS_EVENT_WAKE_WORD_RUNNING);
        // 等待一小段时间，让 AudioInputTask 处理完最后的数据
        vTaskDelay(pdMS_TO_TICKS(20));
        // 再停止唤醒词检测（会清空 ringbuffer）
        wake_word_->Stop();
        ESP_LOGI(TAG, "Wake word detection stopped");
    }
}

void AudioService::EnableVoiceProcessing(bool enable) {
    ESP_LOGI(TAG, "%s voice processing", enable ? "Enabling" : "Disabling");
    if (enable) {
        if (!audio_processor_initialized_) {
            ESP_LOGI(TAG, "  Initializing audio processor for the first time...");
            audio_processor_->Initialize(codec_, OPUS_FRAME_DURATION_MS, models_list_);
            audio_processor_initialized_ = true;
        } else {
            ESP_LOGI(TAG, "  Audio processor already initialized, just starting...");
        }

        /* We should make sure no audio is playing */
        ResetDecoder();
        audio_input_need_warmup_ = true;
        ESP_LOGI(TAG, "  Calling audio_processor_->Start()...");
        audio_processor_->Start();
        ESP_LOGI(TAG, "  Setting AS_EVENT_AUDIO_PROCESSOR_RUNNING event bit...");
        xEventGroupSetBits(event_group_, AS_EVENT_AUDIO_PROCESSOR_RUNNING);
        ESP_LOGI(TAG, "✅ Voice processing enabled, IsRunning=%d", audio_processor_->IsRunning());
    } else {
        ESP_LOGI(TAG, "  Calling audio_processor_->Stop()...");
        audio_processor_->Stop();
        ESP_LOGI(TAG, "  Clearing AS_EVENT_AUDIO_PROCESSOR_RUNNING event bit...");
        xEventGroupClearBits(event_group_, AS_EVENT_AUDIO_PROCESSOR_RUNNING);
        ESP_LOGI(TAG, "✅ Voice processing disabled");
    }
}

void AudioService::EnableAudioTesting(bool enable) {
    ESP_LOGI(TAG, "%s audio testing", enable ? "Enabling" : "Disabling");
    if (enable) {
        xEventGroupSetBits(event_group_, AS_EVENT_AUDIO_TESTING_RUNNING);
    } else {
        xEventGroupClearBits(event_group_, AS_EVENT_AUDIO_TESTING_RUNNING);
        /* Copy audio_testing_queue_ to audio_decode_queue_ */
        std::lock_guard<std::mutex> lock(audio_queue_mutex_);
        audio_decode_queue_ = std::move(audio_testing_queue_);
        audio_queue_cv_.notify_all();
    }
}

void AudioService::EnableDeviceAec(bool enable) {
    ESP_LOGI(TAG, "%s device AEC", enable ? "Enabling" : "Disabling");
    if (!audio_processor_initialized_) {
        audio_processor_->Initialize(codec_, OPUS_FRAME_DURATION_MS, models_list_);
        audio_processor_initialized_ = true;
    }

    audio_processor_->EnableDeviceAec(enable);
}

void AudioService::SetPlaybackMode(bool playback_mode) {
    // ═══════════════════════════════════════════════════════════════════════════
    // 双模式控制：
    // 
    // playback_mode = true (Speaking 状态):
    //   - 启用 AFE + AEC 处理
    //   - 消除扬声器回声
    //   - 用于打断检测
    // 
    // playback_mode = false (Listening 状态):
    //   - 旁路 AFE，直接输出麦克风数据
    //   - 节省大量 CPU（AFE 处理是 CPU 密集型的）
    //   - 用于语音识别
    // 
    // ═══════════════════════════════════════════════════════════════════════════
    if (playback_mode_ != playback_mode) {
        playback_mode_ = playback_mode;
        
#if CONFIG_USE_DEVICE_AEC
        if (audio_processor_initialized_) {
            // 设置 AFE 旁路模式：Listening = bypass, Speaking = AEC active
            auto afe_processor = static_cast<AfeAudioProcessor*>(audio_processor_.get());
            if (afe_processor) {
                afe_processor->SetBypassMode(!playback_mode);
            }
            
            ESP_LOGI(TAG, "🎛️ SetPlaybackMode(%s) → %s", 
                     playback_mode ? "true" : "false",
                     playback_mode ? "AFE+AEC ACTIVE" : "AFE BYPASS (save CPU)");
        }
#endif
    }
}

void AudioService::SetCallbacks(AudioServiceCallbacks& callbacks) {
    callbacks_ = callbacks;
}

void AudioService::PlaySound(const std::string_view& ogg) {
    ESP_LOGI(TAG, "🎵 播放提示音: 大小=%d 字节", ogg.size());
    
    if (!codec_->output_enabled()) {
        ESP_LOGI(TAG, "🔊 音频输出未启用，正在启用...");
        // esp_timer_stop(audio_power_timer_);
        // esp_timer_start_periodic(audio_power_timer_, AUDIO_POWER_CHECK_INTERVAL_MS * 1000);
        codec_->EnableOutput(true);
    }

    const uint8_t* buf = reinterpret_cast<const uint8_t*>(ogg.data());
    size_t size = ogg.size();
    size_t offset = 0;

    auto find_page = [&](size_t start)->size_t {
        for (size_t i = start; i + 4 <= size; ++i) {
            if (buf[i] == 'O' && buf[i+1] == 'g' && buf[i+2] == 'g' && buf[i+3] == 'S') return i;
        }
        return static_cast<size_t>(-1);
    };

    bool seen_head = false;
    bool seen_tags = false;
    int sample_rate = 16000; // 默认值

    while (true) {
        size_t pos = find_page(offset);
        if (pos == static_cast<size_t>(-1)) break;
        offset = pos;
        if (offset + 27 > size) break;

        const uint8_t* page = buf + offset;
        uint8_t page_segments = page[26];
        size_t seg_table_off = offset + 27;
        if (seg_table_off + page_segments > size) break;

        size_t body_size = 0;
        for (size_t i = 0; i < page_segments; ++i) body_size += page[27 + i];

        size_t body_off = seg_table_off + page_segments;
        if (body_off + body_size > size) break;

        // Parse packets using lacing
        size_t cur = body_off;
        size_t seg_idx = 0;
        while (seg_idx < page_segments) {
            size_t pkt_len = 0;
            size_t pkt_start = cur;
            bool continued = false;
            do {
                uint8_t l = page[27 + seg_idx++];
                pkt_len += l;
                cur += l;
                continued = (l == 255);
            } while (continued && seg_idx < page_segments);

            if (pkt_len == 0) continue;
            const uint8_t* pkt_ptr = buf + pkt_start;

            if (!seen_head) {
                // 解析OpusHead包
                if (pkt_len >= 19 && std::memcmp(pkt_ptr, "OpusHead", 8) == 0) {
                    seen_head = true;
                    
                    // OpusHead结构：[0-7] "OpusHead", [8] version, [9] channel_count, [10-11] pre_skip
                    // [12-15] input_sample_rate, [16-17] output_gain, [18] mapping_family
                    if (pkt_len >= 12) {
                        uint8_t version = pkt_ptr[8];
                        uint8_t channel_count = pkt_ptr[9];
                        
                        if (pkt_len >= 16) {
                            // 读取输入采样率 (little-endian)
                            sample_rate = pkt_ptr[12] | (pkt_ptr[13] << 8) | 
                                        (pkt_ptr[14] << 16) | (pkt_ptr[15] << 24);
                            ESP_LOGI(TAG, "OpusHead: version=%d, channels=%d, sample_rate=%d", 
                                   version, channel_count, sample_rate);
                        }
                    }
                }
                continue;
            }
            if (!seen_tags) {
                // Expect OpusTags in second packet
                if (pkt_len >= 8 && std::memcmp(pkt_ptr, "OpusTags", 8) == 0) {
                    seen_tags = true;
                }
                continue;
            }

            // Audio packet (Opus)
            auto packet = std::make_unique<AudioStreamPacket>();
            packet->sample_rate = sample_rate;
            packet->frame_duration = 60;
            packet->payload.resize(pkt_len);
            std::memcpy(packet->payload.data(), pkt_ptr, pkt_len);
            PushPacketToDecodeQueue(std::move(packet), true);
        }

        offset = body_off + body_size;
    }
}

bool AudioService::IsIdle() {
    std::lock_guard<std::mutex> lock(audio_queue_mutex_);
    return audio_encode_queue_.empty() && audio_decode_queue_.empty() && audio_playback_queue_.empty() && audio_testing_queue_.empty();
}

AudioService::QueueStats AudioService::GetQueueStats() {
    std::lock_guard<std::mutex> lock(audio_queue_mutex_);
    return QueueStats{
        .encode_queue_size = audio_encode_queue_.size(),
        .send_queue_size = audio_send_queue_.size(),
        .decode_queue_size = audio_decode_queue_.size(),
        .playback_queue_size = audio_playback_queue_.size()
    };
}

void AudioService::ResetDecoder() {
    std::lock_guard<std::mutex> lock(audio_queue_mutex_);
    opus_decoder_->ResetState();
    timestamp_queue_.clear();
    audio_decode_queue_.clear();
    audio_playback_queue_.clear();
    audio_testing_queue_.clear();
    audio_queue_cv_.notify_all();
}

void AudioService::CheckAndUpdateAudioPowerState() {
    auto now = std::chrono::steady_clock::now();
    auto input_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_input_time_).count();
    auto output_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_output_time_).count();
    if (input_elapsed > AUDIO_POWER_TIMEOUT_MS && codec_->input_enabled()) {
        codec_->EnableInput(false);
    }
    if (output_elapsed > AUDIO_POWER_TIMEOUT_MS && codec_->output_enabled()) {
        codec_->EnableOutput(false);
    }
    // if (!codec_->input_enabled() && !codec_->output_enabled()) {
    //     esp_timer_stop(audio_power_timer_);
    // }
}

void AudioService::SetModelsList(srmodel_list_t* models_list) {
    models_list_ = models_list;

    ESP_LOGI(TAG, "SetModelsList called, models_list: %p", models_list);
    if (models_list_ != nullptr) {
        ESP_LOGI(TAG, "Models list count: %d", models_list_->num);
        for (int i = 0; i < models_list_->num && i < 10; i++) {
            ESP_LOGI(TAG, "  Model %d: %s", i, models_list_->model_name[i]);
        }
    } else {
        ESP_LOGW(TAG, "Models list is NULL!");
    }

#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32P4
    
#if CONFIG_USE_MICRO_WAKE_WORD
    ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  Creating MicroWakeWord (TFLite Micro Streaming)        ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════════╝");
    
    auto micro_ww = std::make_unique<micro_wake_word::MicroWakeWord>();
    
    // 设置特征步长（必须在 Initialize 之前调用！）
    micro_ww->set_features_step_size(10);  // 10ms step size to match model training
    ESP_LOGI(TAG, "⚙️  Feature step size set to 10ms");
    
    // 初始化（不依赖 models_list）
    if (!micro_ww->Initialize(codec_, nullptr)) {
        ESP_LOGE(TAG, "❌ Failed to initialize MicroWakeWord");
        wake_word_ = nullptr;
    } else {
        ESP_LOGI(TAG, "✅ MicroWakeWord initialized successfully");
        
        // ✅ 配置参数：根据实际测试调整
        // 由于使用 24kHz->16kHz 重采样，阈值需要相应调整
        const float default_threshold = 0.70;  // 默认阈值
        const size_t sliding_window = 5;       // 官方推荐滑动窗口
        const size_t default_tensor_arena = 26080;  // 默认 tensor arena 大小
        
        int model_count = 0;
        
        ESP_LOGI(TAG, "🎯 Loading Wake Word Models (runtime configurable via BLE):");
        ESP_LOGI(TAG, "   - Sample Rate: 24kHz (ES7210 原生)");
        ESP_LOGI(TAG, "   - Resampling: 24kHz → 16kHz (SILK Resampler)");
        ESP_LOGI(TAG, "   - Frontend: ESPHome-aligned (min_signal=0.05)");
        ESP_LOGI(TAG, "   - MIC Input: SLOT0 only (主麦克风) ✅");
        ESP_LOGI(TAG, "   - Sliding Window: %u", (unsigned int)sliding_window);
        
        // ═══════════════════════════════════════════════════════════════
        // Model: Okay Nabu (ESPHome Official v2) - ALWAYS ENABLED
        // ═══════════════════════════════════════════════════════════════
        ESP_LOGI(TAG, "📦 Loading: Okay Nabu (ESPHome Official v2) [ALWAYS ENABLED]");
        ESP_LOGI(TAG, "   - Threshold: %.2f, Tensor Arena: %u bytes", default_threshold, (unsigned int)default_tensor_arena);
        micro_ww->add_wake_word_model(
            okay_nabu_tflite,
            default_threshold,
            sliding_window,
            "okay nabu",
            default_tensor_arena,
            "okay_nabu",      // model_id
            true,             // always_enabled = true (cannot be disabled)
            true              // initial_enabled = true
        );
        model_count++;

        // ═══════════════════════════════════════════════════════════════
        // Model: Hey Ploud - Default enabled
        // ═══════════════════════════════════════════════════════════════
        ESP_LOGI(TAG, "📦 Loading: Hey Ploud [default: enabled]");
        ESP_LOGI(TAG, "   - Threshold: %.2f, Tensor Arena: %u bytes", default_threshold, (unsigned int)default_tensor_arena);
        micro_ww->add_wake_word_model(
            hey_ploud_tflite,
            default_threshold,
            sliding_window,
            "hey ploud",
            default_tensor_arena,
            "hey_ploud",      // model_id
            false,            // always_enabled = false (can be disabled)
            true              // initial_enabled = true (default enabled)
        );
        model_count++;

        // ═══════════════════════════════════════════════════════════════
        // Model: Hey HelloKitty - Default disabled
        // ═══════════════════════════════════════════════════════════════
        ESP_LOGI(TAG, "📦 Loading: Hey HelloKitty [default: disabled]");
        ESP_LOGI(TAG, "   - Threshold: %.2f, Tensor Arena: %u bytes", default_threshold, (unsigned int)default_tensor_arena);
        micro_ww->add_wake_word_model(
            hey_hellokitty_tflite,
            default_threshold,
            sliding_window,
            "hey hellokitty",
            default_tensor_arena,
            "hey_hellokitty",  // model_id
            false,             // always_enabled = false
            false              // initial_enabled = false
        );
        model_count++;

        // ═══════════════════════════════════════════════════════════════
        // Model: Hey IronMan - Default disabled
        // ═══════════════════════════════════════════════════════════════
        ESP_LOGI(TAG, "📦 Loading: Hey IronMan [default: disabled]");
        ESP_LOGI(TAG, "   - Threshold: %.2f, Tensor Arena: %u bytes", default_threshold, (unsigned int)default_tensor_arena);
        micro_ww->add_wake_word_model(
            hey_ironman_tflite,
            default_threshold,
            sliding_window,
            "hey iron man",
            default_tensor_arena,
            "hey_ironman",     // model_id
            false,             // always_enabled = false
            false              // initial_enabled = false
        );
        model_count++;

        // ═══════════════════════════════════════════════════════════════
        // Model: Hey Luigi - Default disabled
        // ═══════════════════════════════════════════════════════════════
        ESP_LOGI(TAG, "📦 Loading: Hey Luigi [default: disabled]");
        ESP_LOGI(TAG, "   - Threshold: %.2f, Tensor Arena: %u bytes", default_threshold, (unsigned int)default_tensor_arena);
        micro_ww->add_wake_word_model(
            hey_luigi_tflite,
            default_threshold,
            sliding_window,
            "hey luigi",
            default_tensor_arena,
            "hey_luigi",       // model_id
            false,             // always_enabled = false
            false              // initial_enabled = false
        );
        model_count++;

        // ═══════════════════════════════════════════════════════════════
        // Model: Hey Stitch - Default disabled
        // ═══════════════════════════════════════════════════════════════
        ESP_LOGI(TAG, "📦 Loading: Hey Stitch [default: disabled]");
        ESP_LOGI(TAG, "   - Threshold: %.2f, Tensor Arena: %u bytes", default_threshold, (unsigned int)default_tensor_arena);
        micro_ww->add_wake_word_model(
            hey_stich_tflite,
            default_threshold,
            sliding_window,
            "hey stitch",
            default_tensor_arena,
            "hey_stitch",      // model_id
            false,             // always_enabled = false
            false              // initial_enabled = false
        );
        model_count++;
        
        ESP_LOGI(TAG, "✅ Registered %d wake word model(s)", model_count);
        
        // 从 NVS 加载保存的模型启用状态
        ESP_LOGI(TAG, "📖 Loading saved model states from NVS...");
        if (micro_ww->load_model_states_from_nvs()) {
            ESP_LOGI(TAG, "✅ Model states restored from NVS");
        } else {
            ESP_LOGI(TAG, "ℹ️  Using default model states (first boot or no saved states)");
        }
        
        ESP_LOGI(TAG, "🎤 Multi wake word models enabled");
        ESP_LOGI(TAG, "   - 'okay_nabu' is always enabled");
        ESP_LOGI(TAG, "   - Model states are automatically saved to NVS");
        
        wake_word_ = std::move(micro_ww);
    }
#elif CONFIG_USE_TFLITE_WAKE_WORD
    ESP_LOGI(TAG, "Creating TFCustomWakeWord (TFLite implementation)");
    wake_word_ = std::make_unique<TFCustomWakeWord>();
#elif CONFIG_USE_CUSTOM_WAKE_WORD
    // MultiNet 唤醒词（CustomWakeWord）
    if (esp_srmodel_filter(models_list_, ESP_MN_PREFIX, NULL) != nullptr) {
        ESP_LOGI(TAG, "Creating CustomWakeWord (MN prefix found)");
        wake_word_ = std::make_unique<CustomWakeWord>();
        ESP_LOGI(TAG, "ℹ️  唤醒词配置将在首次启用检测时从 NVS 自动加载");
    } else {
        ESP_LOGW(TAG, "MultiNet model not found in models list!");
        wake_word_ = nullptr;
    }
#elif CONFIG_USE_AFE_WAKE_WORD
    if (esp_srmodel_filter(models_list_, ESP_WN_PREFIX, NULL) != nullptr) {
        ESP_LOGI(TAG, "Creating AfeWakeWord (WN prefix found)");
        wake_word_ = std::make_unique<AfeWakeWord>();
    } else {
        ESP_LOGW(TAG, "WakeNet model not found in models list!");
        wake_word_ = nullptr;
    }
#else
    // 自动检测模式（向后兼容）
    if (esp_srmodel_filter(models_list_, ESP_MN_PREFIX, NULL) != nullptr) {
        ESP_LOGI(TAG, "Creating CustomWakeWord (MN prefix found, auto-detected)");
        wake_word_ = std::make_unique<CustomWakeWord>();
    } else if (esp_srmodel_filter(models_list_, ESP_WN_PREFIX, NULL) != nullptr) {
        ESP_LOGI(TAG, "Creating AfeWakeWord (WN prefix found, auto-detected)");
        wake_word_ = std::make_unique<AfeWakeWord>();
    } else {
        ESP_LOGW(TAG, "No wake word model found in models list!");
        wake_word_ = nullptr;
    }
#endif

#else  // ESP32, ESP32C3, ESP32C6
    if (esp_srmodel_filter(models_list_, ESP_WN_PREFIX, NULL) != nullptr) {
        ESP_LOGI(TAG, "Creating EspWakeWord (WN prefix found)");
        wake_word_ = std::make_unique<EspWakeWord>();
    } else {
        ESP_LOGW(TAG, "No wake word model found in models list!");
        wake_word_ = nullptr;
    }
#endif

    if (wake_word_) {
        ESP_LOGI(TAG, "Wake word object created successfully");
        wake_word_->OnWakeWordDetected([this](const std::string& wake_word) {
            if (callbacks_.on_wake_word_detected) {
                callbacks_.on_wake_word_detected(wake_word);
            }
        });
    }
}

bool AudioService::IsAfeWakeWord() {
#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32P4
    return wake_word_ != nullptr && dynamic_cast<AfeWakeWord*>(wake_word_.get()) != nullptr;
#else
    return false;
#endif
}
