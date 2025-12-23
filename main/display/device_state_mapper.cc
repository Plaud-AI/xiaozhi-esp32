#include "device_state_mapper.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "DeviceStateMapper";

namespace emotion {

DeviceStateMapper& DeviceStateMapper::Instance()
{
    static DeviceStateMapper instance;
    return instance;
}

void DeviceStateMapper::Init()
{
    initialized_ = true;
    ESP_LOGI(TAG, "DeviceStateMapper initialized");
}

void DeviceStateMapper::RegisterMapping(const MappingRule& rule)
{
    mappings_[rule.device_state].push_back(rule);
    
    ESP_LOGI(TAG, "Registered mapping: %s -> %s", 
             DeviceStateToString(rule.device_state),
             EmotionStateToString(rule.emotion_state));
}

void DeviceStateMapper::RegisterMappings(const std::vector<MappingRule>& rules)
{
    for (const auto& rule : rules) {
        RegisterMapping(rule);
    }
    
    ESP_LOGI(TAG, "Registered %d mapping rules", rules.size());
}

int DeviceStateMapper::RegisterDefaultMappings()
{
    std::vector<MappingRule> default_rules;

    // 基础映射规则（临时实现）
    
    // 初始化状态 -> 连接中情感
    default_rules.emplace_back(
        DeviceState::BOOTING, EmotionState::CONNECTING,
        EmotionPriority::HIGH, EmotionDuration::PERSISTENT
    );
    
    default_rules.emplace_back(
        DeviceState::INITIALIZING, EmotionState::CONNECTING,
        EmotionPriority::HIGH, EmotionDuration::PERSISTENT
    );

    // 待机状态 -> 平静情感
    default_rules.emplace_back(
        DeviceState::IDLE, EmotionState::CALM,
        EmotionPriority::LOW, EmotionDuration::PERSISTENT
    );
    
    default_rules.emplace_back(
        DeviceState::SLEEPING, EmotionState::SLEEPY,
        EmotionPriority::LOW, EmotionDuration::PERSISTENT
    );

    // 唤醒 -> 兴奋情感
    default_rules.emplace_back(
        DeviceState::WAKING, EmotionState::EXCITED,
        EmotionPriority::NORMAL, EmotionDuration::TEMPORARY, 2000
    );
    
    default_rules.emplace_back(
        DeviceState::AWAKE, EmotionState::HAPPY,
        EmotionPriority::NORMAL, EmotionDuration::TEMPORARY, 1000
    );

    // 交互状态
    default_rules.emplace_back(
        DeviceState::LISTENING, EmotionState::LISTENING,
        EmotionPriority::NORMAL, EmotionDuration::PERSISTENT
    );
    
    default_rules.emplace_back(
        DeviceState::PROCESSING, EmotionState::THINKING,
        EmotionPriority::NORMAL, EmotionDuration::PERSISTENT
    );
    
    default_rules.emplace_back(
        DeviceState::SPEAKING, EmotionState::SPEAKING,
        EmotionPriority::NORMAL, EmotionDuration::PERSISTENT
    );

    // 网络状态
    default_rules.emplace_back(
        DeviceState::CONNECTING, EmotionState::CONNECTING,
        EmotionPriority::NORMAL, EmotionDuration::PERSISTENT
    );
    
    default_rules.emplace_back(
        DeviceState::CONNECTED, EmotionState::HAPPY,
        EmotionPriority::NORMAL, EmotionDuration::TEMPORARY, 2000
    );
    
    default_rules.emplace_back(
        DeviceState::DISCONNECTED, EmotionState::SAD,
        EmotionPriority::HIGH, EmotionDuration::PERSISTENT
    );

    // 更新状态
    default_rules.emplace_back(
        DeviceState::UPDATING, EmotionState::BUSY,
        EmotionPriority::HIGH, EmotionDuration::PERSISTENT
    );

    // 错误状态
    default_rules.emplace_back(
        DeviceState::ERROR, EmotionState::ERROR,
        EmotionPriority::CRITICAL, EmotionDuration::PERSISTENT
    );
    
    default_rules.emplace_back(
        DeviceState::LOW_BATTERY, EmotionState::SLEEPY,
        EmotionPriority::HIGH, EmotionDuration::PERSISTENT
    );

    // 音乐播放 -> 开心情感
    default_rules.emplace_back(
        DeviceState::PLAYING_MUSIC, EmotionState::HAPPY,
        EmotionPriority::NORMAL, EmotionDuration::PERSISTENT
    );

    // 忙碌状态
    default_rules.emplace_back(
        DeviceState::BUSY, EmotionState::BUSY,
        EmotionPriority::NORMAL, EmotionDuration::PERSISTENT
    );

    RegisterMappings(default_rules);
    
    ESP_LOGI(TAG, "Registered %d default mapping rules", default_rules.size());
    return default_rules.size();
}

EmotionState DeviceStateMapper::MapToEmotion(DeviceState device_state, 
                                              const MappingContext* context) const
{
    const MappingRule* rule = FindMatchingRule(device_state, context);
    
    if (rule) {
        return rule->emotion_state;
    }

    // 如果没有找到映射，返回中性情感
    ESP_LOGW(TAG, "No mapping found for device state: %s, using NEUTRAL", 
             DeviceStateToString(device_state));
    return EmotionState::NEUTRAL;
}

bool DeviceStateMapper::GetEmotionConfig(DeviceState device_state,
                                         const MappingContext* context,
                                         EmotionConfig& out_config,
                                         EmotionState& out_emotion) const
{
    const MappingRule* rule = FindMatchingRule(device_state, context);
    
    if (!rule) {
        return false;
    }

    // 填充配置
    out_emotion = rule->emotion_state;
    out_config.priority = rule->priority;
    out_config.duration = rule->duration;
    out_config.duration_ms = rule->duration_ms;
    out_config.allow_interrupt = true;

    return true;
}

bool DeviceStateMapper::ApplyDeviceState(DeviceState device_state,
                                         const MappingContext* context,
                                         EmotionStateManager* emotion_mgr)
{
    // 使用提供的管理器或默认单例
    auto* mgr = emotion_mgr ? emotion_mgr : &EmotionStateManager::Instance();

    // 如果有上下文更新回调，先更新上下文
    if (context_update_callback_) {
        current_context_ = context_update_callback_();
        context = &current_context_;
    } else if (context) {
        current_context_ = *context;
    }

    // 获取情感配置
    EmotionConfig config;
    EmotionState emotion;
    
    if (!GetEmotionConfig(device_state, context, config, emotion)) {
        ESP_LOGW(TAG, "Failed to get emotion config for: %s", 
                 DeviceStateToString(device_state));
        return false;
    }

    // 应用到情感管理器
    bool success = mgr->SetEmotion(emotion, config);

    if (success) {
        ESP_LOGI(TAG, "Applied device state: %s -> emotion: %s", 
                 DeviceStateToString(device_state),
                 EmotionStateToString(emotion));
    }

    return success;
}

void DeviceStateMapper::ClearAllMappings()
{
    mappings_.clear();
    ESP_LOGI(TAG, "All mappings cleared");
}

void DeviceStateMapper::ClearMapping(DeviceState device_state)
{
    mappings_.erase(device_state);
}

void DeviceStateMapper::PrintMappings() const
{
    ESP_LOGI(TAG, "=== Device State Mappings ===");
    for (const auto& pair : mappings_) {
        ESP_LOGI(TAG, "%s:", DeviceStateToString(pair.first));
        for (const auto& rule : pair.second) {
            ESP_LOGI(TAG, "  -> %s (priority: %d, duration: %d)", 
                     EmotionStateToString(rule.emotion_state),
                     (int)rule.priority,
                     (int)rule.duration);
        }
    }
    ESP_LOGI(TAG, "=============================");
}

std::vector<DeviceState> DeviceStateMapper::GetMappedStates() const
{
    std::vector<DeviceState> result;
    for (const auto& pair : mappings_) {
        result.push_back(pair.first);
    }
    return result;
}

void DeviceStateMapper::SetContextUpdateCallback(std::function<MappingContext()> callback)
{
    context_update_callback_ = callback;
}

void DeviceStateMapper::UpdateContext(const MappingContext& context)
{
    current_context_ = context;
}

const MappingRule* DeviceStateMapper::FindMatchingRule(DeviceState device_state, 
                                                       const MappingContext* context) const
{
    auto it = mappings_.find(device_state);
    if (it == mappings_.end() || it->second.empty()) {
        return nullptr;
    }

    // 如果只有一个规则，直接返回
    if (it->second.size() == 1) {
        return &it->second[0];
    }

    // 如果有多个规则，找到第一个符合条件的
    for (const auto& rule : it->second) {
        if (rule.condition) {
            // 如果有条件函数，检查条件
            if (context && rule.condition(*context)) {
                return &rule;
            }
        } else {
            // 没有条件函数，直接返回
            return &rule;
        }
    }

    // 如果所有条件都不满足，返回第一个规则
    return &it->second[0];
}

} // namespace emotion

