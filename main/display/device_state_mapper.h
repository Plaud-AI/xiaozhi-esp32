#pragma once

/**
 * @file device_state_mapper.h
 * @brief 设备状态到情感映射模块
 * 
 * 负责将小智设备的具体状态映射到情感状态
 * 这是一个适配层，解耦设备状态和情感表达
 * 
 * 特性：
 * - 设备状态到情感的灵活映射
 * - 支持自定义映射规则
 * - 支持上下文感知映射
 * - 易于扩展和修改
 */

#include "emotion_state_manager.h"
#include <string>
#include <map>
#include <functional>

namespace emotion {

/**
 * @brief 小智设备状态枚举
 * 
 * 定义小智语音助手的所有工作状态
 */
enum class DeviceState {
    // 初始化状态
    BOOTING,            // 启动中
    INITIALIZING,       // 初始化
    
    // 待机状态
    IDLE,               // 空闲
    SLEEPING,           // 休眠
    
    // 唤醒相关
    WAKING,             // 唤醒中
    AWAKE,              // 已唤醒
    
    // 交互状态
    LISTENING,          // 聆听用户输入
    PROCESSING,         // 处理中/思考
    SPEAKING,           // 播放语音回复
    
    // 网络状态
    CONNECTING,         // 网络连接中
    CONNECTED,          // 已连接
    DISCONNECTED,       // 断开连接
    
    // 更新状态
    UPDATING,           // 固件更新中
    
    // 错误状态
    ERROR,              // 错误
    LOW_BATTERY,        // 低电量
    
    // 音乐相关
    PLAYING_MUSIC,      // 播放音乐
    
    // 其他
    BUSY,               // 忙碌
    CUSTOM              // 自定义状态
};

/**
 * @brief 设备状态到字符串
 */
inline const char* DeviceStateToString(DeviceState state) {
    switch(state) {
        case DeviceState::BOOTING:          return "booting";
        case DeviceState::INITIALIZING:     return "initializing";
        case DeviceState::IDLE:             return "idle";
        case DeviceState::SLEEPING:         return "sleeping";
        case DeviceState::WAKING:           return "waking";
        case DeviceState::AWAKE:            return "awake";
        case DeviceState::LISTENING:        return "listening";
        case DeviceState::PROCESSING:       return "processing";
        case DeviceState::SPEAKING:         return "speaking";
        case DeviceState::CONNECTING:       return "connecting";
        case DeviceState::CONNECTED:        return "connected";
        case DeviceState::DISCONNECTED:     return "disconnected";
        case DeviceState::UPDATING:         return "updating";
        case DeviceState::ERROR:            return "error";
        case DeviceState::LOW_BATTERY:      return "low_battery";
        case DeviceState::PLAYING_MUSIC:    return "playing_music";
        case DeviceState::BUSY:             return "busy";
        case DeviceState::CUSTOM:           return "custom";
        default:                            return "unknown";
    }
}

/**
 * @brief 映射上下文
 * 
 * 提供额外的上下文信息，用于更精确的映射
 */
struct MappingContext {
    int battery_level;          // 电池电量 (0-100)
    bool is_first_interaction;  // 是否首次交互
    int interaction_count;      // 交互次数
    uint32_t uptime_seconds;    // 运行时长（秒）
    std::string last_command;   // 最后的命令
    
    MappingContext()
        : battery_level(100)
        , is_first_interaction(true)
        , interaction_count(0)
        , uptime_seconds(0) {}
};

/**
 * @brief 映射规则
 * 
 * 定义一个设备状态如何映射到情感状态
 */
struct MappingRule {
    DeviceState device_state;               // 设备状态
    EmotionState emotion_state;             // 对应的情感状态
    EmotionPriority priority;               // 优先级
    EmotionDuration duration;               // 持续模式
    int duration_ms;                        // 持续时长
    
    // 高级：条件映射（可选，nullptr 表示无条件）
    std::function<bool(const MappingContext&)> condition;
    
    MappingRule()
        : device_state(DeviceState::IDLE)
        , emotion_state(EmotionState::NEUTRAL)
        , priority(EmotionPriority::NORMAL)
        , duration(EmotionDuration::PERSISTENT)
        , duration_ms(0)
        , condition(nullptr) {}
        
