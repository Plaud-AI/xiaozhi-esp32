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
    event_group_ = xEventGroupCreate();
}

CustomWakeWord::~CustomWakeWord() {
    // 清理 AFE
    if (afe_data_ != nullptr && afe_iface_ != nullptr) {
        afe_iface_->destroy(afe_data_);
        afe_data_ = nullptr;
    }
    
    if (event_group_ != nullptr) {
        vEventGroupDelete(event_group_);
    }
    
    // 清理 MultiNet
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
            app_threshold_ = threshold->valuedouble;  // 从配置读取应用层阈值
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
        ESP_LOGI(TAG, "CustomWakeWord::Initialize1");
        models_ = esp_srmodel_init("model");
        language_ = "en";  // 使用英文模型
        multinet_threshold_ = 0.05;  // MultiNet 内部阈值（低阈值让它总是返回结果）
        app_threshold_ = 0.30;  // 应用层阈值（降低适应非标准发音，标准词可达0.6-0.8）
        duration_ = 5000;  // 超时时间 5 秒

        /*
        // 添加固定的唤醒词（MultiNet6 格式：全大写标准拼写）
        ESP_LOGI(TAG, "Loading built-in wake words (MultiNet6 format)");
        ESP_LOGI(TAG, "  MultiNet threshold=%.2f (internal, low to get all results)", multinet_threshold_);
        ESP_LOGI(TAG, "  App threshold=%.2f (application layer filtering)", app_threshold_);

        // MultiNet6 正确格式：全大写字母，标准英文拼写
        commands_.push_back({"HI PLAUD", "hi plaud", "wake"});
        commands_.push_back({"HELLO PLAUD", "hello plaud", "wake"});
        commands_.push_back({"HEY PLAUD", "hey plaud", "wake"});
     */

        // ⚠️ 避免使用太多 "HI + 名字" 的相似模式，会导致混淆
        // ⚠️ Tom/Jack/Lily/Lucy 音素相似，MultiNet6 很难区分
        
        // ✅ 推荐：标准英文唤醒词（音素清晰，好发音，不易误触发）
        commands_.push_back({"COMPUTER", "computer", "wake"});        // 单词：常用词，3音节
        commands_.push_back({"HELLO FRIEND", "hello friend", "wake"}); // 双词：HELLO + 朋友
        
        // 备选唤醒词（可根据需要启用）
        // commands_.push_back({"ASSISTANT", "assistant", "wake"});   // 单词：助手
        // commands_.push_back({"HELLO ROBOT", "hello robot", "wake"}); // HELLO + 机器人
        // commands_.push_back({"HELLO SYSTEM", "hello system", "wake"}); // HELLO + 系统

    } else {
        ESP_LOGI(TAG, "CustomWakeWord::Initialize2  hit!!!!");
        models_ = models_list;
        // MultiNet Only 模式：始终使用代码中定义的默认唤醒词
        // 不从 assets 读取，确保行为一致
        ESP_LOGI(TAG, "Using built-in wake words (ignoring assets config)");
        language_ = "en";
        multinet_threshold_ = 0.05;  // MultiNet 内部阈值（低阈值让它总是返回结果）
        app_threshold_ = 0.30;  // 应用层阈值（机器标准发音测试：0.301-0.576，平均0.43）
        duration_ = 5000;
       
        // ⚠️ 避免使用太多 "HI + 名字" 的相似模式，会导致混淆
        // ⚠️ Tom/Jack/Lily/Lucy 音素相似，MultiNet6 很难区分
        
        // ✅ 推荐：标准英文唤醒词（音素清晰，好发音，不易误触发）
        //commands_.push_back({"COMPUTER", "computer", "wake"});        // 单词：常用词，3音节
        //commands_.push_back({"HELLO FRIEND", "hello friend", "wake"}); // 双词：HELLO + 朋友

        // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        // ⚠️ 重要：唤醒词数量的权衡
        // - 1 个词：噪声概率高（0.2-0.3），容易误触发 ❌
        // - 2-3 个词：平衡点，噪声概率降低（0.1-0.15），真实语音仍可识别 ✅
        // - 5+ 个词：噪声概率很低（0.05-0.08），但真实语音概率也降低（0.2-0.3）❌
        // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        
        commands_.push_back({"HI COMPUTER", "hi computer", "wake"});     // 主唤醒词
        commands_.push_back({"HELLO ASSISTANT", "hello assistant", "wake"}); // 备用
        commands_.push_back({"HEY DEVICE", "hey device", "wake"});       // 备用（用于测试）
        
        // 更多备选（如果需要进一步降低误触发率，可启用更多词）
        //commands_.push_back({"WAKE UP", "wake up", "wake"});
        //commands_.push_back({"OK READY", "ok ready", "wake"});  
        
        // 备选唤醒词（可根据需要启用）
        // commands_.push_back({"ASSISTANT", "assistant", "wake"});   // 单词：助手
        // commands_.push_back({"HELLO ROBOT", "hello robot", "wake"}); // HELLO + 机器人
        // commands_.push_back({"HELLO SYSTEM", "hello system", "wake"}); // HELLO + 系统
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

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Found MultiNet model: %s (language: %s)", mn_name_, language_.c_str());
    ESP_LOGI(TAG, "========================================");
    
    // 确认使用的模型版本
    if (strstr(mn_name_, "mn6") != nullptr) {
        ESP_LOGI(TAG, "✓ Confirmed using MultiNet6 - Grapheme format required");
    } else if (strstr(mn_name_, "mn5") != nullptr) {
        ESP_LOGW(TAG, "⚠️ Using MultiNet5 - Phoneme format required, but code is configured for MultiNet6!");
        ESP_LOGW(TAG, "⚠️ Please update sdkconfig to use CONFIG_SR_MN_EN_MULTINET6_QUANT=y");
    } else if (strstr(mn_name_, "mn7") != nullptr) {
        ESP_LOGI(TAG, "✓ Using MultiNet7 - Grapheme format with optional phoneme column");
    } else {
        ESP_LOGW(TAG, "⚠️ Unknown MultiNet version: %s", mn_name_);
    }

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
    
    // 获取 MultiNet 参数信息
    int chunk_size = multinet_->get_samp_chunksize(multinet_model_data_);
    int sample_rate = multinet_->get_samp_rate(multinet_model_data_);
    ESP_LOGI(TAG, "MultiNet parameters: chunk_size=%d, sample_rate=%d", chunk_size, sample_rate);
    
    // 设置 MultiNet 内部检测阈值（设置得很低，让它总是返回结果）
    multinet_->set_det_threshold(multinet_model_data_, multinet_threshold_);
    ESP_LOGI(TAG, "MultiNet internal threshold set to: %.2f", multinet_threshold_);
    ESP_LOGI(TAG, "Application layer threshold: %.2f (for filtering)", app_threshold_);
    
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
    ESP_LOGI(TAG, "Calling esp_mn_commands_update()...");
    esp_mn_error_t* err = esp_mn_commands_update();
    if (err) {
        ESP_LOGE(TAG, "❌ Failed to update commands, %d errors:", err->num);
        for (int i = 0; i < err->num; i++) {
            ESP_LOGE(TAG, "  Error command ID: %d, content: '%s'", 
                     err->phrases[i]->command_id, 
                     err->phrases[i]->string);
        }
        // 错误结构会自动清理，不需要手动释放
        return false;
    }

    // 打印已添加的命令
    ESP_LOGI(TAG, "✓ Successfully added %d commands to MultiNet6", commands_.size());
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Active MultiNet6 commands:");
    esp_mn_commands_print();
    ESP_LOGI(TAG, "========================================");
    
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // 初始化 AFE (Audio Front-End) 用于降噪、波束成形、AEC
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    if (use_afe_) {
        ESP_LOGI(TAG, "Initializing AFE for audio preprocessing...");
        
        int ref_num = codec_->input_reference() ? 1 : 0;
        ESP_LOGI(TAG, "AFE config: input_channels=%d, ref_num=%d, input_reference=%d",
                 codec_->input_channels(), ref_num, codec_->input_reference());
        
        // 构建输入格式字符串：M=麦克风，R=回放参考
        std::string input_format;
        for (int i = 0; i < codec_->input_channels() - ref_num; i++) {
            input_format.push_back('M');
        }
        for (int i = 0; i < ref_num; i++) {
            input_format.push_back('R');
        }
        ESP_LOGI(TAG, "AFE input format: \"%s\"", input_format.c_str());
        
        // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        // AFE 配置：不传入 WakeNet，只配置 NS（降噪）
        // CustomWakeWord 使用 MultiNet，不需要 WakeNet
        // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        
        // 先从 models_ 中过滤出 NS 模型
        char* ns_model_name = esp_srmodel_filter(models_, ESP_NSNET_PREFIX, NULL);
        
        // 创建 AFE 配置（不传入 models_，避免加载 WakeNet）
        afe_config_t* afe_config = afe_config_init(input_format.c_str(), NULL, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
        if (afe_config == nullptr) {
            ESP_LOGE(TAG, "❌ Failed to init AFE config!");
            return false;
        }
        
        // 配置 AFE 参数
        afe_config->aec_init = codec_->input_reference();  // 如果有回放参考，启用 AEC
        afe_config->aec_mode = AEC_MODE_SR_HIGH_PERF;
        afe_config->vad_init = false;  // VAD 已禁用（节省 1-2% CPU + 20KB RAM）

        afe_config->wakenet_init = false;
        
        // 手动配置 NS（降噪）
        if (ns_model_name != nullptr) {
            ESP_LOGI(TAG, "✓ Found NSNet model: %s", ns_model_name);
            afe_config->ns_init = true;
            afe_config->ns_model_name = ns_model_name;
            afe_config->afe_ns_mode = AFE_NS_MODE_NET;  // 使用神经网络降噪（NSNet2）
            ESP_LOGI(TAG, "✓ NSNet2 降噪已启用 (强力降噪)");
        } else {
            // ❌ 没有 NSNet 模型，禁用 NS（WebRTC NS 可能导致崩溃）
            ESP_LOGW(TAG, "NSNet model not found, NS disabled");
            ESP_LOGW(TAG, "  → 降噪效果会降低，建议重新编译以包含 NSNet2");
            afe_config->ns_init = false;
            afe_config->ns_model_name = nullptr;
        }
        
        afe_config->agc_init = false;  // 不启用 AGC（音频增益已在 codec 层设置）
        afe_config->afe_perferred_core = 1;
        afe_config->afe_perferred_priority = 1;
        afe_config->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
        
        ESP_LOGI(TAG, "AFE features: AEC=%d, NS=%d", 
                 afe_config->aec_init, afe_config->ns_init);
        
        // 创建 AFE 接口
        afe_iface_ = esp_afe_handle_from_config(afe_config);
        if (afe_iface_ == nullptr) {
            ESP_LOGE(TAG, "❌ Failed to get AFE interface handle!");
            return false;
        }
        
        // 创建 AFE 数据实例
        afe_data_ = afe_iface_->create_from_config(afe_config);
        if (afe_data_ == nullptr) {
            ESP_LOGE(TAG, "❌ Failed to create AFE data from config!");
            return false;
        }
        
        ESP_LOGI(TAG, "✓ AFE initialized: feed_size=%d, fetch_size=%d", 
                 afe_iface_->get_feed_chunksize(afe_data_),
                 afe_iface_->get_fetch_chunksize(afe_data_));
        
        // 创建音频检测任务（AFE + MultiNet）
        // 使用 xTaskCreatePinnedToCore 指定核心 1（AFE 也在核心 1）
        BaseType_t task_created = xTaskCreatePinnedToCore(
            [](void* arg) {
                auto this_ = (CustomWakeWord*)arg;
                this_->AudioDetectionTask();
                vTaskDelete(NULL);
            }, 
            "wake_det",     // 任务名（缩短以节省内存）
            4096,           // 栈大小 4KB（足够了，AFE fetch 不需要太多栈）
            this,           // 参数
            5,              // 优先级（与 AFE 任务相同）
            nullptr,        // 任务句柄
            1               // 核心 1（与 AFE 在同一核心）
        );
        
        if (task_created != pdPASS) {
            ESP_LOGE(TAG, "❌ Failed to create audio detection task! (out of memory?)");
            ESP_LOGI(TAG, "Available heap: %d bytes", esp_get_free_heap_size());
            return false;
        }
        ESP_LOGI(TAG, "✓ Audio detection task created on core 1");
    } else {
        ESP_LOGI(TAG, "AFE disabled, using raw audio input");
    }
    
    ESP_LOGI(TAG, "CustomWakeWord initialized successfully");
    return true;
}

void CustomWakeWord::OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback) {
    wake_word_detected_callback_ = callback;
}

