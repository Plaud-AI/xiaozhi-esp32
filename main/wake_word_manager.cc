#include "wake_word_manager.h"
#include "audio/wake_words/custom_wake_word.h"

#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <cJSON.h>

#define TAG "WakeWordManager"

WakeWordManager& WakeWordManager::GetInstance() {
    static WakeWordManager instance;
    return instance;
}

WakeWordManager::WakeWordManager() {
    ESP_LOGI(TAG, "WakeWordManager initialized");
}

bool WakeWordManager::SetWakeWords(const std::vector<WakeWordConfig>& words, 
                                    float threshold, 
                                    bool replace) {
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "║ SetWakeWords 调用");
    ESP_LOGI(TAG, "╠════════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "║ 参数: count=%d, threshold=%.3f, replace=%d", 
             words.size(), threshold, replace);
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════");
    
    if (words.empty()) {
        ESP_LOGW(TAG, "❌ Empty wake words list");
        return false;
    }
    
    if (words.size() > MAX_WAKE_WORDS) {
        ESP_LOGW(TAG, "❌ Too many wake words: %d (max %d)", words.size(), MAX_WAKE_WORDS);
        return false;
    }
    
    // 替换模式：清空现有配置
    if (replace) {
        ESP_LOGI(TAG, "🗑️  清空现有唤醒词配置 (replace=true)");
        wake_words_.clear();
    } else {
        ESP_LOGI(TAG, "➕ 追加模式，当前已有 %d 个唤醒词", wake_words_.size());
    }
    
    // 添加新唤醒词
    int added_count = 0;
    for (size_t i = 0; i < words.size(); i++) {
        const auto& word = words[i];
        
        ESP_LOGI(TAG, "────────────────────────────────────────");
        ESP_LOGI(TAG, "处理唤醒词 [%d/%d]:", i + 1, words.size());
        ESP_LOGI(TAG, "  text: %s", word.text.c_str());
        ESP_LOGI(TAG, "  display: %s", word.display.c_str());
        ESP_LOGI(TAG, "  phonemes: %d 个变体", word.phonemes.size());
        for (size_t j = 0; j < word.phonemes.size(); j++) {
            ESP_LOGI(TAG, "    [%d] %s", j + 1, word.phonemes[j].c_str());
        }
        
        // 验证配置
        if (!ValidateConfig(word)) {
            ESP_LOGW(TAG, "❌ Invalid wake word config: %s", word.text.c_str());
            continue;
        }
        
        // 检查是否已存在
        bool exists = false;
        for (const auto& existing : wake_words_) {
            if (existing.text == word.text) {
                exists = true;
                break;
            }
        }
        
        if (exists) {
            ESP_LOGW(TAG, "⚠️  Wake word already exists: %s (跳过)", word.text.c_str());
            continue;
        }
        
        wake_words_.push_back(word);
        added_count++;
        ESP_LOGI(TAG, "✅ Added wake word: %s", word.text.c_str());
    }
    
    threshold_ = threshold;
    
    ESP_LOGI(TAG, "════════════════════════════════════════");
    ESP_LOGI(TAG, "📊 汇总:");
    ESP_LOGI(TAG, "  新增: %d 个唤醒词", added_count);
    ESP_LOGI(TAG, "  总计: %d 个唤醒词", wake_words_.size());
    ESP_LOGI(TAG, "  阈值: %.3f", threshold_);
    ESP_LOGI(TAG, "════════════════════════════════════════");
    
    // 保存到 NVS
    ESP_LOGI(TAG, "💾 开始保存到 NVS...");
    if (!SaveToNVS()) {
        ESP_LOGE(TAG, "❌ Failed to save to NVS");
        return false;
    }
    
    ESP_LOGI(TAG, "✅ SetWakeWords 完成！");
    return true;
}

std::vector<WakeWordConfig> WakeWordManager::GetWakeWords() const {
    return wake_words_;
}