    MappingRule(DeviceState dev, EmotionState emo, 
                EmotionPriority pri = EmotionPriority::NORMAL,
                EmotionDuration dur = EmotionDuration::PERSISTENT,
                int dur_ms = 0)
        : device_state(dev)
        , emotion_state(emo)
        , priority(pri)
        , duration(dur)
        , duration_ms(dur_ms)
        , condition(nullptr) {}
};

/**
 * @brief 设备状态到情感映射器
 * 
 * 核心功能：
 * - 管理设备状态到情感的映射规则
 * - 应用映射规则
 * - 支持上下文感知映射
 * - 提供默认映射配置
 */
class DeviceStateMapper {
public:
    /**
     * @brief 获取单例
     */
    static DeviceStateMapper& Instance();

    /**
     * @brief 初始化
     */
    void Init();

    /**
     * @brief 注册映射规则
     * 
     * @param rule 映射规则
     */
    void RegisterMapping(const MappingRule& rule);

    /**
     * @brief 批量注册映射规则
     * 
     * @param rules 规则列表
     */
    void RegisterMappings(const std::vector<MappingRule>& rules);

    /**
     * @brief 注册默认映射规则
     * 
     * 创建小智项目的标准映射
     * 
     * @return 注册的规则数量
     */
    int RegisterDefaultMappings();

    /**
     * @brief 将设备状态映射到情感状态
     * 
     * @param device_state 设备状态
     * @param context 映射上下文（可选）
     * @return 对应的情感状态
     */
    EmotionState MapToEmotion(DeviceState device_state, 
                              const MappingContext* context = nullptr) const;

    /**
     * @brief 获取完整的映射配置
     * 
     * @param device_state 设备状态
     * @param context 映射上下文
     * @param[out] out_config 输出情感配置
     * @return true 找到映射
     */
    bool GetEmotionConfig(DeviceState device_state,
                         const MappingContext* context,
                         EmotionConfig& out_config,
                         EmotionState& out_emotion) const;

    /**
     * @brief 应用设备状态（自动映射并设置情感）
     * 
     * 便捷方法，直接将设备状态应用到情感状态管理器
     * 
     * @param device_state 设备状态
     * @param context 映射上下文
     * @param emotion_mgr 情感状态管理器（可选，默认使用单例）
     * @return true 成功
     */
    bool ApplyDeviceState(DeviceState device_state,
                         const MappingContext* context = nullptr,
                         EmotionStateManager* emotion_mgr = nullptr);

    /**
     * @brief 清除所有映射
     */
    void ClearAllMappings();

    /**
     * @brief 清除指定设备状态的映射
     * 
     * @param device_state 设备状态
     */
    void ClearMapping(DeviceState device_state);

    /**
     * @brief 打印映射规则（调试用）
     */
    void PrintMappings() const;

    /**
     * @brief 获取已映射的设备状态列表
     */
    std::vector<DeviceState> GetMappedStates() const;

    /**
     * @brief 设置上下文更新回调
     * 
     * 用于自动更新上下文信息
     * 
     * @param callback 回调函数
     */
    void SetContextUpdateCallback(std::function<MappingContext()> callback);

    /**
     * @brief 获取当前上下文
     */
    MappingContext GetContext() const { return current_context_; }

    /**
     * @brief 更新上下文
     * 
     * @param context 新的上下文
     */
    void UpdateContext(const MappingContext& context);

private:
    DeviceStateMapper() = default;
    ~DeviceStateMapper() = default;

    // 禁止拷贝和赋值
    DeviceStateMapper(const DeviceStateMapper&) = delete;
    DeviceStateMapper& operator=(const DeviceStateMapper&) = delete;

    // 内部方法
    const MappingRule* FindMatchingRule(DeviceState device_state, 
                                        const MappingContext* context) const;

    // 成员变量
    std::map<DeviceState, std::vector<MappingRule>> mappings_;
    MappingContext current_context_;
    std::function<MappingContext()> context_update_callback_;
    bool initialized_;
};

} // namespace emotion

