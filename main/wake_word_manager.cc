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


// ─────────────────────────────────────────────────────────────────────────────
// 动态模型加载 —— 挂载到 MicroWakeWord（v2.3 多槽位）
// ─────────────────────────────────────────────────────────────────────────────
#include "application.h"
#include "audio/wake_words/micro/micro_wake_word.h"
#include "wake_word_downloader.h"
#include <esp_heap_caps.h>
#include <esp_spiffs.h>
#include <cstdio>

namespace {

bool EnsureWwStoreMounted() {
    if (esp_spiffs_mounted("ww_store")) return true;
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/ww",
        .partition_label = "ww_store",
        .max_files = 8,
        .format_if_mount_failed = true,
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE("WakeWordManager", "挂载 ww_store SPIFFS 失败: %s", esp_err_to_name(ret));
        return false;
    }
    return true;
}

// 从 Application 中拿到 MicroWakeWord；如果当前 WakeWord 不是 MicroWakeWord，返回 nullptr。
micro_wake_word::MicroWakeWord* GetMicroWakeWord() {
    auto& app = Application::GetInstance();
    WakeWord* ww = app.GetAudioService().GetWakeWord();
    return dynamic_cast<micro_wake_word::MicroWakeWord*>(ww);
}

}  // namespace

bool WakeWordManager::LoadCustomModel(const std::string& wakeword_id,
                                      const std::string& wake_word_text,
                                      const std::string& display_name,
                                      const std::string& expected_md5,
                                      size_t expected_size) {
    ESP_LOGI(TAG, "LoadCustomModel: wakeword_id=%s text='%s'",
             wakeword_id.c_str(), wake_word_text.c_str());

    if (wakeword_id.empty() || wake_word_text.empty()) {
        ESP_LOGE(TAG, "LoadCustomModel: 参数非法");
        return false;
    }

    auto* mww = GetMicroWakeWord();
    if (!mww) {
        ESP_LOGE(TAG, "LoadCustomModel: 当前 WakeWord 不是 MicroWakeWord，无法挂载动态模型");
        return false;
    }

    if (!EnsureWwStoreMounted()) return false;

    // ── 1. 如果已有同 id 的槽位，先卸载旧实例（覆盖式安装）────────────────
    for (auto it = installed_models_.begin(); it != installed_models_.end(); ++it) {
        if (it->wakeword_id == wakeword_id) {
            ESP_LOGI(TAG, "LoadCustomModel: 已存在 wakeword_id=%s，先卸载旧实例", wakeword_id.c_str());
            mww->RemoveDynamicModel(MakeDynamicModelId(wakeword_id));
            if (it->buffer) heap_caps_free(it->buffer);
            installed_models_.erase(it);
            break;
        }
    }

    // ── 2. 读取 .tflite 文件到 SPIRAM ──────────────────────────────────
    std::string path = std::string("/ww/") + wakeword_id + ".tflite";
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        ESP_LOGE(TAG, "LoadCustomModel: 文件不存在 %s", path.c_str());
        return false;
    }
    fseek(f, 0, SEEK_END);
    size_t size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size == 0 || size > MAX_CUSTOM_MODEL_BYTES) {
        ESP_LOGE(TAG, "LoadCustomModel: 文件大小非法 %d", (int)size);
        fclose(f);
        return false;
    }
    if (expected_size > 0 && size != expected_size) {
        ESP_LOGE(TAG, "LoadCustomModel: 大小不符 actual=%d expected=%d", (int)size, (int)expected_size);
        fclose(f);
        return false;
    }

    uint8_t* buf = static_cast<uint8_t*>(
        heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!buf) {
        ESP_LOGE(TAG, "LoadCustomModel: SPIRAM 分配失败 %d bytes", (int)size);
        fclose(f);
        return false;
    }
    size_t n = fread(buf, 1, size, f);
    fclose(f);
    if (n != size) {
        ESP_LOGE(TAG, "LoadCustomModel: 读取不完整 %d/%d", (int)n, (int)size);
        heap_caps_free(buf);
        return false;
    }

    // ── 3. 注册到 MicroWakeWord（失败会内部自动回滚 push_back）─────────
    const std::string model_id = MakeDynamicModelId(wakeword_id);
    if (!mww->AddDynamicModel(buf, size, model_id, wake_word_text,
                              DYN_PROBABILITY_CUTOFF,
                              DYN_SLIDING_WINDOW_SIZE,
                              DYN_TENSOR_ARENA_SIZE,
                              true)) {
        ESP_LOGE(TAG, "LoadCustomModel: AddDynamicModel 失败");
        heap_caps_free(buf);
        return false;
    }

    // ── 4. 覆盖内置：把同 label 的模型全部 disable（忽略大小写+压缩空白）
    size_t overridden = mww->DisableModelsByLabel(wake_word_text, model_id);
    if (overridden > 0) {
        ESP_LOGI(TAG, "LoadCustomModel: 覆盖了 %u 个同 label 内置模型", (unsigned)overridden);
    }

    // ── 5. 记录到列表并持久化 NVS ─────────────────────────────────────
    InstalledModel slot;
    slot.wakeword_id    = wakeword_id;
    slot.wake_word_text = wake_word_text;
    slot.display        = display_name;
    slot.file_md5       = expected_md5;
    slot.file_size      = size;
    slot.buffer         = buf;
    installed_models_.push_back(std::move(slot));

    SaveInstalledModelList();

    ESP_LOGI(TAG, "✅ LoadCustomModel 完成: '%s' (共 %u 个动态槽位)",
             wake_word_text.c_str(), (unsigned)installed_models_.size());
    return true;
}