bool WakeWordManager::DeleteWakeWord(const std::string& text) {
    ESP_LOGI(TAG, "Deleting wake word: %s", text.c_str());
    
    auto it = wake_words_.begin();
    while (it != wake_words_.end()) {
        if (it->text == text) {
            wake_words_.erase(it);
            ESP_LOGI(TAG, "Wake word deleted: %s", text.c_str());
            return SaveToNVS();
        }
        ++it;
    }
    
    ESP_LOGW(TAG, "Wake word not found: %s", text.c_str());
    return false;
}

void WakeWordManager::ClearWakeWords() {
    ESP_LOGI(TAG, "Clearing all wake words");
    wake_words_.clear();
    SaveToNVS();
}

bool WakeWordManager::ResetToDefault() {
    ESP_LOGI(TAG, "Resetting to default wake words");
    wake_words_ = GetDefaultWakeWords();
    threshold_ = DEFAULT_WAKE_WORD_THRESHOLD;
    return SaveToNVS();
}

bool WakeWordManager::SaveToNVS() {
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "║ SaveToNVS 调用");
    ESP_LOGI(TAG, "╠════════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "║ 唤醒词数量: %d", wake_words_.size());
    ESP_LOGI(TAG, "║ 阈值: %.3f", threshold_);
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════");
    
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to open NVS: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "✅ NVS 打开成功 (namespace: %s)", NVS_NAMESPACE);
    
    // 构建 JSON
    ESP_LOGI(TAG, "📝 构建 JSON 数据...");
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "❌ Failed to create JSON object");
        nvs_close(nvs_handle);
        return false;
    }
    
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON_AddNumberToObject(root, "threshold", threshold_);
    
    cJSON* words_array = cJSON_CreateArray();
    for (const auto& word : wake_words_) {
        cJSON* word_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(word_obj, "text", word.text.c_str());
        cJSON_AddStringToObject(word_obj, "display", word.display.c_str());
        
        cJSON* phonemes_array = cJSON_CreateArray();
        for (const auto& phoneme : word.phonemes) {
            cJSON_AddItemToArray(phonemes_array, cJSON_CreateString(phoneme.c_str()));
        }
        cJSON_AddItemToObject(word_obj, "phonemes", phonemes_array);
        
        cJSON_AddItemToArray(words_array, word_obj);
    }
    cJSON_AddItemToObject(root, "words", words_array);
    
    // 序列化
    ESP_LOGI(TAG, "🔄 序列化 JSON...");
    char* json_str = cJSON_PrintUnformatted(root);
    if (!json_str) {
        ESP_LOGE(TAG, "❌ Failed to serialize JSON");
        cJSON_Delete(root);
        nvs_close(nvs_handle);
        return false;
    }
    
    size_t json_len = strlen(json_str);
    ESP_LOGI(TAG, "✅ JSON 序列化完成 (长度: %d 字节)", json_len);
    ESP_LOGI(TAG, "JSON 内容预览: %.100s%s", json_str, (json_len > 100) ? "..." : "");
    
    // 保存到 NVS
    ESP_LOGI(TAG, "💾 写入 NVS (key: %s)...", NVS_KEY_CONFIG);
    err = nvs_set_str(nvs_handle, NVS_KEY_CONFIG, json_str);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to save to NVS: %s", esp_err_to_name(err));
        free(json_str);
        cJSON_Delete(root);
        nvs_close(nvs_handle);
        return false;
    }
    ESP_LOGI(TAG, "✅ NVS 写入成功");
    
    ESP_LOGI(TAG, "💾 提交 NVS...");
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "⚠️  NVS commit failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "✅ NVS 提交成功");
    }
    
    ESP_LOGI(TAG, "✅ SaveToNVS 完成！");
    
    free(json_str);
    cJSON_Delete(root);
    nvs_close(nvs_handle);
    
    return true;
}

