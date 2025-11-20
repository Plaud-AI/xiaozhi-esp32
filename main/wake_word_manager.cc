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
    ESP_LOGI(TAG, "SetWakeWords: count=%d, threshold=%.2f, replace=%d", 
             words.size(), threshold, replace);
    
    if (words.empty()) {
        ESP_LOGW(TAG, "Empty wake words list");
        return false;
    }
    
    if (words.size() > MAX_WAKE_WORDS) {
        ESP_LOGW(TAG, "Too many wake words: %d (max %d)", words.size(), MAX_WAKE_WORDS);
        return false;
    }
    
    // 替换模式：清空现有配置
    if (replace) {
        wake_words_.clear();
    }
    
    // 添加新唤醒词
    for (const auto& word : words) {
        // 验证配置
        if (!ValidateConfig(word)) {
            ESP_LOGW(TAG, "Invalid wake word config: %s", word.text.c_str());
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
            ESP_LOGW(TAG, "Wake word already exists: %s", word.text.c_str());
            continue;
        }
        
        wake_words_.push_back(word);
        ESP_LOGI(TAG, "Added wake word: %s (%d phoneme variants)", 
                 word.text.c_str(), word.phonemes.size());
    }
    
    threshold_ = threshold;
    
    // 保存到 NVS
    if (!SaveToNVS()) {
        ESP_LOGW(TAG, "Failed to save to NVS");
        return false;
    }
    
    ESP_LOGI(TAG, "SetWakeWords completed: total=%d wake words", wake_words_.size());
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
    threshold_ = 0.15f;
    return SaveToNVS();
}

bool WakeWordManager::SaveToNVS() {
    ESP_LOGI(TAG, "Saving %d wake words to NVS", wake_words_.size());
    
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return false;
    }
    
    // 构建 JSON
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "Failed to create JSON object");
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
    char* json_str = cJSON_PrintUnformatted(root);
    if (!json_str) {
        ESP_LOGE(TAG, "Failed to serialize JSON");
        cJSON_Delete(root);
        nvs_close(nvs_handle);
        return false;
    }
    
    // 保存到 NVS
    err = nvs_set_str(nvs_handle, NVS_KEY_CONFIG, json_str);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save to NVS: %s", esp_err_to_name(err));
        free(json_str);
        cJSON_Delete(root);
        nvs_close(nvs_handle);
        return false;
    }
    
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS commit failed: %s", esp_err_to_name(err));
    }
    
    ESP_LOGI(TAG, "Saved to NVS successfully (size: %d bytes)", strlen(json_str));
    
    free(json_str);
    cJSON_Delete(root);
    nvs_close(nvs_handle);
    
    return true;
}

bool WakeWordManager::LoadFromNVS() {
    ESP_LOGI(TAG, "Loading wake words from NVS");
    
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS: %s (may not exist yet)", esp_err_to_name(err));
        return false;
    }
    
    // 获取 JSON 字符串长度
    size_t required_size = 0;
    err = nvs_get_str(nvs_handle, NVS_KEY_CONFIG, nullptr, &required_size);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No saved config found");
        nvs_close(nvs_handle);
        return false;
    }
    
    // 读取 JSON 字符串
    char* json_str = (char*)malloc(required_size);
    if (!json_str) {
        ESP_LOGE(TAG, "Failed to allocate memory");
        nvs_close(nvs_handle);
        return false;
    }
    
    err = nvs_get_str(nvs_handle, NVS_KEY_CONFIG, json_str, &required_size);
    nvs_close(nvs_handle);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read from NVS: %s", esp_err_to_name(err));
        free(json_str);
        return false;
    }
    
    ESP_LOGI(TAG, "Loaded JSON from NVS (size: %d bytes)", required_size);
    
    // 解析 JSON
    cJSON* root = cJSON_Parse(json_str);
    free(json_str);
    
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        return false;
    }
    
    // 读取版本
    cJSON* version = cJSON_GetObjectItem(root, "version");
    if (version && cJSON_IsNumber(version)) {
        ESP_LOGI(TAG, "Config version: %d", version->valueint);
    }
    
    // 读取阈值（但不使用，保留用于调试）
    cJSON* threshold = cJSON_GetObjectItem(root, "threshold");
    if (threshold && cJSON_IsNumber(threshold)) {
        threshold_ = threshold->valuedouble;
        ESP_LOGI(TAG, "Loaded threshold from NVS: %.3f (will NOT be applied to CustomWakeWord)", threshold_);
    } else {
        ESP_LOGI(TAG, "No threshold in NVS, default: %.3f (will NOT be applied to CustomWakeWord)", threshold_);
    }
    
    // 读取唤醒词列表
    cJSON* words_array = cJSON_GetObjectItem(root, "words");
    if (!words_array || !cJSON_IsArray(words_array)) {
        ESP_LOGE(TAG, "Invalid words array in JSON");
        cJSON_Delete(root);
        return false;
    }
    
    wake_words_.clear();
    
    cJSON* word_item = nullptr;
    cJSON_ArrayForEach(word_item, words_array) {
        WakeWordConfig config;
        
        cJSON* text = cJSON_GetObjectItem(word_item, "text");
        cJSON* display = cJSON_GetObjectItem(word_item, "display");
        cJSON* phonemes = cJSON_GetObjectItem(word_item, "phonemes");
        
        if (!text || !cJSON_IsString(text) || !phonemes || !cJSON_IsArray(phonemes)) {
            ESP_LOGW(TAG, "Skipping invalid wake word entry");
            continue;
        }
        
        config.text = text->valuestring;
        config.display = display && cJSON_IsString(display) ? 
                         display->valuestring : config.text;
        
        // 读取音素数组
        cJSON* phoneme_item = nullptr;
        cJSON_ArrayForEach(phoneme_item, phonemes) {
            if (cJSON_IsString(phoneme_item)) {
                config.phonemes.push_back(phoneme_item->valuestring);
            }
        }
        
        if (!config.phonemes.empty()) {
            wake_words_.push_back(config);
            ESP_LOGI(TAG, "Loaded wake word: %s (%d phonemes)", 
                     config.text.c_str(), config.phonemes.size());
        }
    }
    
    cJSON_Delete(root);
    
    ESP_LOGI(TAG, "Loaded %d wake words from NVS", wake_words_.size());
    return true;
}

