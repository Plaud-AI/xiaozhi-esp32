#include "emotion_animation_mapper.h"
#include "esp_log.h"
#include <algorithm>
#include <cstdio>

static const char* TAG = "EmotionAnimMapper";

namespace emotion {

EmotionAnimationMapper& EmotionAnimationMapper::Instance()
{
    static EmotionAnimationMapper instance;
    return instance;
}

void EmotionAnimationMapper::Init(const std::string& anim_base_path)
{
    anim_base_path_ = anim_base_path;
    rng_.seed(rd_());
    initialized_ = true;
    
    ESP_LOGI(TAG, "EmotionAnimationMapper initialized, base path: %s", 
             anim_base_path_.c_str());
}

void EmotionAnimationMapper::RegisterAnimation(EmotionState emotion, 
                                                const std::string& animation_path,
                                                int weight, bool loop)
{
    AnimationInfo info(animation_path, weight, loop);
    
    auto& mapping = mappings_[emotion];
    mapping.emotion = emotion;
    mapping.animations.push_back(info);
    
    ESP_LOGI(TAG, "Registered: %s -> %s (weight: %d)", 
             EmotionStateToString(emotion), animation_path.c_str(), weight);
}

void EmotionAnimationMapper::RegisterAnimations(EmotionState emotion,
                                                 const std::vector<AnimationInfo>& animations,
                                                 AnimationSelectionStrategy strategy)
{
    auto& mapping = mappings_[emotion];
    mapping.emotion = emotion;
    mapping.animations = animations;
    mapping.strategy = strategy;
    
    ESP_LOGI(TAG, "Registered %d animations for %s", 
             animations.size(), EmotionStateToString(emotion));
}

int EmotionAnimationMapper::RegisterStandardMappings()
{
    if (!initialized_) {
        ESP_LOGW(TAG, "Not initialized");
        return 0;
    }

    int count = 0;

    // 标准情感到文件名的映射
    struct Mapping {
        EmotionState emotion;
        const char* filename;
        bool loop;
    };

    Mapping standard_mappings[] = {
        {EmotionState::HAPPY,      "happy.json",      false},
        {EmotionState::SAD,        "sad.json",        false},
        {EmotionState::EXCITED,    "excited.json",    false},
        {EmotionState::CALM,       "calm.json",       false},
        {EmotionState::SLEEPY,     "sleepy.json",     false},
        {EmotionState::SURPRISED,  "surprised.json",  false},
        {EmotionState::LISTENING,  "listening.json",  true},
        {EmotionState::THINKING,   "champion.json",   true},   // using champion for thinking (distinct)
        {EmotionState::SPEAKING,   "singing.json",    true},   // using singing for speaking
        {EmotionState::CONNECTING, "disdain.json",    true},   // using disdain for connecting (distinct)
        {EmotionState::BUSY,       "disgust.json",    true},   // using disgust for busy (distinct)
    };

    for (const auto& m : standard_mappings) {
        std::string path = anim_base_path_ + m.filename;
        if (FileExists(path.c_str())) {
            RegisterAnimation(m.emotion, path, 1, m.loop);
            count++;
        } else {
            ESP_LOGD(TAG, "Animation file not found: %s", path.c_str());
        }
    }

    ESP_LOGI(TAG, "Registered %d standard mappings", count);
    return count;
}

int EmotionAnimationMapper::RegisterDefaultMappings()
{
    int count = RegisterStandardMappings();
    
    // 设置全局回退动画（使用 calm 作为默认）
    std::string fallback = anim_base_path_ + "calm.json";
    if (FileExists(fallback.c_str())) {
        SetGlobalFallback(fallback);
    }
    
    return count;
}

bool EmotionAnimationMapper::GetAnimation(EmotionState emotion, AnimationInfo& out_info)
{
    auto it = mappings_.find(emotion);
    
    // 如果找不到映射
    if (it == mappings_.end() || it->second.animations.empty()) {
        ESP_LOGW(TAG, "No animation found for %s", EmotionStateToString(emotion));
        
        // 使用回退动画
        if (!global_fallback_.empty()) {
            out_info = AnimationInfo(global_fallback_);
            ESP_LOGI(TAG, "Using global fallback: %s", global_fallback_.c_str());
            return false;
        }
        
        return false;
    }

    // 根据策略选择动画
    out_info = SelectAnimation(it->second);
    
    ESP_LOGD(TAG, "Selected animation for %s: %s", 
             EmotionStateToString(emotion), out_info.file_path.c_str());
    
    return true;
}

std::vector<AnimationInfo> EmotionAnimationMapper::GetAllAnimations(EmotionState emotion) const
{
    auto it = mappings_.find(emotion);
    if (it != mappings_.end()) {
        return it->second.animations;
    }
    return std::vector<AnimationInfo>();
}

void EmotionAnimationMapper::SetSelectionStrategy(EmotionState emotion, 
                                                   AnimationSelectionStrategy strategy)
{
    mappings_[emotion].strategy = strategy;
    ESP_LOGI(TAG, "Set strategy for %s: %d", EmotionStateToString(emotion), (int)strategy);
}

void EmotionAnimationMapper::SetFallbackAnimation(EmotionState emotion, 
                                                   const std::string& fallback_path)
{
    mappings_[emotion].fallback_animation = fallback_path;
}

void EmotionAnimationMapper::SetGlobalFallback(const std::string& fallback_path)
{
    global_fallback_ = fallback_path;
    ESP_LOGI(TAG, "Set global fallback: %s", fallback_path.c_str());
}

bool EmotionAnimationMapper::HasMapping(EmotionState emotion) const
{
    auto it = mappings_.find(emotion);
    return it != mappings_.end() && !it->second.animations.empty();
}

std::vector<EmotionState> EmotionAnimationMapper::GetMappedEmotions() const
{
    std::vector<EmotionState> result;
    for (const auto& pair : mappings_) {
        if (!pair.second.animations.empty()) {
            result.push_back(pair.first);
        }
    }
    return result;
}

void EmotionAnimationMapper::ClearAllMappings()
{
    mappings_.clear();
    round_robin_index_.clear();
    ESP_LOGI(TAG, "All mappings cleared");
}

void EmotionAnimationMapper::ClearMapping(EmotionState emotion)
{
    mappings_.erase(emotion);
    round_robin_index_.erase(emotion);
}

void EmotionAnimationMapper::PrintMappings() const
{
    ESP_LOGI(TAG, "=== Emotion Animation Mappings ===");
    for (const auto& pair : mappings_) {
        ESP_LOGI(TAG, "%s: %d animation(s)", 
                 EmotionStateToString(pair.first),
                 pair.second.animations.size());
        for (const auto& anim : pair.second.animations) {
            ESP_LOGI(TAG, "  - %s (weight: %d, loop: %d)", 
                     anim.file_path.c_str(), anim.weight, anim.loop);
        }
    }
    ESP_LOGI(TAG, "================================");
}

AnimationInfo EmotionAnimationMapper::SelectAnimation(const EmotionAnimationMapping& mapping)
{
    switch (mapping.strategy) {
        case AnimationSelectionStrategy::SEQUENTIAL:
            return SelectSequential(mapping);
        case AnimationSelectionStrategy::RANDOM:
            return SelectRandom(mapping);
        case AnimationSelectionStrategy::WEIGHTED:
            return SelectWeighted(mapping);
        case AnimationSelectionStrategy::ROUND_ROBIN:
            return SelectRoundRobin(mapping);
        default:
            return SelectRandom(mapping);
    }
}

AnimationInfo EmotionAnimationMapper::SelectSequential(const EmotionAnimationMapping& mapping)
{
    return mapping.animations[0];
}

AnimationInfo EmotionAnimationMapper::SelectRandom(const EmotionAnimationMapping& mapping)
{
    std::uniform_int_distribution<size_t> dist(0, mapping.animations.size() - 1);
    size_t index = dist(rng_);
    return mapping.animations[index];
}

AnimationInfo EmotionAnimationMapper::SelectWeighted(const EmotionAnimationMapping& mapping)
{
    // 计算总权重
    int total_weight = 0;
    for (const auto& anim : mapping.animations) {
        total_weight += anim.weight;
    }

    // 随机选择
    std::uniform_int_distribution<int> dist(0, total_weight - 1);
    int random_weight = dist(rng_);

    // 找到对应的动画
    int current_weight = 0;
    for (const auto& anim : mapping.animations) {
        current_weight += anim.weight;
        if (random_weight < current_weight) {
            return anim;
        }
    }

    // 不应该到达这里，返回第一个
    return mapping.animations[0];
}

AnimationInfo EmotionAnimationMapper::SelectRoundRobin(const EmotionAnimationMapping& mapping)
{
    size_t& index = round_robin_index_[mapping.emotion];
    AnimationInfo result = mapping.animations[index];
    
    index = (index + 1) % mapping.animations.size();
    
    return result;
}

bool EmotionAnimationMapper::FileExists(const std::string& path) const
{
    FILE* f = fopen(path.c_str(), "r");
    if (f) {
        fclose(f);
        return true;
    }
    return false;
}

} // namespace emotion

