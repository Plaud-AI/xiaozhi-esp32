#include "custom_wake_word.h"
#include "audio_service.h"
#include "system_info.h"
#include "assets.h"

#include <esp_log.h>
#include <esp_mn_iface.h>
#include <esp_mn_models.h>
#include <esp_mn_speech_commands.h>
#include <cJSON.h>


#define TAG "CustomWakeWord"


CustomWakeWord::CustomWakeWord()
    : wake_word_pcm_(), wake_word_opus_() {
}

CustomWakeWord::~CustomWakeWord() {
    if (multinet_model_data_ != nullptr && multinet_ != nullptr) {
        multinet_->destroy(multinet_model_data_);
        multinet_model_data_ = nullptr;
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
}

void CustomWakeWord::ParseWakenetModelConfig() {
    // Read index.json
    auto& assets = Assets::GetInstance();
    void* ptr = nullptr;
    size_t size = 0;
    if (!assets.GetAssetData("index.json", ptr, size)) {
        ESP_LOGE(TAG, "Failed to read index.json");
        return;
    }
    cJSON* root = cJSON_ParseWithLength(static_cast<char*>(ptr), size);
    if (root == nullptr) {
        ESP_LOGE(TAG, "Failed to parse index.json");
        return;
    }
    cJSON* multinet_model = cJSON_GetObjectItem(root, "multinet_model");
    if (cJSON_IsObject(multinet_model)) {
        cJSON* language = cJSON_GetObjectItem(multinet_model, "language");
        cJSON* duration = cJSON_GetObjectItem(multinet_model, "duration");
        cJSON* threshold = cJSON_GetObjectItem(multinet_model, "threshold");
        cJSON* commands = cJSON_GetObjectItem(multinet_model, "commands");
        if (cJSON_IsString(language)) {
            language_ = language->valuestring;
        }
        if (cJSON_IsNumber(duration)) {
            duration_ = duration->valueint;
        }
        if (cJSON_IsNumber(threshold)) {
            threshold_ = threshold->valuedouble;
        }
        if (cJSON_IsArray(commands)) {
            for (int i = 0; i < cJSON_GetArraySize(commands); i++) {
                cJSON* command = cJSON_GetArrayItem(commands, i);
                if (cJSON_IsObject(command)) {
                    cJSON* command_name = cJSON_GetObjectItem(command, "command");
                    cJSON* text = cJSON_GetObjectItem(command, "text");
                    cJSON* action = cJSON_GetObjectItem(command, "action");
                    if (cJSON_IsString(command_name) && cJSON_IsString(text) && cJSON_IsString(action)) {
                        commands_.push_back({command_name->valuestring, text->valuestring, action->valuestring});
                        ESP_LOGI(TAG, "Command: %s, Text: %s, Action: %s", command_name->valuestring, text->valuestring, action->valuestring);
                    }
                }
            }
        }
    }
    cJSON_Delete(root);
}


bool CustomWakeWord::Initialize(AudioCodec* codec, srmodel_list_t* models_list) {
    codec_ = codec;
    commands_.clear();

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // MultiNet Only Mode: 直接使用 MultiNet 作为唤醒模型
    // MultiNet6 使用 Grapheme（字形）格式，推荐全大写
    // 参考: https://docs.espressif.com/projects/esp-sr/en/latest/esp32/speech_command_recognition/README.html
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    if (models_list == nullptr) {
        models_ = esp_srmodel_init("model");
        language_ = "en";  // 使用英文模型
        threshold_ = 0.5;  // 推荐阈值 0.5（0.15 太敏感容易误触发）
        duration_ = 5000;  // 超时时间 5 秒
        
        // 添加固定的唤醒词（MultiNet6 格式：全大写标准拼写）
        ESP_LOGI(TAG, "Loading built-in wake words (MultiNet6 format), threshold=%.2f", threshold_);

        // MultiNet6 正确格式：全大写字母，标准英文拼写
        commands_.push_back({"HI PLAUD", "hi plaud", "wake"});
        commands_.push_back({"HELLO PLAUD", "hello plaud", "wake"});
        commands_.push_back({"HEY PLAUD", "hey plaud", "wake"});
        
        commands_.push_back({"HI NICEBUILD", "hi nicebuild", "wake"});
        commands_.push_back({"HELLO NICEBUILD", "hello nicebuild", "wake"});
        commands_.push_back({"HEY NICEBUILD", "hey nicebuild", "wake"});

    } else {
        models_ = models_list;
        // MultiNet Only 模式：始终使用代码中定义的默认唤醒词
        // 不从 assets 读取，确保行为一致
        ESP_LOGI(TAG, "Using built-in wake words (ignoring assets config)");
        language_ = "en";
        threshold_ = 0.5;  // 推荐阈值
        duration_ = 5000;
        
        // MultiNet6 正确格式：全大写字母，标准英文拼写
        commands_.push_back({"HI PLAUD", "hi plaud", "wake"});
        commands_.push_back({"HELLO PLAUD", "hello plaud", "wake"});
        commands_.push_back({"HEY PLAUD", "hey plaud", "wake"});
        
        commands_.push_back({"HI NICEBUILD", "hi nicebuild", "wake"});
        commands_.push_back({"HELLO NICEBUILD", "hello nicebuild", "wake"});
        commands_.push_back({"HEY NICEBUILD", "hey nicebuild", "wake"});
    }

    if (models_ == nullptr || models_->num == -1) {
        ESP_LOGE(TAG, "Failed to initialize model");
        return false;
    }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // 初始化 MultiNet（命令词识别，作为唤醒模型）
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    mn_name_ = esp_srmodel_filter(models_, ESP_MN_PREFIX, language_.c_str());
    if (mn_name_ == nullptr) {
        ESP_LOGE(TAG, "Failed to find MultiNet model for language: %s", language_.c_str());
        ESP_LOGE(TAG, "Please ensure MultiNet model is selected in sdkconfig");
        ESP_LOGI(TAG, "For English: CONFIG_SR_MN5Q8_EN=y");
        ESP_LOGI(TAG, "For Chinese: CONFIG_SR_MN_CN=y");
        return false;
    }

    ESP_LOGI(TAG, "Found MultiNet model: %s (language: %s)", mn_name_, language_.c_str());

    multinet_ = esp_mn_handle_from_name(mn_name_);
    if (multinet_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create MultiNet handle");
        return false;
    }

    // 创建 MultiNet 模型数据
    multinet_model_data_ = multinet_->create(mn_name_, duration_);
    if (multinet_model_data_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create MultiNet model data");
        return false;
    }
    
    // 设置检测阈值
    multinet_->set_det_threshold(multinet_model_data_, threshold_);
    ESP_LOGI(TAG, "MultiNet threshold set to: %.2f", threshold_);
    
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // 添加唤醒命令（MultiNet6 使用 Grapheme 格式）
    // MultiNet6 格式：全大写标准英文拼写，无需音素转换
    // 参考: https://docs.espressif.com/projects/esp-sr/en/latest/esp32/speech_command_recognition/README.html#multinet6-customize-speech-commands
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    esp_mn_commands_clear();
    
    ESP_LOGI(TAG, "Adding %d wake word commands (MultiNet6 format):", commands_.size());
    for (int i = 0; i < commands_.size(); i++) {
        ESP_LOGI(TAG, "  [%d] command=\"%s\", text=\"%s\", action=\"%s\"", 
                 i + 1,  // MultiNet command ID 从 1 开始（0 保留）
                 commands_[i].command.c_str(),
                 commands_[i].text.c_str(), 
                 commands_[i].action.c_str());
        
        // MultiNet6: 添加命令（Grapheme 格式，全大写）
        // 注意: command ID 从 1 开始（0 保留给系统）
        esp_mn_commands_add(i + 1, commands_[i].command.c_str());
    }
    
    // 更新命令到模型
    esp_mn_error_t* err = esp_mn_commands_update();
    if (err) {
        ESP_LOGE(TAG, "Failed to update commands, %d errors:", err->num);
        for (int i = 0; i < err->num; i++) {
            ESP_LOGE(TAG, "  Error command ID: %d, content: %s", 
                     err->phrases[i]->command_id, 
                     err->phrases[i]->string);
        }
        // 错误结构会自动清理，不需要手动释放
        return false;
    }

    // 打印已添加的命令
    ESP_LOGI(TAG, "✓ Successfully added %d commands to MultiNet6", commands_.size());
    esp_mn_commands_print();
    
    ESP_LOGI(TAG, "CustomWakeWord initialized successfully (MultiNet only mode)");
    return true;
}

void CustomWakeWord::OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback) {
    wake_word_detected_callback_ = callback;
}