bool WakeWordManager::LoadFromNVS() {
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "║ LoadFromNVS 调用");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════");
    
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "⚠️  Failed to open NVS: %s (may not exist yet)", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "✅ NVS 打开成功 (namespace: %s)", NVS_NAMESPACE);
    
    // 获取 JSON 字符串长度
    ESP_LOGI(TAG, "📏 获取数据长度 (key: %s)...", NVS_KEY_CONFIG);
    size_t required_size = 0;
    err = nvs_get_str(nvs_handle, NVS_KEY_CONFIG, nullptr, &required_size);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "⚠️  No saved config found: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return false;
    }
    ESP_LOGI(TAG, "✅ 数据大小: %d 字节", required_size);
    
    // 读取 JSON 字符串
    ESP_LOGI(TAG, "📖 读取数据...");
    char* json_str = (char*)malloc(required_size);
    if (!json_str) {
        ESP_LOGE(TAG, "❌ Failed to allocate %d bytes", required_size);
        nvs_close(nvs_handle);
        return false;
    }
    
    err = nvs_get_str(nvs_handle, NVS_KEY_CONFIG, json_str, &required_size);
    nvs_close(nvs_handle);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to read from NVS: %s", esp_err_to_name(err));
        free(json_str);
        return false;
    }
    
    ESP_LOGI(TAG, "✅ JSON 读取成功 (长度: %d 字节)", required_size);
    ESP_LOGI(TAG, "JSON 内容预览: %.100s%s", json_str, (required_size > 100) ? "..." : "");
    
    // 解析 JSON
    ESP_LOGI(TAG, "🔄 解析 JSON...");
    cJSON* root = cJSON_Parse(json_str);
    free(json_str);
    
    if (!root) {
        ESP_LOGE(TAG, "❌ Failed to parse JSON");
        const char* error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            ESP_LOGE(TAG, "JSON 错误位置: %s", error_ptr);
        }
        return false;
    }
    ESP_LOGI(TAG, "✅ JSON 解析成功");
    
    // 读取版本
    cJSON* version = cJSON_GetObjectItem(root, "version");
    if (version && cJSON_IsNumber(version)) {
        ESP_LOGI(TAG, "📌 Config version: %d", version->valueint);
    }
    
    // 读取阈值
    cJSON* threshold = cJSON_GetObjectItem(root, "threshold");
    if (threshold && cJSON_IsNumber(threshold)) {
        threshold_ = threshold->valuedouble;
        ESP_LOGI(TAG, "📌 阈值: %.3f", threshold_);
    } else {
        ESP_LOGI(TAG, "⚠️  未找到阈值，使用默认值: %.3f", threshold_);
    }
    
    // 读取唤醒词列表
    ESP_LOGI(TAG, "📋 读取唤醒词列表...");
    cJSON* words_array = cJSON_GetObjectItem(root, "words");
    if (!words_array || !cJSON_IsArray(words_array)) {
        ESP_LOGE(TAG, "❌ Invalid words array in JSON");
        cJSON_Delete(root);
        return false;
    }
    
    int array_size = cJSON_GetArraySize(words_array);
    ESP_LOGI(TAG, "找到 %d 个唤醒词", array_size);
    
    wake_words_.clear();
    
    cJSON* word_item = nullptr;
    int word_index = 0;
    cJSON_ArrayForEach(word_item, words_array) {
        word_index++;
        ESP_LOGI(TAG, "────────────────────────────────────────");
        ESP_LOGI(TAG, "解析唤醒词 [%d/%d]", word_index, array_size);
        
        WakeWordConfig config;
        
        cJSON* text = cJSON_GetObjectItem(word_item, "text");
        cJSON* display = cJSON_GetObjectItem(word_item, "display");
        cJSON* phonemes = cJSON_GetObjectItem(word_item, "phonemes");
        
        if (!text || !cJSON_IsString(text)) {
            ESP_LOGW(TAG, "❌ text 字段缺失或无效，跳过");
            continue;
        }
        
        if (!phonemes || !cJSON_IsArray(phonemes)) {
            ESP_LOGW(TAG, "❌ phonemes 字段缺失或无效，跳过");
            continue;
        }
        
        config.text = text->valuestring;
        config.display = display && cJSON_IsString(display) ? 
                         display->valuestring : config.text;
        
        ESP_LOGI(TAG, "  text: %s", config.text.c_str());
        ESP_LOGI(TAG, "  display: %s", config.display.c_str());
        
        // 读取音素数组（带验证）
        cJSON* phoneme_item = nullptr;
        int phoneme_index = 0;
        int invalid_phoneme_count = 0;
        cJSON_ArrayForEach(phoneme_item, phonemes) {
            if (cJSON_IsString(phoneme_item)) {
                std::string phoneme_str = phoneme_item->valuestring;
                
                // ⚠️ 验证音素长度（MultiNet 要求 >= 3 字符）
                if (phoneme_str.length() < 3) {
                    ESP_LOGW(TAG, "    phoneme[%d]: '%s' (长度: %d) ❌ 太短，跳过", 
                            phoneme_index + 1, phoneme_str.c_str(), phoneme_str.length());
                    invalid_phoneme_count++;
                    continue;
                }
                
                // 验证不能全是空格
                bool all_spaces = true;
                for (char c : phoneme_str) {
                    if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                        all_spaces = false;
                        break;
                    }
                }
                if (all_spaces) {
                    ESP_LOGW(TAG, "    phoneme[%d]: 全是空白字符 ❌ 跳过", phoneme_index + 1);
                    invalid_phoneme_count++;
                    continue;
                }
                
                config.phonemes.push_back(phoneme_str);
                phoneme_index++;
                ESP_LOGI(TAG, "    phoneme[%d]: %s (长度: %d) ✅", 
                        phoneme_index, phoneme_str.c_str(), phoneme_str.length());
            }
        }
        
        // ✅ 策略：text 永远作为第一个默认音素（如果有效）
        // 先检查 text 是否有效
        if (config.text.length() < 3) {
            ESP_LOGE(TAG, "❌ text '%s' 太短（长度: %d < 3），无法作为默认音素，跳过该唤醒词", 
                    config.text.c_str(), config.text.length());
            continue;
        }
        
        // 检查 text 是否已经在 phonemes 中
        bool text_exists = false;
        for (const auto& p : config.phonemes) {
            if (p == config.text) {
                text_exists = true;
                break;
            }
        }
        
        // 如果 text 不在 phonemes 中，将其作为第一个音素插入
        if (!text_exists) {
            config.phonemes.insert(config.phonemes.begin(), config.text);
            if (invalid_phoneme_count > 0) {
                ESP_LOGW(TAG, "⚠️  所有 %d 个音素都无效，使用 text 作为唯一音素: '%s'", 
                        invalid_phoneme_count, config.text.c_str());
            } else if (config.phonemes.size() == 1) {
                ESP_LOGI(TAG, "💡 未提供音素，使用 text 作为默认音素: '%s'", config.text.c_str());
            } else {
                ESP_LOGI(TAG, "💡 将 text 作为默认音素添加到最前面: '%s'", config.text.c_str());
            }
        } else {
            ESP_LOGI(TAG, "ℹ️  text 已存在于音素列表中");
        }
        
        wake_words_.push_back(config);
        ESP_LOGI(TAG, "✅ 加载成功: %s (%d 个有效音素)", 
                 config.text.c_str(), config.phonemes.size());
    }
    
    cJSON_Delete(root);
    
    ESP_LOGI(TAG, "════════════════════════════════════════");
    ESP_LOGI(TAG, "✅ LoadFromNVS 完成！");
    ESP_LOGI(TAG, "  成功加载: %d 个唤醒词", wake_words_.size());
    ESP_LOGI(TAG, "  阈值: %.3f", threshold_);
    ESP_LOGI(TAG, "════════════════════════════════════════");
    return true;
}

