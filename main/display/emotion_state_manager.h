#pragma once

/**
 * @file emotion_state_manager.h
 * @brief 情感状态管理模块
 * 
 * 负责管理设备的情感状态，独立于具体的动画实现
 * 特性：
 * - 情感状态切换
 * - 情感优先级管理
 * - 临时情感（自动恢复）
 * - 情感历史记录
 * - 状态变化通知
 */

#include <string>
#include <functional>
#include <vector>
#include <map>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

namespace emotion {

/**
 * @brief 情感类型枚举
 * 
 * 定义系统支持的所有情感类型
 */
enum class EmotionState {
    // 基础情感
    NEUTRAL,        // 😐 中性/平静
    HAPPY,          // 😊 开心
    SAD,            // 😢 悲伤
    EXCITED,        // 😆 兴奋
    CALM,           // 😌 平静/满足
    SLEEPY,         // 😴 困倦
    SURPRISED,      // 😮 惊讶/好奇
    
    // 交互状态情感
    LISTENING,      // 👂 倾听
    THINKING,       // 🤔 思考
    SPEAKING,       // 🗣️ 说话
    
    // 系统状态情感
    BUSY,           // 😓 忙碌
    ERROR,          // 😰 错误
    CONNECTING,     // 🔄 连接中
    
    // 特殊情感
    LOVE,           // 😍 喜爱
    CONFUSED,       // 🤨 困惑
    ANGRY,          // 😠 生气
    
    UNKNOWN         // 未知
};

/**
 * @brief 情感优先级
 * 
 * 用于处理情感冲突，高优先级的情感会覆盖低优先级
 */
enum class EmotionPriority {
    LOW = 0,        // 低优先级（如背景情感）
    NORMAL = 1,     // 普通优先级（如用户交互）
    HIGH = 2,       // 高优先级（如系统警告）
    CRITICAL = 3    // 关键优先级（如错误状态）
};

/**
 * @brief 情感持续模式
 */
enum class EmotionDuration {
    ONCE,           // 播放一次后自动恢复
    TEMPORARY,      // 临时显示（指定时长）
    PERSISTENT      // 持续显示直到手动切换
};

/**
 * @brief 情感状态配置
 */
struct EmotionConfig {
    EmotionPriority priority;       // 优先级
    EmotionDuration duration;       // 持续模式
    int duration_ms;                // 持续时长（毫秒，仅对 TEMPORARY 有效）
    bool allow_interrupt;           // 是否允许被打断
    
    EmotionConfig()
        : priority(EmotionPriority::NORMAL)
        , duration(EmotionDuration::PERSISTENT)
        , duration_ms(0)
        , allow_interrupt(true) {}
};

/**
 * @brief 情感状态变化事件
 */
struct EmotionChangeEvent {
    EmotionState from_state;        // 源状态
    EmotionState to_state;          // 目标状态
    EmotionPriority priority;       // 优先级
    uint32_t timestamp;             // 时间戳
    bool is_auto_restore;           // 是否自动恢复
};

/**
 * @brief 情感到字符串
 */
inline const char* EmotionStateToString(EmotionState state) {
    switch(state) {
        case EmotionState::NEUTRAL:     return "neutral";
        case EmotionState::HAPPY:       return "happy";
        case EmotionState::SAD:         return "sad";
        case EmotionState::EXCITED:     return "excited";
        case EmotionState::CALM:        return "calm";
        case EmotionState::SLEEPY:      return "sleepy";
        case EmotionState::SURPRISED:   return "surprised";
        case EmotionState::LISTENING:   return "listening";
        case EmotionState::THINKING:    return "thinking";
        case EmotionState::SPEAKING:    return "speaking";
        case EmotionState::BUSY:        return "busy";
        case EmotionState::ERROR:       return "error";
        case EmotionState::CONNECTING:  return "connecting";
        case EmotionState::LOVE:        return "love";
        case EmotionState::CONFUSED:    return "confused";
        case EmotionState::ANGRY:       return "angry";
        case EmotionState::UNKNOWN:     return "unknown";
        default:                        return "unknown";
    }
}

/**
 * @brief 字符串到情感
 */
inline EmotionState StringToEmotionState(const std::string& str) {
    if (str == "neutral")    return EmotionState::NEUTRAL;
    if (str == "happy")      return EmotionState::HAPPY;
    if (str == "sad")        return EmotionState::SAD;
    if (str == "excited")    return EmotionState::EXCITED;
    if (str == "calm")       return EmotionState::CALM;
    if (str == "sleepy")     return EmotionState::SLEEPY;
    if (str == "surprised")  return EmotionState::SURPRISED;
    if (str == "listening")  return EmotionState::LISTENING;
    if (str == "thinking")   return EmotionState::THINKING;
    if (str == "speaking")   return EmotionState::SPEAKING;
    if (str == "busy")       return EmotionState::BUSY;
    if (str == "error")      return EmotionState::ERROR;
    if (str == "connecting") return EmotionState::CONNECTING;
    if (str == "love")       return EmotionState::LOVE;
    if (str == "confused")   return EmotionState::CONFUSED;
    if (str == "angry")      return EmotionState::ANGRY;
    return EmotionState::UNKNOWN;
}

/**
 * @brief 情感状态管理器
 * 
 * 核心功能：
 * - 管理当前情感状态
 * - 处理情感优先级
 * - 支持临时情感和自动恢复
 * - 提供状态变化通知
 */
class EmotionStateManager {
public:
    /**
     * @brief 获取单例
     */
    static EmotionStateManager& Instance();