void CustomWakeWord::Start() {
    running_ = true;
    if (use_afe_ && event_group_ != nullptr) {
        xEventGroupSetBits(event_group_, 0x01);  // 启动 AFE 检测任务
    }
}

void CustomWakeWord::Stop() {
    running_ = false;
    if (use_afe_ && event_group_ != nullptr) {
        xEventGroupClearBits(event_group_, 0x01);  // 停止 AFE 检测任务
        if (afe_data_ != nullptr && afe_iface_ != nullptr) {
            afe_iface_->reset_buffer(afe_data_);  // 重置 AFE 缓冲区
        }
    }
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
    // 如果启用了 AFE，将数据送入 AFE 进行预处理
    // AFE 会在后台任务中处理，然后送给 MultiNet
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    if (use_afe_ && afe_data_ != nullptr && afe_iface_ != nullptr) {
        afe_iface_->feed(afe_data_, data.data());
        return;  // AFE 模式下，不直接调用 MultiNet
    }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // 非 AFE 模式：直接使用 MultiNet 检测（原有逻辑）
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
    
    // 调试：每 100 次 Feed 显示一次检测状态
    static int state_count = 0;
    if (++state_count % 100 == 0) {
        ESP_LOGI(TAG, "MultiNet state: %d (0=DETECTING, 1=DETECTED, 2=TIMEOUT), running=%d", 
                 mn_state, running_ ? 1 : 0);
    }
    
    // 重要：记录非 DETECTING 状态
    if (mn_state != ESP_MN_STATE_DETECTING) {
        ESP_LOGI(TAG, "⚠️ MultiNet state changed to: %d (0=DETECTING, 1=DETECTED, 2=TIMEOUT)", 
                 mn_state);
    }
    
    if (mn_state == ESP_MN_STATE_DETECTING) {
        // 正在检测中，无需处理
        return;
    } 
    else if (mn_state == ESP_MN_STATE_DETECTED) {
        // ✓ MultiNet 返回了检测结果（但可能低于应用层阈值）
        esp_mn_results_t* mn_result = multinet_->get_results(multinet_model_data_);
        
        if (mn_result != NULL && mn_result->num > 0) {
            // 获取第一个检测到的命令 ID（概率最高的）
            int command_id = mn_result->phrase_id[0];
            float best_prob = mn_result->prob[0];  // prob[0] 对应 phrase_id[0]
            
            // 总是显示所有检测结果（方便调试阈值）
            ESP_LOGI(TAG, "begin━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
            ESP_LOGI(TAG, "📊 MultiNet detection result (num=%d):", mn_result->num);
            for (int i = 0; i < mn_result->num && i < 5; i++) {
                int result_cmd_id = mn_result->phrase_id[i];
                float result_prob = mn_result->prob[i];
                int result_array_index = result_cmd_id - 1;
                
                if (result_array_index >= 0 && result_array_index < commands_.size()) {
                    ESP_LOGI(TAG, "  [%d] ID=%d (%s): prob=%.3f %s", 
                             i, result_cmd_id, 
                             commands_[result_array_index].text.c_str(),
                             result_prob,
                             (i == 0) ? "← BEST" : "");
                } else {
                    ESP_LOGI(TAG, "  [%d] ID=%d (unknown): prob=%.3f", 
                             i, result_cmd_id, result_prob);
                }
            }
            ESP_LOGI(TAG, "end━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");

            
            // 应用层阈值过滤
            if (best_prob < app_threshold_) {
                ESP_LOGW(TAG, "⚠️  Probability %.3f < threshold %.2f, ignoring detection", 
                         best_prob, app_threshold_);
                ESP_LOGI(TAG, " 应用层过滤结束━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
                multinet_->clean(multinet_model_data_);
                return;
            }
            
            ESP_LOGI(TAG, "✓ Best match - Command ID: %d, Probability: %.3f (>= %.2f)", 
                     command_id, best_prob, app_threshold_);
            
            // 检查命令 ID 是否有效
            // 注意：MultiNet command ID 从 1 开始，我们的数组从 0 开始
            int array_index = command_id - 1;
            if (array_index >= 0 && array_index < commands_.size()) {
                auto& command = commands_[array_index];
                
                ESP_LOGI(TAG, "  → Command: \"%s\", Text: \"%s\", Action: \"%s\"",
                         command.command.c_str(),
                         command.text.c_str(),
                         command.action.c_str());
                
                // 只响应 "wake" 动作的命令
                if (command.action == "wake") {
                    last_detected_wake_word_ = command.text;  // 例如: "hi plaud"
                    running_ = false;  // 停止检测
                    
                    ESP_LOGI(TAG, "✓✓✓ Wake word detected: \"%s\" (probability: %.3f) ✓✓✓", 
                             last_detected_wake_word_.c_str(), best_prob);
                    ESP_LOGI(TAG, "唤醒命中━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
                    
                    // 触发回调
                    if (wake_word_detected_callback_) {
                        wake_word_detected_callback_(last_detected_wake_word_);
                    }
                }
            } else {
                ESP_LOGW(TAG, "Invalid command ID: %d (array index: %d, total commands: %d)", 
                         command_id, array_index, commands_.size());
            }
        } else {
            ESP_LOGW(TAG, "MultiNet detected but no results (mn_result=%p, num=%d)", 
                     mn_result, mn_result ? mn_result->num : -1);
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
    // 如果启用了 AFE，返回 AFE 的 feed size
    if (use_afe_ && afe_data_ != nullptr && afe_iface_ != nullptr) {
        return afe_iface_->get_feed_chunksize(afe_data_);
    }
    
    // 否则返回 MultiNet 的 chunk size
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
    app_threshold_ = threshold;  // 设置应用层阈值
    ESP_LOGI(TAG, "Set application layer threshold to %.2f", app_threshold_);
    ESP_LOGI(TAG, "Note: MultiNet internal threshold remains at %.2f (low value to get all results)", multinet_threshold_);
    
    // 应用层阈值不需要更新到 MultiNet（我们在应用层过滤）
    // MultiNet 内部阈值保持低值以获取所有检测结果
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



// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// AudioDetectionTask: AFE + MultiNet 集成检测任务
// 从 AFE 获取处理后的干净音频，然后送给 MultiNet 进行唤醒词检测
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
void CustomWakeWord::AudioDetectionTask() {
    ESP_LOGI(TAG, "AudioDetectionTask started (AFE + MultiNet mode)");
    
    if (afe_iface_ == nullptr || afe_data_ == nullptr) {
        ESP_LOGE(TAG, "AFE not initialized, task exiting!");
        return;
    }
    
    auto fetch_size = afe_iface_->get_fetch_chunksize(afe_data_);
    auto feed_size = afe_iface_->get_feed_chunksize(afe_data_);
    ESP_LOGI(TAG, "AFE parameters: feed_size=%d, fetch_size=%d", feed_size, fetch_size);
    
    int loop_count = 0;
    while (true) {
        // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        // 等待检测启用信号（避免在未启用时持续 fetch 导致 ringbuffer empty）
        // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        EventBits_t bits = xEventGroupWaitBits(
            event_group_, 
            0x01,          // 等待 bit 0
            pdFALSE,       // 不清除 bit
            pdTRUE,        // 所有 bit 都满足
            portMAX_DELAY  // 无限等待
        );
        
        // 检查是否真正启用（double check）
        if (!running_.load() || (bits & 0x01) == 0) {
            vTaskDelay(pdMS_TO_TICKS(100));  // 短暂延迟后重试
            continue;
        }
        
        // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        // 从 AFE 获取处理后的音频
        // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        afe_fetch_result_t* res = afe_iface_->fetch(afe_data_);
        if (res == nullptr || res->ret_value == ESP_FAIL) {
            ESP_LOGW(TAG, "AFE fetch failed (ringbuffer empty?), pausing detection...");
            // AFE 数据不可用，清除 event bit 并重新等待
            xEventGroupClearBits(event_group_, 0x01);
            continue;
        }
        
        // 每 100 次循环打印一次状态（调试用）
        loop_count++;
        if (loop_count % 100 == 0) {
            bool is_running = running_.load();
            ESP_LOGI(TAG, "AFE fetch loop %d: data_size=%d, running=%d",
                     loop_count, res->data_size, is_running);
        }
        
        // 再次检查是否启用（可能在 fetch 期间被停止）
        if (!running_.load()) {
            continue;  // 丢弃数据并返回等待
        }
        
        // 存储唤醒词数据（用于后续编码发送）
        if (res->data && res->data_size > 0) {
            std::vector<int16_t> audio_data(res->data, res->data + res->data_size / sizeof(int16_t));
            StoreWakeWordData(audio_data);
        }
        
        // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        // 将 AFE 处理后的音频送给 MultiNet 检测
        // 注意：不使用 VAD 过滤！
        // 原因：VAD 误判会导致唤醒词被丢弃，造成严重延迟或无法唤醒
        // 降低误触发的策略：依赖多唤醒词 + 阈值过滤
        // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        if (multinet_ == nullptr || multinet_model_data_ == nullptr) {
            continue;
        }
        
        esp_mn_state_t mn_state = multinet_->detect(multinet_model_data_, res->data);
        
        // 调试：每 100 次显示一次状态
        static int state_count = 0;
        if (++state_count % 100 == 0) {
            ESP_LOGI(TAG, "MultiNet state: %d (0=DETECTING, 1=DETECTED, 2=TIMEOUT), running=%d",
                     mn_state, running_ ? 1 : 0);
        }
        
        if (mn_state != ESP_MN_STATE_DETECTING) {
            ESP_LOGI(TAG, "⚠️ MultiNet state changed to: %d (0=DETECTING, 1=DETECTED, 2=TIMEOUT)",
                     mn_state);
        }
        
        if (mn_state == ESP_MN_STATE_DETECTING) {
            continue;  // 正在检测中
        }
        else if (mn_state == ESP_MN_STATE_DETECTED) {
            // ✓ MultiNet 检测到命令
            esp_mn_results_t* mn_result = multinet_->get_results(multinet_model_data_);
            
            if (mn_result != NULL && mn_result->num > 0) {
                int command_id = mn_result->phrase_id[0];
                float best_prob = mn_result->prob[0];
                
                // 显示所有检测结果
                ESP_LOGI(TAG, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
                ESP_LOGI(TAG, "📊 MultiNet result: num=%d, total_commands=%d", 
                         mn_result->num, commands_.size());
                
                for (int i = 0; i < mn_result->num && i < 5; i++) {
                    int result_cmd_id = mn_result->phrase_id[i];
                    float result_prob = mn_result->prob[i];
                    int result_array_index = result_cmd_id - 1;
                    
                    if (result_array_index >= 0 && result_array_index < commands_.size()) {
                        ESP_LOGI(TAG, "  [%d] ID=%d (%s): prob=%.3f %s",
                                 i, result_cmd_id,
                                 commands_[result_array_index].text.c_str(),
                                 result_prob,
                                 (i == 0) ? "← BEST" : "");
                    }
                }
                
                // 警告：如果返回结果少于注册词，说明其他词概率太低被 MultiNet 过滤
                if (mn_result->num < commands_.size()) {
                    ESP_LOGW(TAG, "  ⚠️ %d 个唤醒词被 MultiNet 过滤（概率太低 < MultiNet内部阈值）", 
                             commands_.size() - mn_result->num);
                }
                
                // 应用层阈值过滤
                if (best_prob < app_threshold_) {
                    ESP_LOGW(TAG, "⚠️ Probability %.3f < threshold %.2f", best_prob, app_threshold_);
                    ESP_LOGI(TAG, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
                    multinet_->clean(multinet_model_data_);
                    continue;
                }
                
                // 检查命令 ID 是否有效
                int array_index = command_id - 1;
                if (array_index >= 0 && array_index < commands_.size()) {
                    auto& command = commands_[array_index];
                    
                    if (command.action == "wake") {
                        last_detected_wake_word_ = command.text;
                        running_ = false;
                        
                        ESP_LOGI(TAG, "✓✓✓ Wake word: \"%s\" (prob: %.3f, WITH AFE) ✓✓✓",
                                 last_detected_wake_word_.c_str(), best_prob);
                        ESP_LOGI(TAG, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
                        
                        if (wake_word_detected_callback_) {
                            wake_word_detected_callback_(last_detected_wake_word_);
                        }
                    }
                }
            }
            multinet_->clean(multinet_model_data_);
        }
        else if (mn_state == ESP_MN_STATE_TIMEOUT) {
            multinet_->clean(multinet_model_data_);
        }
    }
}

