#include "afe_wake_word.h"
#include "audio_service.h"

#include <esp_log.h>
#include <sstream>

#define DETECTION_RUNNING_EVENT 1

#define TAG "AfeWakeWord"

AfeWakeWord::AfeWakeWord()
    : afe_data_(nullptr),
      wake_word_pcm_(),
      wake_word_opus_() {

    event_group_ = xEventGroupCreate();
}

AfeWakeWord::~AfeWakeWord() {
    if (afe_data_ != nullptr) {
        afe_iface_->destroy(afe_data_);
    }

    if (wake_word_encode_task_stack_ != nullptr) {
        heap_caps_free(wake_word_encode_task_stack_);
    }

    if (wake_word_encode_task_buffer_ != nullptr) {
        heap_caps_free(wake_word_encode_task_buffer_);
    }

    if (models_ != nullptr) {
        esp_srmodel_deinit(models_);
    }

    vEventGroupDelete(event_group_);
}

bool AfeWakeWord::Initialize(AudioCodec* codec, srmodel_list_t* models_list) {
    codec_ = codec;
    int ref_num = codec_->input_reference() ? 1 : 0;

    ESP_LOGI(TAG, "AfeWakeWord::Initialize called, codec=%p, models_list=%p, ref_num=%d", 
             codec, models_list, ref_num);

    if (models_list == nullptr) {
        ESP_LOGI(TAG, "models_list is NULL, calling esp_srmodel_init(\"model\")");
        models_ = esp_srmodel_init("model");
    } else {
        models_ = models_list;
    }

    if (models_ == nullptr || models_->num == -1) {
        ESP_LOGE(TAG, "Failed to initialize wakenet model, models_=%p, num=%d", 
                 models_, models_ ? models_->num : -999);
        return false;
    }
    ESP_LOGI(TAG, "Found %d models in list", models_->num);
    for (int i = 0; i < models_->num; i++) {
        ESP_LOGI(TAG, "  Model %d: %s", i, models_->model_name[i]);
        if (strstr(models_->model_name[i], ESP_WN_PREFIX) != NULL) {
            wakenet_model_ = models_->model_name[i];
            auto words = esp_srmodel_get_wake_words(models_, wakenet_model_);
            ESP_LOGI(TAG, "  -> Wake word model found! Words: %s", words);
            // split by ";" to get all wake words
            std::stringstream ss(words);
            std::string word;
            while (std::getline(ss, word, ';')) {
                wake_words_.push_back(word);
                ESP_LOGI(TAG, "     Added wake word: %s", word.c_str());
            }
        }
    }

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
    ESP_LOGI(TAG, "AFE config: aec_init=%d, aec_mode=%d", afe_config->aec_init, afe_config->aec_mode);
    
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

    xTaskCreate([](void* arg) {
        auto this_ = (AfeWakeWord*)arg;
        this_->AudioDetectionTask();
        vTaskDelete(NULL);
    }, "audio_detection", 4096, this, 3, nullptr);

    ESP_LOGI(TAG, "AfeWakeWord initialization completed successfully!");
    return true;
}

void AfeWakeWord::OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback) {
    wake_word_detected_callback_ = callback;
}

void AfeWakeWord::Start() {
    xEventGroupSetBits(event_group_, DETECTION_RUNNING_EVENT);
}

void AfeWakeWord::Stop() {
    // 清除检测运行标志位
    xEventGroupClearBits(event_group_, DETECTION_RUNNING_EVENT);
    
    // 重置 AFE 缓冲区（幂等操作，可以重复调用）
    if (afe_data_ != nullptr && afe_iface_ != nullptr) {
        afe_iface_->reset_buffer(afe_data_);
    }
}

void AfeWakeWord::Feed(const std::vector<int16_t>& data) {
    if (afe_data_ == nullptr) {
        return;
    }
    
    // 计算音频能量，用于检测麦克风是否工作
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
            ESP_LOGW(TAG, "  ⚠️ Audio level is very low! Microphone may not be working properly!");
        }
    }
    
    afe_iface_->feed(afe_data_, data.data());
}

size_t AfeWakeWord::GetFeedSize() {
    if (afe_data_ == nullptr) {
        return 0;
    }
    return afe_iface_->get_feed_chunksize(afe_data_);
}

void AfeWakeWord::AudioDetectionTask() {
    auto fetch_size = afe_iface_->get_fetch_chunksize(afe_data_);
    auto feed_size = afe_iface_->get_feed_chunksize(afe_data_);
    ESP_LOGI(TAG, "Audio detection task started, feed size: %d fetch size: %d",
        feed_size, fetch_size);

    int loop_count = 0;
    while (true) {
        // 等待检测启用事件，超时时间设置为 portMAX_DELAY 表示一直等待
        xEventGroupWaitBits(event_group_, DETECTION_RUNNING_EVENT, pdFALSE, pdTRUE, portMAX_DELAY);

        // 使用较短的超时时间进行 fetch，这样可以及时响应 Stop() 调用
        auto res = afe_iface_->fetch_with_delay(afe_data_, pdMS_TO_TICKS(100));
        if (res == nullptr || res->ret_value == ESP_FAIL) {
            continue;
        }

        // 每处理 100 次打印一次日志，证明检测任务在运行
        loop_count++;
        if (loop_count % 100 == 0) {
            ESP_LOGI(TAG, "Wake word detection running... (loop %d, wakeup_state=%d)", 
                     loop_count, res->wakeup_state);
        }

        // Store the wake word data for voice recognition, like who is speaking
        StoreWakeWordData(res->data, res->data_size / sizeof(int16_t));

        if (res->wakeup_state == WAKENET_DETECTED) {
            ESP_LOGI(TAG, "*** WAKE WORD DETECTED! *** model_index=%d", res->wakenet_model_index);
            Stop();
            last_detected_wake_word_ = wake_words_[res->wakenet_model_index - 1];
            ESP_LOGI(TAG, "Wake word name: %s", last_detected_wake_word_.c_str());

            if (wake_word_detected_callback_) {
                wake_word_detected_callback_(last_detected_wake_word_);
            } else {
                ESP_LOGW(TAG, "Wake word detected but callback is NULL!");
            }
        }
    }
}

void AfeWakeWord::StoreWakeWordData(const int16_t* data, size_t samples) {
    // store audio data to wake_word_pcm_
    wake_word_pcm_.emplace_back(std::vector<int16_t>(data, data + samples));
    // keep about 2 seconds of data, detect duration is 30ms (sample_rate == 16000, chunksize == 512)
    while (wake_word_pcm_.size() > 2000 / 30) {
        wake_word_pcm_.pop_front();
    }
}

void AfeWakeWord::EncodeWakeWordData() {
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
        auto this_ = (AfeWakeWord*)arg;
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
            ESP_LOGI(TAG, "Encode wake word opus %d packets in %ld ms", packets, (long)((end_time - start_time) / 1000));

            std::lock_guard<std::mutex> lock(this_->wake_word_mutex_);
            this_->wake_word_opus_.push_back(std::vector<uint8_t>());
            this_->wake_word_cv_.notify_all();
        }
        vTaskDelete(NULL);
    }, "encode_wake_word", stack_size, this, 2, wake_word_encode_task_stack_, wake_word_encode_task_buffer_);
}

bool AfeWakeWord::GetWakeWordOpus(std::vector<uint8_t>& opus) {
    std::unique_lock<std::mutex> lock(wake_word_mutex_);
    wake_word_cv_.wait(lock, [this]() {
        return !wake_word_opus_.empty();
    });
    opus.swap(wake_word_opus_.front());
    wake_word_opus_.pop_front();
    return !opus.empty();
}