    /**
     * @brief 初始化
     * 
     * @param default_emotion 默认情感
     */
    void Init(EmotionState default_emotion = EmotionState::NEUTRAL);

    /**
     * @brief 设置情感状态
     * 
     * @param emotion 情感类型
     * @param config 情感配置
     * @return true 设置成功
     * @return false 设置失败（可能被优先级阻止）
     */
    bool SetEmotion(EmotionState emotion, const EmotionConfig& config = EmotionConfig());

    /**
     * @brief 设置临时情感
     * 
     * @param emotion 情感类型
     * @param duration_ms 持续时长（毫秒），-1 表示播放一次完整动画
     * @param priority 优先级
     * @return true 设置成功
     */
    bool SetTemporaryEmotion(EmotionState emotion, int duration_ms, 
                            EmotionPriority priority = EmotionPriority::NORMAL);

    /**
     * @brief 强制设置情感（忽略优先级）
     * 
     * @param emotion 情感类型
     * @param config 配置
     */
    void ForceSetEmotion(EmotionState emotion, const EmotionConfig& config = EmotionConfig());

    /**
     * @brief 恢复到之前的情感状态
     * 
     * @return true 恢复成功
     * @return false 无历史记录
     */
    bool RestorePreviousEmotion();

    /**
     * @brief 获取当前情感
     */
    EmotionState GetCurrentEmotion() const { return current_emotion_; }

    /**
     * @brief 获取之前的情感
     */
    EmotionState GetPreviousEmotion() const { return previous_emotion_; }

    /**
     * @brief 获取当前优先级
     */
    EmotionPriority GetCurrentPriority() const { return current_priority_; }

    /**
     * @brief 设置默认情感
     * 
     * @param emotion 默认情感
     */
    void SetDefaultEmotion(EmotionState emotion) { default_emotion_ = emotion; }

    /**
     * @brief 获取默认情感
     */
    EmotionState GetDefaultEmotion() const { return default_emotion_; }

    /**
     * @brief 重置到默认情感
     */
    void ResetToDefault();

    /**
     * @brief 设置状态变化回调
     * 
     * @param callback 回调函数
     */
    void SetStateChangeCallback(std::function<void(const EmotionChangeEvent&)> callback);

    /**
     * @brief 获取情感历史记录
     * 
     * @param max_count 最大返回数量
     * @return 历史记录列表
     */
    std::vector<EmotionChangeEvent> GetHistory(int max_count = 10) const;

    /**
     * @brief 清除历史记录
     */
    void ClearHistory();

    /**
     * @brief 启用/禁用自动恢复
     * 
     * @param enable 是否启用
     */
    void SetAutoRestore(bool enable) { auto_restore_enabled_ = enable; }

    /**
     * @brief 检查是否可以切换到指定情感
     * 
     * @param emotion 情感
     * @param priority 优先级
     * @return true 可以切换
     */
    bool CanSwitchTo(EmotionState emotion, EmotionPriority priority) const;

    /**
     * @brief 获取情感统计信息
     */
    struct Statistics {
        int total_switches;             // 总切换次数
        EmotionState most_used;         // 最常用情感
        uint32_t current_duration_ms;   // 当前情感持续时长
    };
    Statistics GetStatistics() const;

private:
    EmotionStateManager() = default;
    ~EmotionStateManager() = default;

    // 禁止拷贝和赋值
    EmotionStateManager(const EmotionStateManager&) = delete;
    EmotionStateManager& operator=(const EmotionStateManager&) = delete;

    // 内部方法
    void TriggerStateChange(EmotionState from, EmotionState to, bool is_auto_restore);
    void ScheduleAutoRestore();
    void CancelAutoRestore();
    static void OnAutoRestoreTimer(TimerHandle_t timer);

    // 状态变量
    EmotionState current_emotion_;
    EmotionState previous_emotion_;
    EmotionState default_emotion_;
    
    EmotionPriority current_priority_;
    EmotionConfig current_config_;
    
    bool initialized_;
    bool auto_restore_enabled_;
    
    // 历史记录
    std::vector<EmotionChangeEvent> history_;
    static constexpr size_t MAX_HISTORY_SIZE = 50;
    
    // 统计信息
    std::map<EmotionState, int> emotion_count_;
    int total_switches_;
    uint32_t current_state_start_time_;
    
    // 回调
    std::function<void(const EmotionChangeEvent&)> state_change_callback_;
    
    // 定时器
    TimerHandle_t auto_restore_timer_;
};

} // namespace emotion