bool WakeWordManager::UnloadCustomModel(const std::string& wakeword_id) {
    auto* mww = GetMicroWakeWord();
    for (auto it = installed_models_.begin(); it != installed_models_.end(); ++it) {
        if (it->wakeword_id != wakeword_id) continue;
        if (mww) {
            mww->RemoveDynamicModel(MakeDynamicModelId(wakeword_id));
        }
        if (it->buffer) heap_caps_free(it->buffer);
        installed_models_.erase(it);
        SaveInstalledModelList();
        ESP_LOGI(TAG, "UnloadCustomModel: '%s' 已卸载", wakeword_id.c_str());
        return true;
    }
    ESP_LOGW(TAG, "UnloadCustomModel: 未找到 '%s'", wakeword_id.c_str());
    return false;
}

std::vector<WakeWordManager::InstalledModel> WakeWordManager::GetInstalledModels() const {
    return installed_models_;
}

// ─────────────────────────────────────────────────────────────────────────────
// NVS 持久化：列表格式 {"version":2,"list":[{...}]}
// ─────────────────────────────────────────────────────────────────────────────
void WakeWordManager::SaveInstalledModelList() {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_MODEL_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SaveInstalledModelList: nvs_open 失败 %s", esp_err_to_name(err));
        return;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "version", 2);
    cJSON* arr = cJSON_CreateArray();
    for (const auto& m : installed_models_) {
        cJSON* obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "wakeword_id", m.wakeword_id.c_str());
        cJSON_AddStringToObject(obj, "wake_word_text", m.wake_word_text.c_str());
        cJSON_AddStringToObject(obj, "display", m.display.c_str());
        cJSON_AddStringToObject(obj, "file_md5", m.file_md5.c_str());
        cJSON_AddNumberToObject(obj, "file_size", (double)m.file_size);
        cJSON_AddItemToArray(arr, obj);
    }
    cJSON_AddItemToObject(root, "list", arr);

    char* json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (installed_models_.empty()) {
        // 全空时顺便清理 legacy 单槽 key
        nvs_erase_key(handle, NVS_MODEL_KEY_LEGACY);
    }

    err = nvs_set_str(handle, NVS_MODEL_KEY, json_str);
    free(json_str);
    if (err == ESP_OK) {
        nvs_commit(handle);
        ESP_LOGI(TAG, "✅ NVS 已保存 %u 个动态模型槽", (unsigned)installed_models_.size());
    } else {
        ESP_LOGE(TAG, "SaveInstalledModelList: nvs_set_str 失败 %s", esp_err_to_name(err));
    }
    nvs_close(handle);
}

