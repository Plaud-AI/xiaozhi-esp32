#include "emotion_mapper.h"
#include <esp_log.h>
#include <algorithm>

static const char* TAG = "EmotionMapper";

namespace xiaozhi {
namespace display {

EmotionMapper& EmotionMapper::GetInstance() {
    static EmotionMapper instance;
    return instance;
}

EmotionMapper::EmotionMapper()
    : initialized_(false)
    , on_emotion_change_(nullptr)
    , llm_parser_(nullptr) {
}

bool EmotionMapper::Initialize() {
    if (initialized_) {
        ESP_LOGW(TAG, "EmotionMapper already initialized");
        return true;
    }
    
    ESP_LOGI(TAG, "Initializing EmotionMapper...");
    
    InitializeDefaultMappings();
    
    initialized_ = true;
    ESP_LOGI(TAG, "EmotionMapper initialized with %zu emotions", emotion_map_.size());
    
    return true;
}

void EmotionMapper::InitializeDefaultMappings() {
    ESP_LOGI(TAG, "Registering default emotion mappings...");
    
    // ========================================
    // 基础状态类（映射到 idle.aaf）
    // ========================================
    RegisterEmotion("neutral", "idle.aaf", 0, true, 15, 5000, "默认", "basic");
    RegisterEmotion("calm", "idle.aaf", 0, true, 15, 5000, "平静", "basic");
    RegisterEmotion("blink", "idle.aaf", 0, true, 15, 5000, "眨眼", "basic");
    RegisterEmotion("idle", "idle.aaf", 0, true, 15, 5000, "待机", "basic");
    
    // ========================================
    // 倾听类（映射到 listening.aaf）
    // ========================================
    RegisterEmotion("listening", "listening.aaf", 1, true, 20, 0, "倾听", "attention");
    RegisterEmotion("curious", "listening.aaf", 1, true, 20, 5000, "好奇", "attention");
    RegisterEmotion("interested", "listening.aaf", 1, true, 20, 5000, "感兴趣", "attention");
    RegisterEmotion("attentive", "listening.aaf", 1, true, 20, 0, "专注", "attention");
    
    // ========================================
    // 说话类（映射到 speaking.aaf）
    // ========================================
    RegisterEmotion("speaking", "speaking.aaf", 2, true, 20, 0, "说话", "communication");
    RegisterEmotion("talking", "speaking.aaf", 2, true, 20, 0, "交谈", "communication");
    RegisterEmotion("explaining", "speaking.aaf", 2, true, 20, 0, "解释", "communication");
    
    // ========================================
    // 思考类（映射到 loading.aaf）
    // ========================================
    RegisterEmotion("thinking", "loading.aaf", 3, true, 15, 5000, "思考", "cognitive");
    RegisterEmotion("loading", "loading.aaf", 3, true, 15, 0, "加载", "cognitive");
    RegisterEmotion("processing", "loading.aaf", 3, true, 15, 5000, "处理中", "cognitive");
    RegisterEmotion("pondering", "loading.aaf", 3, true, 15, 5000, "沉思", "cognitive");
    RegisterEmotion("confused", "loading.aaf", 3, true, 15, 5000, "困惑", "cognitive");
    
    // ========================================
    // 积极情感（映射到 success.aaf）
    // ========================================
    RegisterEmotion("happy", "success.aaf", 6, false, 15, 5000, "开心", "positive");
    RegisterEmotion("excited", "success.aaf", 6, false, 15, 5000, "兴奋", "positive");
    RegisterEmotion("joyful", "success.aaf", 6, false, 15, 5000, "喜悦", "positive");
    RegisterEmotion("success", "success.aaf", 6, false, 15, 3000, "成功", "positive");
    RegisterEmotion("proud", "success.aaf", 6, false, 15, 5000, "自豪", "positive");
    RegisterEmotion("love", "success.aaf", 6, false, 15, 5000, "喜爱", "positive");
    RegisterEmotion("grateful", "success.aaf", 6, false, 15, 5000, "感激", "positive");
    
    // ========================================
    // 消极情感（映射到 error.aaf）
    // ========================================
    RegisterEmotion("sad", "error.aaf", 7, true, 15, 5000, "悲伤", "negative");
    RegisterEmotion("angry", "error.aaf", 7, true, 15, 5000, "生气", "negative");
    RegisterEmotion("frustrated", "error.aaf", 7, true, 15, 5000, "沮丧", "negative");
    RegisterEmotion("disappointed", "error.aaf", 7, true, 15, 5000, "失望", "negative");
    RegisterEmotion("error", "error.aaf", 7, true, 15, 3000, "错误", "negative");
    RegisterEmotion("worried", "error.aaf", 7, true, 15, 5000, "担忧", "negative");
    RegisterEmotion("anxious", "error.aaf", 7, true, 15, 5000, "焦虑", "negative");
    
    // ========================================
    // 惊讶/特殊（映射到 settings.aaf）
    // ========================================
    RegisterEmotion("surprised", "settings.aaf", 4, true, 15, 3000, "惊讶", "special");
    RegisterEmotion("shocked", "settings.aaf", 4, true, 15, 3000, "震惊", "special");
    RegisterEmotion("amazed", "settings.aaf", 4, true, 15, 3000, "惊叹", "special");
    RegisterEmotion("sleepy", "settings.aaf", 4, true, 15, 5000, "困倦", "special");
    RegisterEmotion("tired", "settings.aaf", 4, true, 15, 5000, "疲惫", "special");
    RegisterEmotion("bored", "settings.aaf", 4, true, 15, 5000, "无聊", "special");
    
    // ========================================
    // 更新状态（映射到 updating.aaf）
    // ========================================
    RegisterEmotion("updating", "updating.aaf", 5, true, 15, 0, "更新中", "system");
    RegisterEmotion("downloading", "updating.aaf", 5, true, 15, 0, "下载中", "system");
    RegisterEmotion("syncing", "updating.aaf", 5, true, 15, 0, "同步中", "system");
    
    ESP_LOGI(TAG, "Registered %zu default emotion mappings", emotion_map_.size());
}

void EmotionMapper::RegisterEmotion(const std::string& emotion,
                                     const std::string& animation,
                                     int index,
                                     bool loop,
                                     int fps,
                                     uint32_t timeout_ms,
                                     const std::string& display_name,
                                     const std::string& category) {
    EmotionInfo info;
    info.name = emotion;
    info.display_name = display_name.empty() ? emotion : display_name;
    info.category = category;
    info.mapped_animation = animation;
    info.animation_index = index;
    info.is_loop = loop;
    info.fps = fps;
    info.default_timeout_ms = timeout_ms;
    
    emotion_map_[emotion] = info;
    
    ESP_LOGD(TAG, "Registered emotion: %s -> %s (index=%d, fps=%d, timeout=%lu)",
             emotion.c_str(), animation.c_str(), index, fps, timeout_ms);
}

EmotionMappingResult EmotionMapper::MapEmotion(const std::string& emotion, InputSource source) {
    EmotionMappingResult result;
    
    if (!initialized_) {
        ESP_LOGW(TAG, "EmotionMapper not initialized, initializing now...");
        Initialize();
    }
    
    ESP_LOGI(TAG, "MapEmotion: emotion='%s', source=%s", 
             emotion.c_str(), InputSourceToString(source));
    
    auto it = emotion_map_.find(emotion);
    if (it == emotion_map_.end()) {
        ESP_LOGW(TAG, "Emotion '%s' not found in mapping table", emotion.c_str());
        result.success = false;
        return result;
    }
    
    const EmotionInfo& info = it->second;
    result.animation_name = info.mapped_animation;
    result.animation_index = info.animation_index;
    result.is_loop = info.is_loop;
    result.fps = info.fps;
    result.timeout_ms = info.default_timeout_ms;
    result.success = true;
    
    ESP_LOGI(TAG, "Emotion '%s' mapped to animation '%s' (index=%d, loop=%d, fps=%d, timeout=%lu)",
             emotion.c_str(), result.animation_name.c_str(),
             result.animation_index, result.is_loop, result.fps, result.timeout_ms);
    
    // 触发回调
    if (on_emotion_change_) {
        on_emotion_change_(emotion, source, result);
    }
    
    return result;
}

bool EmotionMapper::IsEmotionRegistered(const std::string& emotion) const {
    return emotion_map_.find(emotion) != emotion_map_.end();
}

std::vector<std::string> EmotionMapper::GetRegisteredEmotions() const {
    std::vector<std::string> emotions;
    emotions.reserve(emotion_map_.size());
    
    for (const auto& pair : emotion_map_) {
        emotions.push_back(pair.first);
    }
    
    // 排序以保持一致性
    std::sort(emotions.begin(), emotions.end());
    
    return emotions;
}

EmotionMapper::EmotionInfo EmotionMapper::GetEmotionInfo(const std::string& emotion) const {
    auto it = emotion_map_.find(emotion);
    if (it != emotion_map_.end()) {
        return it->second;
    }
    return EmotionInfo();
}

std::vector<std::string> EmotionMapper::GetCategories() const {
    std::vector<std::string> categories;
    std::unordered_map<std::string, bool> seen;
    
    for (const auto& pair : emotion_map_) {
        if (seen.find(pair.second.category) == seen.end()) {
            categories.push_back(pair.second.category);
            seen[pair.second.category] = true;
        }
    }
    
    std::sort(categories.begin(), categories.end());
    return categories;
}

std::vector<std::string> EmotionMapper::GetEmotionsByCategory(const std::string& category) const {
    std::vector<std::string> emotions;
    
    for (const auto& pair : emotion_map_) {
        if (pair.second.category == category) {
            emotions.push_back(pair.first);
        }
    }
    
    std::sort(emotions.begin(), emotions.end());
    return emotions;
}

void EmotionMapper::SetEmotionChangeCallback(EmotionChangeCallback callback) {
    on_emotion_change_ = callback;
}

std::string EmotionMapper::ParseEmotionFromLLM(const std::string& llm_response) {
    // 如果设置了自定义解析器，使用它
    if (llm_parser_) {
        return llm_parser_(llm_response);
    }
    
    // 默认实现：简单的关键词匹配
    // 这是一个非常基础的实现，未来可以替换为更智能的解析逻辑
    
    // 转换为小写进行匹配
    std::string lower_response = llm_response;
    std::transform(lower_response.begin(), lower_response.end(), 
                   lower_response.begin(), ::tolower);
    
    // 按优先级检查关键词
    // 注意：这里的顺序很重要，更具体的情感应该先检查
    
    // 积极情感
    if (lower_response.find("excited") != std::string::npos ||
        lower_response.find("兴奋") != std::string::npos) {
        return "excited";
    }
    if (lower_response.find("happy") != std::string::npos ||
        lower_response.find("开心") != std::string::npos ||
        lower_response.find("高兴") != std::string::npos) {
        return "happy";
    }
    if (lower_response.find("joyful") != std::string::npos ||
        lower_response.find("喜悦") != std::string::npos) {
        return "joyful";
    }
    
    // 消极情感
    if (lower_response.find("angry") != std::string::npos ||
        lower_response.find("生气") != std::string::npos ||
        lower_response.find("愤怒") != std::string::npos) {
        return "angry";
    }
    if (lower_response.find("sad") != std::string::npos ||
        lower_response.find("悲伤") != std::string::npos ||
        lower_response.find("伤心") != std::string::npos) {
        return "sad";
    }
    if (lower_response.find("worried") != std::string::npos ||
        lower_response.find("担忧") != std::string::npos ||
        lower_response.find("担心") != std::string::npos) {
        return "worried";
    }
    
    // 惊讶
    if (lower_response.find("surprised") != std::string::npos ||
        lower_response.find("惊讶") != std::string::npos ||
        lower_response.find("吃惊") != std::string::npos) {
        return "surprised";
    }
    
    // 思考
    if (lower_response.find("thinking") != std::string::npos ||
        lower_response.find("思考") != std::string::npos ||
        lower_response.find("考虑") != std::string::npos) {
        return "thinking";
    }
    if (lower_response.find("confused") != std::string::npos ||
        lower_response.find("困惑") != std::string::npos) {
        return "confused";
    }
    
    // 好奇
    if (lower_response.find("curious") != std::string::npos ||
        lower_response.find("好奇") != std::string::npos) {
        return "curious";
    }
    
    // 疲惫
    if (lower_response.find("tired") != std::string::npos ||
        lower_response.find("疲惫") != std::string::npos ||
        lower_response.find("累") != std::string::npos) {
        return "tired";
    }
    if (lower_response.find("sleepy") != std::string::npos ||
        lower_response.find("困") != std::string::npos) {
        return "sleepy";
    }
    
    // 默认返回空，表示无法解析
    ESP_LOGD(TAG, "Could not parse emotion from LLM response");
    return "";
}

void EmotionMapper::SetLLMEmotionParser(LLMEmotionParser parser) {
    llm_parser_ = parser;
    ESP_LOGI(TAG, "Custom LLM emotion parser set");
}

} // namespace display
} // namespace xiaozhi