bool WakeWordManager::ApplyToCustomWakeWord(CustomWakeWord* wake_word) {
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "║ ApplyToCustomWakeWord 调用");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════");
    
    if (!wake_word) {
        ESP_LOGE(TAG, "❌ Wake word object is NULL");
        return false;
    }
    ESP_LOGI(TAG, "✅ CustomWakeWord 对象有效");
    
    if (wake_words_.empty()) {
        ESP_LOGW(TAG, "⚠️  No wake words to apply");
        return false;
    }
    
    ESP_LOGI(TAG, "📊 配置信息:");
    ESP_LOGI(TAG, "  唤醒词数量: %d", wake_words_.size());
    ESP_LOGI(TAG, "  阈值: %.3f", threshold_);
    
    // 1. 清除现有命令
    ESP_LOGI(TAG, "🗑️  清除现有命令...");
    wake_word->ClearCommands();
    
    // 2. 添加新命令（将所有音素变体都添加进去）
    ESP_LOGI(TAG, "➕ 添加新命令...");
    ESP_LOGI(TAG, "════════════════════════════════════════");
    
    int total_phonemes = 0;
    for (size_t i = 0; i < wake_words_.size(); i++) {
        const auto& word = wake_words_[i];
        ESP_LOGI(TAG, "唤醒词 [%d/%d]: %s", i + 1, wake_words_.size(), word.text.c_str());
        ESP_LOGI(TAG, "  display: %s", word.display.c_str());
        ESP_LOGI(TAG, "  音素变体: %d 个", word.phonemes.size());
        
        for (size_t j = 0; j < word.phonemes.size(); j++) {
            const auto& phoneme = word.phonemes[j];
            ESP_LOGI(TAG, "    [%d] 添加命令: \"%s\" -> \"%s\"", 
                     j + 1, phoneme.c_str(), word.display.c_str());
            wake_word->AddCommand(phoneme, word.display, "wake");
            total_phonemes++;
        }
    }
    
    ESP_LOGI(TAG, "════════════════════════════════════════");
    ESP_LOGI(TAG, "✅ 共添加 %d 个音素命令", total_phonemes);
    
    // 3. 设置阈值
    ESP_LOGI(TAG, "🎚️  设置阈值: %.3f", threshold_);
    wake_word->SetThreshold(threshold_);
    
    // 4. 更新命令到 MultiNet（运行时生效！）
    ESP_LOGI(TAG, "🔄 更新命令到 MultiNet...");
    bool success = wake_word->UpdateCommands();
    
    ESP_LOGI(TAG, "════════════════════════════════════════");
    if (success) {
        ESP_LOGI(TAG, "✅✅✅ ApplyToCustomWakeWord 成功！");
        ESP_LOGI(TAG, "  音素命令: %d 个", total_phonemes);
        ESP_LOGI(TAG, "  阈值: %.3f", threshold_);
        ESP_LOGI(TAG, "  运行时更新: 成功 (无需重启)");
    } else {
        ESP_LOGW(TAG, "⚠️  MultiNet 更新失败");
        ESP_LOGW(TAG, "  命令已添加，但需要重启设备才能生效");
    }
    ESP_LOGI(TAG, "════════════════════════════════════════");
    
    return success;
}