void CustomWakeWord::Start() {
    running_ = true;
}

void CustomWakeWord::Stop() {
    running_ = false;
}

void CustomWakeWord::Feed(const std::vector<int16_t>& data) {
    if (multinet_model_data_ == nullptr || !running_) {
        return;
    }

    // 添加调试日志，证明 Feed 被调用
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
        ESP_LOGI(TAG, "CustomWakeWord Feed (count %d): avg=%d, max=%d, samples=%d", 
                 feed_count, avg, max_val, data.size());
    }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // MultiNet Only Mode: 直接使用 MultiNet 检测（不依赖 WakeNet）
    // 参考: esp-sr-multinet/main/blink_example_main.c: 114-143
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    esp_mn_state_t mn_state;
    
    // 处理双通道音频（如果是）
    if (codec_->input_channels() == 2) {
        // 提取左声道
        auto mono_data = std::vector<int16_t>(data.size() / 2);
        for (size_t i = 0, j = 0; i < mono_data.size(); ++i, j += 2) {
            mono_data[i] = data[j];
        }
        
        StoreWakeWordData(mono_data);
        mn_state = multinet_->detect(multinet_model_data_, 
                                     const_cast<int16_t*>(mono_data.data()));
    } else {
        StoreWakeWordData(data);
        mn_state = multinet_->detect(multinet_model_data_, 
                                     const_cast<int16_t*>(data.data()));
    }
    
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // 处理检测结果
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    
    // 调试：每 50 次 Feed 显示一次状态
    static int state_count = 0;
    if (++state_count % 50 == 0) {
        ESP_LOGD(TAG, "MultiNet state: %d (0=detecting, 1=detected, 2=timeout)", mn_state);
    }
    
    if (mn_state == ESP_MN_STATE_DETECTING) {
        // 正在检测中，无需处理
        return;
    } 
    else if (mn_state == ESP_MN_STATE_DETECTED) {
        // ✓ 检测到命令！
        esp_mn_results_t* mn_result = multinet_->get_results(multinet_model_data_);
        
        if (mn_result != NULL && mn_result->num > 0) {
            // 获取第一个检测到的命令 ID
            int command_id = mn_result->phrase_id[0];
            
            // 显示所有命令的概率（调试用）
            ESP_LOGI(TAG, "✓ MultiNet detection result:");
            for (int i = 0; i < commands_.size() && i < 10; i++) {
                ESP_LOGI(TAG, "  Command %d (%s): prob=%.2f %s", 
                         i, commands_[i].text.c_str(), 
                         mn_result->prob[i],
                         (i == command_id) ? "← BEST" : "");
            }
            
            ESP_LOGI(TAG, "✓ Detected command ID: %d, prob: %.2f", 
                     command_id, mn_result->prob[command_id]);
            
            // 检查命令 ID 是否有效
            // 注意：MultiNet command ID 从 1 开始，我们的数组从 0 开始
            int array_index = command_id - 1;
            if (array_index >= 0 && array_index < commands_.size()) {
                auto& command = commands_[array_index];
                
                ESP_LOGI(TAG, "  Command: %s, Text: %s, Action: %s",
                         command.command.c_str(),
                         command.text.c_str(),
                         command.action.c_str());
                
                // 只响应 "wake" 动作的命令
                if (command.action == "wake") {
                    last_detected_wake_word_ = command.text;  // 例如: "hi plaud"
                    running_ = false;  // 停止检测
                    
                    ESP_LOGI(TAG, "✓ Wake word detected: %s", last_detected_wake_word_.c_str());
                    
                    // 触发回调
                    if (wake_word_detected_callback_) {
                        wake_word_detected_callback_(last_detected_wake_word_);
                    }
                }
            } else {
                ESP_LOGW(TAG, "Invalid command ID: %d (array index: %d, total commands: %d)", 
                         command_id, array_index, commands_.size());
            }
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

size_t CustomWakeWord::GetFeedSize() {
    if (multinet_model_data_ == nullptr) {
        return 0;
    }
    return multinet_->get_samp_chunksize(multinet_model_data_);
}

void CustomWakeWord::StoreWakeWordData(const std::vector<int16_t>& data) {
    // Store PCM data for wake word encoding
    wake_word_pcm_.push_back(data);
    
    // Keep only the last 2 seconds (16000 Hz * 2 / 480 ≈ 66 frames)
    while (wake_word_pcm_.size() > 66) {
        wake_word_pcm_.pop_front();
    }
}

void CustomWakeWord::EncodeWakeWordData() {
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
    xTaskCreateStatic([](void* arg) {
        auto this_ = (CustomWakeWord*)arg;
        OpusEncoderWrapper encoder(16000, 1, 60);
        
        // Opus 编码器期望 960 samples (60ms @ 16kHz)
        // CustomWakeWord 每个块是 512 samples (32ms @ 16kHz)
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
        
        vTaskDelete(NULL);
    }, "encode_wake_word", stack_size, this, 3, wake_word_encode_task_stack_, wake_word_encode_task_buffer_);
}

bool CustomWakeWord::GetWakeWordOpus(std::vector<uint8_t>& opus) {
    if (wake_word_opus_.empty()) {
        return false;
    }
    opus = std::move(wake_word_opus_.front());
    wake_word_opus_.pop_front();
    return true;
}

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// 动态命令管理接口实现
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

void CustomWakeWord::ClearCommands() {
    commands_.clear();
    ESP_LOGI(TAG, "Cleared all wake word commands");
}

void CustomWakeWord::AddCommand(const std::string& phoneme, 
                                const std::string& text, 
                                const std::string& action) {
    commands_.push_back({phoneme, text, action});
    ESP_LOGI(TAG, "Added command: phoneme='%s', text='%s', action='%s'", 
             phoneme.c_str(), text.c_str(), action.c_str());
}

void CustomWakeWord::SetThreshold(float threshold) {
    threshold_ = threshold;
    ESP_LOGI(TAG, "Set detection threshold to %.2f", threshold_);
    
    // 阈值可以立即更新（运行时生效）
    if (multinet_ != nullptr && multinet_model_data_ != nullptr) {
        multinet_->set_det_threshold(multinet_model_data_, threshold_);
        ESP_LOGI(TAG, "✓ Threshold applied to MultiNet immediately");
    }
}

bool CustomWakeWord::UpdateCommands() {
    if (multinet_model_data_ == nullptr) {
        ESP_LOGW(TAG, "MultiNet not initialized, cannot update commands");
        return false;
    }
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Updating commands to MultiNet at runtime...");
    ESP_LOGI(TAG, "========================================");
    
    // 1. 清空现有命令
    esp_mn_commands_clear();
    ESP_LOGI(TAG, "Cleared existing MultiNet commands");
    
    // 2. 添加新命令
    ESP_LOGI(TAG, "Adding %d wake word commands:", commands_.size());
    for (int i = 0; i < commands_.size(); i++) {
        ESP_LOGI(TAG, "  [%d] command=\"%s\", text=\"%s\", action=\"%s\"", 
                 i + 1,  // MultiNet command ID 从 1 开始
                 commands_[i].command.c_str(),
                 commands_[i].text.c_str(), 
                 commands_[i].action.c_str());
        
        // MultiNet command ID 从 1 开始
        esp_mn_commands_add(i + 1, commands_[i].command.c_str());
    }
    
    // 3. 更新命令到模型（运行时生效！）
    esp_mn_error_t* err = esp_mn_commands_update();
    if (err) {
        ESP_LOGE(TAG, "Failed to update commands, %d errors:", err->num);
        for (int i = 0; i < err->num; i++) {
            ESP_LOGE(TAG, "  Error command ID: %d, content: %s", 
                     err->phrases[i]->command_id, 
                     err->phrases[i]->string);
        }
        return false;
    }
    
    // 4. 打印已更新的命令
    ESP_LOGI(TAG, "✓ Successfully updated %d commands to MultiNet at runtime!", commands_.size());
    esp_mn_commands_print();
    ESP_LOGI(TAG, "========================================");
    
    return true;
}

