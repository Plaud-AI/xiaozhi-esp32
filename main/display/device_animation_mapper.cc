#include "device_animation_mapper.h"
#include <esp_log.h>

static const char* TAG = "DeviceAnimMapper";

namespace display {

DeviceAnimationMapper& DeviceAnimationMapper::GetInstance() {
    static DeviceAnimationMapper instance;
    return instance;
}

void DeviceAnimationMapper::Init(const std::string& anim_base_path) {
    anim_base_path_ = anim_base_path;
    
    // 确保路径以 '/' 结尾
    if (!anim_base_path_.empty() && anim_base_path_.back() != '/') {
        anim_base_path_ += '/';
    }
    
    initialized_ = true;
    ESP_LOGI(TAG, "DeviceAnimationMapper initialized, base path: %s", anim_base_path_.c_str());
}

void DeviceAnimationMapper::RegisterMapping(DeviceState state, 
                                             const std::string& animation_file, 
                                             bool loop) {
    std::string full_path = anim_base_path_ + animation_file;
    mappings_[state] = AnimationConfig(full_path, loop);
    
    ESP_LOGI(TAG, "Registered: %d -> %s (loop=%d)", 
             static_cast<int>(state), animation_file.c_str(), loop);
}

int DeviceAnimationMapper::RegisterDefaultMappings() {
    if (!initialized_) {
        ESP_LOGW(TAG, "Not initialized");
        return 0;
    }

    // 默认映射规则：设备状态 → 动画文件
    // 根据设计文档的映射方案
    
    RegisterMapping(kDeviceStateUnknown,         "idle.json",     true);   // 待机
    RegisterMapping(kDeviceStateStarting,        "loading.json",  true);   // 启动加载
    RegisterMapping(kDeviceStateWifiConfiguring, "settings.json", true);   // WiFi配网
    RegisterMapping(kDeviceStateIdle,            "idle.json",     true);   // 待机呼吸
    RegisterMapping(kDeviceStateConnecting,      "loading.json",  true);   // 连接加载
    RegisterMapping(kDeviceStateListening,       "listening.json",true);   // 倾听声波
    RegisterMapping(kDeviceStateSpeaking,        "speaking.json", true);   // 说话张合
    RegisterMapping(kDeviceStateUpgrading,       "updating.json", true);   // 升级下载
    RegisterMapping(kDeviceStateActivating,      "loading.json",  true);   // 激活加载
    RegisterMapping(kDeviceStateAudioTesting,    "settings.json", true);   // 音频测试
    RegisterMapping(kDeviceStateFatalError,      "error.json",    true);   // 错误警告

    int count = mappings_.size();
    ESP_LOGI(TAG, "Registered %d default mappings", count);
    
    return count;
}

bool DeviceAnimationMapper::GetAnimationConfig(DeviceState state, 
                                                AnimationConfig& out_config) const {
    auto it = mappings_.find(state);
    if (it != mappings_.end()) {
        out_config = it->second;
        return true;
    }
    
    // 未找到映射，使用 idle.json 作为默认
    ESP_LOGW(TAG, "No mapping for state %d, using idle.json as fallback", 
             static_cast<int>(state));
    out_config = AnimationConfig(anim_base_path_ + "idle.json", true);
    return false;
}

std::string DeviceAnimationMapper::GetAnimationPath(DeviceState state) const {
    AnimationConfig config;
    GetAnimationConfig(state, config);
    return config.file_path;
}

void DeviceAnimationMapper::ClearAllMappings() {
    mappings_.clear();
    ESP_LOGI(TAG, "All mappings cleared");
}

void DeviceAnimationMapper::PrintMappings() const {
    ESP_LOGI(TAG, "=== Device State to Animation Mappings ===");
    
    const char* state_names[] = {
        "Unknown", "Starting", "WifiConfiguring", "Idle", "Connecting",
        "Listening", "Speaking", "Upgrading", "Activating", 
        "AudioTesting", "FatalError"
    };
    
    for (const auto& pair : mappings_) {
        int state_idx = static_cast<int>(pair.first);
        const char* state_name = (state_idx >= 0 && state_idx < 11) ? 
                                 state_names[state_idx] : "Unknown";
        
        ESP_LOGI(TAG, "  %s (%d) → %s (loop=%d)", 
                 state_name, state_idx,
                 pair.second.file_path.c_str(),
                 pair.second.loop);
    }
    
    ESP_LOGI(TAG, "==========================================");
}

} // namespace display