bool WakeWordManager::ValidateConfig(const WakeWordConfig& config) {
    // 检查 text 字段（必需）
    if (config.text.empty()) {
        ESP_LOGW(TAG, "❌ 验证失败: text 字段为空");
        return false;
    }
    
    // 检查 phonemes 字段（必需，但可以只包含 text 本身）
    if (config.phonemes.empty()) {
        ESP_LOGW(TAG, "❌ 验证失败: phonemes 列表为空 (text: %s)", config.text.c_str());
        ESP_LOGW(TAG, "   提示: phonemes 应该包含至少一个音素变体");
        return false;
    }
    
    // 检查音素格式（不能为空字符串）
    for (size_t i = 0; i < config.phonemes.size(); i++) {
        if (config.phonemes[i].empty()) {
            ESP_LOGW(TAG, "❌ 验证失败: phoneme[%d] 为空字符串 (wake word: %s)", 
                     i, config.text.c_str());
            return false;
        }
    }
    
    ESP_LOGD(TAG, "✅ 验证通过: text='%s', phonemes=%d", 
             config.text.c_str(), config.phonemes.size());
    return true;
}

std::vector<WakeWordConfig> WakeWordManager::GetDefaultWakeWords() {
    std::vector<WakeWordConfig> defaults;
    
    // 默认唤醒词 1: "hi plaud" (MultiNet6 Grapheme 格式)
    defaults.push_back({
        "hi plaud",
        "Hi Plaud",
        {
            "HI PLAUD",
            "HEY PLAUD",
            "HELLO PLAUD"
        }
    });
    
    // 默认唤醒词 2: "hi device" (更清晰易识别)
    defaults.push_back({
        "hi device",
        "Hi Device",
        {
            "HI DEVICE",
            "HEY DEVICE"
        }
    });
    
    ESP_LOGI(TAG, "Using %d default wake words", defaults.size());
    return defaults;
}

