#pragma once

#include <string>
#include <unordered_map>

namespace xiaozhi {
namespace display {

// 动画文件名映射（设备状态 → 动画文件）
struct AnimationFileMapping {
    static const std::unordered_map<std::string, std::string> device_states;
    static const std::unordered_map<std::string, std::string> emotion_states;
};

// 设备状态动画映射
const std::unordered_map<std::string, std::string> AnimationFileMapping::device_states = {
    // 8 个核心设备状态
    {"idle",       "idle.aaf"},       // 待机：慢眨眼
    {"listening",  "listening.aaf"},  // 倾听：快眨眼
    {"speaking",   "speaking.aaf"},   // 说话：开心
    {"loading",    "loading.aaf"},    // 加载：眩晕
    {"settings",   "settings.aaf"},   // 设置：睡眠
    {"updating",   "updating.aaf"},   // 更新：眨眼
    {"success",    "success.aaf"},    // 成功：开心
    {"error",      "error.aaf"},      // 错误：悲伤
};

// 情感状态动画映射（暂时为空，后续扩展）
const std::unordered_map<std::string, std::string> AnimationFileMapping::emotion_states = {
    // 将来可以添加更多情感动画
};

// 动画配置
struct AnimationConfig {
    // 动画文件路径前缀
    static constexpr const char* ANIMATION_PATH_PREFIX = "/assets/animations/";
    
    // 默认 FPS
    static constexpr int DEFAULT_FPS = 25;
    
    // 设备状态默认 FPS
    struct DeviceStateFps {
        static constexpr int IDLE = 15;       // 待机：慢速
        static constexpr int LISTENING = 30;  // 倾听：快速
        static constexpr int SPEAKING = 25;   // 说话：中速
        static constexpr int LOADING = 30;    // 加载：快速
        static constexpr int SETTINGS = 20;   // 设置：中速
        static constexpr int UPDATING = 20;   // 更新：中速
        static constexpr int SUCCESS = 30;    // 成功：快速
        static constexpr int ERROR = 25;      // 错误：中速
    };
    
    // 根据设备状态名称获取推荐 FPS
    static int GetRecommendedFps(const std::string& state_name) {
        if (state_name == "idle") return DeviceStateFps::IDLE;
        if (state_name == "listening") return DeviceStateFps::LISTENING;
        if (state_name == "speaking") return DeviceStateFps::SPEAKING;
        if (state_name == "loading") return DeviceStateFps::LOADING;
        if (state_name == "settings") return DeviceStateFps::SETTINGS;
        if (state_name == "updating") return DeviceStateFps::UPDATING;
        if (state_name == "success") return DeviceStateFps::SUCCESS;
        if (state_name == "error") return DeviceStateFps::ERROR;
        return DEFAULT_FPS;
    }
};

} // namespace display
} // namespace xiaozhi

