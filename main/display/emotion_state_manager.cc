#include "emotion_state_manager.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "EmotionStateMgr";

namespace emotion {

EmotionStateManager& EmotionStateManager::Instance()
{
    static EmotionStateManager instance;
    return instance;
}

void EmotionStateManager::Init(EmotionState default_emotion)
{
    default_emotion_ = default_emotion;
    current_emotion_ = default_emotion;
    previous_emotion_ = default_emotion;
    current_priority_ = EmotionPriority::NORMAL;
    initialized_ = true;
    auto_restore_enabled_ = true;
    total_switches_ = 0;
    current_state_start_time_ = esp_timer_get_time() / 1000;
    
    // 创建自动恢复定时器
    auto_restore_timer_ = xTimerCreate(
        "emotion_restore",
        pdMS_TO_TICKS(1000),
        pdFALSE,  // 不自动重载
        this,
        OnAutoRestoreTimer
    );
    
    ESP_LOGI(TAG, "EmotionStateManager initialized, default: %s", 
             EmotionStateToString(default_emotion));
}

bool EmotionStateManager::SetEmotion(EmotionState emotion, const EmotionConfig& config)
{
    if (!initialized_) {
        ESP_LOGW(TAG, "Not initialized");
        return false;
    }

    // 检查是否可以切换
    if (!CanSwitchTo(emotion, config.priority)) {
        ESP_LOGD(TAG, "Cannot switch to %s (priority blocked)", 
                 EmotionStateToString(emotion));
        return false;
    }

    // 取消之前的自动恢复定时器
    CancelAutoRestore();

    // 保存之前的状态
    previous_emotion_ = current_emotion_;
    
    // 更新当前状态
    current_emotion_ = emotion;
    current_priority_ = config.priority;
    current_config_ = config;
    current_state_start_time_ = esp_timer_get_time() / 1000;

    // 触发状态变化回调
    TriggerStateChange(previous_emotion_, current_emotion_, false);

    // 统计
    emotion_count_[emotion]++;
    total_switches_++;

    // 如果是临时情感，设置自动恢复
    if (config.duration == EmotionDuration::TEMPORARY && config.duration_ms > 0) {
        ScheduleAutoRestore();
    }

    ESP_LOGI(TAG, "Emotion: %s -> %s (priority: %d)", 
             EmotionStateToString(previous_emotion_),
             EmotionStateToString(current_emotion_),
             (int)config.priority);

    return true;
}

bool EmotionStateManager::SetTemporaryEmotion(EmotionState emotion, int duration_ms, 
                                               EmotionPriority priority)
{
    EmotionConfig config;
    config.priority = priority;
    config.duration = EmotionDuration::TEMPORARY;
    config.duration_ms = duration_ms;
    config.allow_interrupt = true;

    return SetEmotion(emotion, config);
}

void EmotionStateManager::ForceSetEmotion(EmotionState emotion, const EmotionConfig& config)
{
    CancelAutoRestore();
    
    previous_emotion_ = current_emotion_;
    current_emotion_ = emotion;
    current_priority_ = config.priority;
    current_config_ = config;
    current_state_start_time_ = esp_timer_get_time() / 1000;

    TriggerStateChange(previous_emotion_, current_emotion_, false);

    emotion_count_[emotion]++;
    total_switches_++;

    if (config.duration == EmotionDuration::TEMPORARY && config.duration_ms > 0) {
        ScheduleAutoRestore();
    }

    ESP_LOGI(TAG, "Force emotion: %s -> %s", 
             EmotionStateToString(previous_emotion_),
             EmotionStateToString(current_emotion_));
}

bool EmotionStateManager::RestorePreviousEmotion()
{
    if (previous_emotion_ == current_emotion_) {
        ESP_LOGD(TAG, "No previous emotion to restore");
        return false;
    }

    ESP_LOGI(TAG, "Restoring previous emotion: %s", 
             EmotionStateToString(previous_emotion_));

    EmotionConfig config;
    config.priority = EmotionPriority::NORMAL;
    config.duration = EmotionDuration::PERSISTENT;

    return SetEmotion(previous_emotion_, config);
}

void EmotionStateManager::ResetToDefault()
{
    ESP_LOGI(TAG, "Resetting to default emotion: %s", 
             EmotionStateToString(default_emotion_));

    EmotionConfig config;
    config.priority = EmotionPriority::NORMAL;
    config.duration = EmotionDuration::PERSISTENT;

    ForceSetEmotion(default_emotion_, config);
}

void EmotionStateManager::SetStateChangeCallback(
    std::function<void(const EmotionChangeEvent&)> callback)
{
    state_change_callback_ = callback;
}

std::vector<EmotionChangeEvent> EmotionStateManager::GetHistory(int max_count) const
{
    std::vector<EmotionChangeEvent> result;
    
    int count = std::min(max_count, (int)history_.size());
    if (count > 0) {
        result.assign(history_.end() - count, history_.end());
    }
    
    return result;
}

void EmotionStateManager::ClearHistory()
{
    history_.clear();
    ESP_LOGI(TAG, "History cleared");
}

bool EmotionStateManager::CanSwitchTo(EmotionState emotion, EmotionPriority priority) const
{
    // 如果当前状态不允许打断
    if (!current_config_.allow_interrupt && priority <= current_priority_) {
        return false;
    }

    // 如果新情感的优先级低于当前优先级
    if (priority < current_priority_) {
        return false;
    }

    return true;
}

EmotionStateManager::Statistics EmotionStateManager::GetStatistics() const
{
    Statistics stats;
    stats.total_switches = total_switches_;
    
    // 找出最常用的情感
    int max_count = 0;
    for (const auto& pair : emotion_count_) {
        if (pair.second > max_count) {
            max_count = pair.second;
            stats.most_used = pair.first;
        }
    }
    
    // 计算当前状态持续时长
    uint32_t now = esp_timer_get_time() / 1000;
    stats.current_duration_ms = now - current_state_start_time_;
    
    return stats;
}

void EmotionStateManager::TriggerStateChange(EmotionState from, EmotionState to, 
                                              bool is_auto_restore)
{
    // 创建事件
    EmotionChangeEvent event;
    event.from_state = from;
    event.to_state = to;
    event.priority = current_priority_;
    event.timestamp = esp_timer_get_time() / 1000;
    event.is_auto_restore = is_auto_restore;

    // 添加到历史记录
    history_.push_back(event);
    if (history_.size() > MAX_HISTORY_SIZE) {
        history_.erase(history_.begin());
    }

    // 调用回调
    if (state_change_callback_) {
        state_change_callback_(event);
    }
}

void EmotionStateManager::ScheduleAutoRestore()
{
    if (!auto_restore_enabled_ || !auto_restore_timer_) {
        return;
    }

    int delay_ms = current_config_.duration_ms;
    if (delay_ms <= 0) {
        delay_ms = 3000;  // 默认 3 秒
    }

    xTimerChangePeriod(auto_restore_timer_, pdMS_TO_TICKS(delay_ms), 100);
    xTimerStart(auto_restore_timer_, 100);

    ESP_LOGD(TAG, "Auto restore scheduled in %d ms", delay_ms);
}

void EmotionStateManager::CancelAutoRestore()
{
    if (auto_restore_timer_) {
        xTimerStop(auto_restore_timer_, 0);
    }
}

void EmotionStateManager::OnAutoRestoreTimer(TimerHandle_t timer)
{
    auto* mgr = static_cast<EmotionStateManager*>(pvTimerGetTimerID(timer));
    if (!mgr) return;

    ESP_LOGI(TAG, "Auto restore triggered");
    mgr->RestorePreviousEmotion();
}

} // namespace emotion