bool WakeWordManager::ApplyToCustomWakeWord(CustomWakeWord* wake_word) {
    if (!wake_word) {
        ESP_LOGE(TAG, "Wake word object is NULL");
        return false;
    }
    
    if (wake_words_.empty()) {
        ESP_LOGW(TAG, "No wake words to apply");
        return false;
    }
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Applying %d wake words to CustomWakeWord", wake_words_.size());
    
    // 1. 清除现有命令
    wake_word->ClearCommands();
    
    // 2. 添加新命令
    int total_phonemes = 0;
    for (const auto& word : wake_words_) {
        for (const auto& phoneme : word.phonemes) {
            wake_word->AddCommand(phoneme, word.display, "wake");
            total_phonemes++;
        }
    }
    
    // 3. 设置阈值（暂时禁用，使用代码中的默认阈值进行测试）
    // wake_word->SetThreshold(threshold_);
    ESP_LOGI(TAG, "⚠️  Using default threshold in code (ignoring NVS threshold %.3f for testing)", threshold_);
    
    // 4. 更新命令到 MultiNet（运行时生效！）
    bool success = wake_word->UpdateCommands();
    if (success) {
        ESP_LOGI(TAG, "✓ Applied %d phoneme variants (threshold=%.2f) - RUNTIME UPDATE SUCCESS!", 
                 total_phonemes, threshold_);
    } else {
        ESP_LOGW(TAG, "⚠️  Commands added but MultiNet update failed, will apply on next Initialize()");
    }
    ESP_LOGI(TAG, "========================================");
    
    return success;
}

bool WakeWordManager::ValidateConfig(const WakeWordConfig& config) {
    if (config.text.empty()) {
        ESP_LOGW(TAG, "Wake word text is empty");
        return false;
    }
    
    if (config.phonemes.empty()) {
        ESP_LOGW(TAG, "Wake word phonemes list is empty: %s", config.text.c_str());
        return false;
    }
    
    // 检查音素格式（简单验证：不能为空字符串）
    for (const auto& phoneme : config.phonemes) {
        if (phoneme.empty()) {
            ESP_LOGW(TAG, "Empty phoneme in wake word: %s", config.text.c_str());
            return false;
        }
    }
    
    return true;
}

std::vector<WakeWordConfig> WakeWordManager::GetDefaultWakeWords() {
    std::vector<WakeWordConfig> defaults;
    
    // 默认唤醒词 1: "hi plaud"
    defaults.push_back({
        "hi plaud",
        "Hi Plaud",
        {
            "hi PLAA1D",
            "hi PLaD",
            "hi PLeD",
            "HH AY1 P L AA1 D"
        }
    });
    
    // 默认唤醒词 2: "hi nicebuild"
    defaults.push_back({
        "hi nicebuild",
        "Hi Nicebuild",
        {
            "HH AY1 N AY1 S B IH0 L D",
            "hi NgSgBcLD"
        }
    });
    
    ESP_LOGI(TAG, "Using %d default wake words", defaults.size());
    return defaults;
}

