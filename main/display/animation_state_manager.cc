#include "animation_state_manager.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <algorithm>

static const char* TAG = "AnimStateMgr";

namespace xiaozhi {
namespace display {

AnimationStateManager::AnimationStateManager() {
    current_state_.type = StateType::Unknown;
}

bool AnimationStateManager::SetDeviceState(DeviceState state) {
    return SetDeviceStateWithPriority(state, Priority::Normal, false);
}

bool AnimationStateManager::SetEmotionState(const char* emotion) {
    return SetEmotionStateWithPriority(emotion, Priority::High, false, 5000);
}

bool AnimationStateManager::SetDeviceStateWithPriority(DeviceState state, Priority priority, bool force) {
    auto it = device_state_map_.find(state);
    if (it == device_state_map_.end()) {
        ESP_LOGW(TAG, "Device state not registered: %d", static_cast<int>(state));
        return false;
    }
    
    StateChangeEvent event;
    event.type = StateType::Device;
    event.state_name = it->second.name;
    event.animation_file = it->second.animation_file;
    event.animation_index = it->second.animation_index;
    event.loop = it->second.loop;
    event.fps = it->second.fps;
    event.priority = priority;
    event.force_interrupt = force;
    event.timeout_ms = 0;  // 设备状态永久有效
    
    return RequestStateChange(event);
}

bool AnimationStateManager::SetEmotionStateWithPriority(const char* emotion, Priority priority, 
                                                         bool force, uint32_t timeout_ms) {
    if (!emotion) {
        ESP_LOGE(TAG, "Invalid emotion name");
        return false;
    }
    
    auto it = emotion_map_.find(emotion);
    if (it == emotion_map_.end()) {
        ESP_LOGW(TAG, "Emotion not registered: %s", emotion);
        return false;
    }
    
    StateChangeEvent event;
    event.type = StateType::Emotion;
    event.state_name = it->second.name;
    event.animation_file = it->second.animation_file;
    event.animation_index = it->second.animation_index;
    event.loop = it->second.loop;
    event.fps = it->second.fps;
    event.priority = priority;
    event.force_interrupt = force;
    event.timeout_ms = timeout_ms;  // 感情状态超时自动恢复
    
    return RequestStateChange(event);
}

bool AnimationStateManager::RequestStateChange(const StateChangeEvent& event) {
    ESP_LOGD(TAG, "Request state change: type=%d, name=%s, priority=%d, force=%d",
             static_cast<int>(event.type), event.state_name.c_str(),
             static_cast<int>(event.priority), event.force_interrupt);
    
    // 检查是否可以打断
    if (!CheckPriorityAndInterrupt(event.priority, event.force_interrupt)) {
        ESP_LOGW(TAG, "State change rejected: priority too low (%d < %d)",
                 static_cast<int>(event.priority),
                 static_cast<int>(current_state_.priority));
        return false;
    }
    
    // 保存当前状态到历史
    if (current_state_.type != StateType::Unknown) {
        PushStateHistory(current_state_);
    }
    
    // 更新当前状态
    current_state_ = ConvertEventToState(event);
    current_state_.timestamp = GetCurrentTimestamp();
    
    ESP_LOGI(TAG, "State changed to: %s (type=%d, priority=%d, timeout=%lu ms)",
             current_state_.name.c_str(),
             static_cast<int>(current_state_.type),
             static_cast<int>(current_state_.priority),
             current_state_.timeout_ms);
    
    // 触发回调
    OnStateChanged(current_state_);
    
    return true;
}

AnimationStateManager::StateInfo AnimationStateManager::GetPreviousState() const {
    if (state_history_.empty()) {
        ESP_LOGW(TAG, "No previous state available");
        return StateInfo();  // 返回默认构造的 StateInfo
    }
    
    // 返回历史栈中的最后一个状态
    return state_history_.back();
}

bool AnimationStateManager::CanInterrupt(Priority new_priority, bool force) const {
    if (force) {
        return true;
    }
    
    // 优先级比较
    if (new_priority > current_state_.priority) {
        return true;
    }
    
    // 优先级相同时，感情状态可以打断设备状态
    if (new_priority == current_state_.priority) {
        // 这里可以添加更细致的规则
        return false;
    }
    
    return false;
}

bool AnimationStateManager::RestorePreviousState() {
    if (state_history_.empty()) {
        ESP_LOGW(TAG, "No previous state to restore");
        return false;
    }
    
    StateInfo prev_state = PopStateHistory();
    
    ESP_LOGI(TAG, "Restoring previous state: %s", prev_state.name.c_str());
    
    // 构造恢复事件
    StateChangeEvent event;
    event.type = prev_state.type;
    event.state_name = prev_state.name;
    event.animation_file = prev_state.animation_file;
    event.animation_index = prev_state.animation_index;
    event.loop = prev_state.loop;
    event.fps = prev_state.fps;
    event.priority = prev_state.priority;
    event.force_interrupt = true;  // 恢复时强制切换
    event.timeout_ms = prev_state.timeout_ms;
    
    // 更新当前状态（不保存到历史，因为是恢复操作）
    current_state_ = prev_state;
    current_state_.timestamp = GetCurrentTimestamp();
    
    // 触发回调
    OnStateChanged(current_state_);
    
    return true;
}

void AnimationStateManager::RegisterDeviceStateMapping(DeviceState state, 
                                                        const char* anim_file,
                                                        int anim_index,
                                                        bool loop, 
                                                        int fps,
                                                        Priority default_priority) {
    StateInfo info;
    info.type = StateType::Device;
    info.name = GetDeviceStateName(state);
    info.animation_file = anim_file ? anim_file : "";
    info.animation_index = anim_index;
    info.loop = loop;
    info.fps = fps;
    info.priority = default_priority;
    info.timeout_ms = 0;  // 设备状态永久有效
    
    device_state_map_[state] = info;
    
    ESP_LOGD(TAG, "Registered device state: %s -> %s (index=%d, fps=%d, priority=%d)",
             info.name.c_str(), anim_file, anim_index, fps, static_cast<int>(default_priority));
}

void AnimationStateManager::RegisterEmotionMapping(const char* emotion,
                                                    const char* anim_file,
                                                    int anim_index,
                                                    bool loop, 
                                                    int fps,
                                                    Priority default_priority) {
    if (!emotion) {
        ESP_LOGE(TAG, "Invalid emotion name");
        return;
    }
    
    StateInfo info;
    info.type = StateType::Emotion;
    info.name = emotion;
    info.animation_file = anim_file ? anim_file : "";
    info.animation_index = anim_index;
    info.loop = loop;
    info.fps = fps;
    info.priority = default_priority;
    info.timeout_ms = 5000;  // 默认 5 秒超时
    
    emotion_map_[emotion] = info;
    
    ESP_LOGD(TAG, "Registered emotion: %s -> %s (index=%d, fps=%d, priority=%d)",
             emotion, anim_file, anim_index, fps, static_cast<int>(default_priority));
}

const char* AnimationStateManager::GetDeviceStateName(DeviceState state) {
    switch (state) {
        case DeviceState::Unknown:   return "Unknown";
        case DeviceState::Idle:      return "Idle";
        case DeviceState::Listening: return "Listening";
        case DeviceState::Speaking:  return "Speaking";
        case DeviceState::Loading:   return "Loading";
        case DeviceState::Settings:  return "Settings";
        case DeviceState::Updating:  return "Updating";
        case DeviceState::Success:   return "Success";
        case DeviceState::Error:     return "Error";
        default:                     return "Unknown";
    }
}

void AnimationStateManager::PushStateHistory(const StateInfo& state) {
    state_history_.push_back(state);
    
    // 限制历史记录数量
    const size_t MAX_HISTORY = 10;
    if (state_history_.size() > MAX_HISTORY) {
        state_history_.erase(state_history_.begin());
    }
}

AnimationStateManager::StateInfo AnimationStateManager::PopStateHistory() {
    if (state_history_.empty()) {
        return StateInfo();
    }
    
    StateInfo state = state_history_.back();
    state_history_.pop_back();
    return state;
}

bool AnimationStateManager::CheckPriorityAndInterrupt(Priority new_priority, bool force) {
    // 强制打断
    if (force) {
        return true;
    }
    
    // 优先级比较
    if (new_priority > current_state_.priority) {
        return true;
    }
    
    // 优先级相同时的特殊规则
    if (new_priority == current_state_.priority) {
        // 可以添加更细致的规则，例如：
        // - 同优先级的感情状态可以互相打断
        // - 同优先级的设备状态不能互相打断
        return false;
    }
    
    return false;
}

AnimationStateManager::StateInfo AnimationStateManager::ConvertEventToState(const StateChangeEvent& event) {
    StateInfo state;
    state.type = event.type;
    state.name = event.state_name;
    state.animation_file = event.animation_file;
    state.animation_index = event.animation_index;
    state.loop = event.loop;
    state.fps = event.fps;
    state.priority = event.priority;
    state.timeout_ms = event.timeout_ms;
    state.timestamp = 0;  // 将在外部设置
    return state;
}

void AnimationStateManager::OnStateChanged(const StateInfo& state) {
    if (state_change_callback_) {
        StateChangeEvent event;
        event.type = state.type;
        event.state_name = state.name;
        event.animation_file = state.animation_file;
        event.animation_index = state.animation_index;
        event.loop = state.loop;
        event.fps = state.fps;
        event.priority = state.priority;
        event.force_interrupt = false;
        event.timeout_ms = state.timeout_ms;
        
        state_change_callback_(event);
    }
}

uint32_t AnimationStateManager::GetCurrentTimestamp() const {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);  // 转换为毫秒
}

} // namespace display
} // namespace xiaozhi