void WakeWordManager::ClearInstalledModelMeta() {
    nvs_handle_t handle;
    if (nvs_open(NVS_MODEL_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;
    nvs_erase_key(handle, NVS_MODEL_KEY);
    nvs_erase_key(handle, NVS_MODEL_KEY_LEGACY);
    nvs_commit(handle);
    nvs_close(handle);
    ESP_LOGI(TAG, "ClearInstalledModelMeta: NVS 列表已清除");
}

// ─────────────────────────────────────────────────────────────────────────────
// LoadOnBoot —— 遍历 NVS 列表，对每条记录做 MD5 完整性校验后加载
// ─────────────────────────────────────────────────────────────────────────────
void WakeWordManager::LoadOnBoot() {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_MODEL_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "LoadOnBoot: 无 ww_model 命名空间，跳过");
        return;
    }

    // 优先读 v2 列表；若没有则尝试 v1 单槽迁移
    const char* key_to_use = NVS_MODEL_KEY;
    size_t len = 0;
    err = nvs_get_str(handle, key_to_use, nullptr, &len);
    if (err != ESP_OK || len == 0) {
        key_to_use = NVS_MODEL_KEY_LEGACY;
        len = 0;
        err = nvs_get_str(handle, key_to_use, nullptr, &len);
        if (err != ESP_OK || len == 0) {
            nvs_close(handle);
            ESP_LOGI(TAG, "LoadOnBoot: NVS 中无动态模型记录");
            return;
        }
        ESP_LOGI(TAG, "LoadOnBoot: 发现 v1 单槽记录，将迁移为 v2 列表格式");
    }

    std::string json_str(len, '\0');
    err = nvs_get_str(handle, key_to_use, &json_str[0], &len);
    nvs_close(handle);
    if (err != ESP_OK) return;

    cJSON* root = cJSON_Parse(json_str.c_str());
    if (!root) {
        ESP_LOGE(TAG, "LoadOnBoot: NVS JSON 解析失败，清除");
        ClearInstalledModelMeta();
        return;
    }

    // 构造统一的候选列表（兼容 v1/v2）
    struct Candidate {
        std::string wakeword_id;
        std::string wake_word_text;
        std::string display;
        std::string file_md5;
        size_t file_size;
    };
    std::vector<Candidate> candidates;

    auto push_from_obj = [&](cJSON* obj) {
        cJSON* id_item   = cJSON_GetObjectItem(obj, "wakeword_id");
        cJSON* text_item = cJSON_GetObjectItem(obj, "wake_word_text");
        cJSON* disp_item = cJSON_GetObjectItem(obj, "display");
        cJSON* md5_item  = cJSON_GetObjectItem(obj, "file_md5");
        cJSON* size_item = cJSON_GetObjectItem(obj, "file_size");
        if (!id_item || !cJSON_IsString(id_item) || !text_item || !cJSON_IsString(text_item)) return;
        Candidate c;
        c.wakeword_id    = id_item->valuestring;
        c.wake_word_text = text_item->valuestring;
        c.display        = (disp_item && cJSON_IsString(disp_item)) ? disp_item->valuestring : c.wake_word_text;
        c.file_md5       = (md5_item && cJSON_IsString(md5_item)) ? md5_item->valuestring : "";
        c.file_size      = (size_item && cJSON_IsNumber(size_item)) ? (size_t)size_item->valuedouble : 0;
        candidates.push_back(std::move(c));
    };

    cJSON* list_item = cJSON_GetObjectItem(root, "list");
    if (list_item && cJSON_IsArray(list_item)) {
        cJSON* obj = nullptr;
        cJSON_ArrayForEach(obj, list_item) push_from_obj(obj);
    } else {
        // v1 格式：root 本身就是一条记录
        push_from_obj(root);
    }
    cJSON_Delete(root);

    if (candidates.empty()) {
        ESP_LOGI(TAG, "LoadOnBoot: 候选列表为空");
        ClearInstalledModelMeta();
        return;
    }

    if (!EnsureWwStoreMounted()) return;

    int ok_count = 0, fail_count = 0;
    for (const auto& c : candidates) {
        std::string path = std::string("/ww/") + c.wakeword_id + ".tflite";

        // 1) 文件存在 + 大小校验（捕获半写入文件）
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) {
            ESP_LOGW(TAG, "LoadOnBoot: 文件缺失 %s，丢弃该记录", path.c_str());
            fail_count++;
            continue;
        }
        fseek(f, 0, SEEK_END);
        size_t actual_size = (size_t)ftell(f);
        fclose(f);
        if (c.file_size > 0 && actual_size != c.file_size) {
            ESP_LOGW(TAG, "LoadOnBoot: %s 大小不符 actual=%d expected=%d，删除",
                     path.c_str(), (int)actual_size, (int)c.file_size);
            remove(path.c_str());
            fail_count++;
            continue;
        }

        // 2) MD5 完整性校验
        if (!c.file_md5.empty()) {
            std::string actual_md5 = WakeWordDownloader::ComputeFileMd5(path);
            if (actual_md5 != c.file_md5) {
                ESP_LOGW(TAG, "LoadOnBoot: %s MD5 不符 (actual=%s expected=%s)，删除",
                         path.c_str(), actual_md5.c_str(), c.file_md5.c_str());
                remove(path.c_str());
                fail_count++;
                continue;
            }
        } else {
            ESP_LOGW(TAG, "LoadOnBoot: %s 缺少 MD5 记录，跳过完整性校验", path.c_str());
        }

        // 3) 加载到引擎
        if (LoadCustomModel(c.wakeword_id, c.wake_word_text, c.display, c.file_md5, c.file_size)) {
            ok_count++;
        } else {
            fail_count++;
        }
    }

    ESP_LOGI(TAG, "LoadOnBoot: 完成（成功 %d，失败 %d，共 %u 条候选）",
             ok_count, fail_count, (unsigned)candidates.size());

    // 如果本次有记录失败，重写 NVS 以清理掉无效条目（SaveInstalledModelList 会根据当前内存列表写回）
    if (fail_count > 0) {
        SaveInstalledModelList();
    }
}
