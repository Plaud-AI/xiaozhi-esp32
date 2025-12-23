#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <cstdint>

namespace xiaozhi {
namespace display {

/**
 * @brief Animation State Manager
 * 
 * 管理设备状态和感情状态，支持优先级控制和状态恢复
 */
class AnimationStateManager {
public:
    // 状态类型
    enum class StateType {
        Unknown,
        Device,    // 设备状态
        Emotion    // 感情状态
    };
    
    // 优先级定义（数值越大优先级越高）
    enum class Priority {
        Low = 0,       // 低优先级：装饰性动画，容易被打断
        Normal = 50,   // 普通优先级：设备状态（默认）
        High = 100,    // 高优先级：感情状态（默认）
        Critical = 200 // 关键优先级：系统级状态，不可被打断
    };
    
    // 设备状态枚举
    enum class DeviceState {
        Unknown,
        Idle,          // 待机
        Listening,     // 倾听
        Speaking,      // 说话
        Loading,       // 加载
        Settings,      // 设置
        Updating,      // 更新
        Success,       // 成功
        Error          // 错误
    };
    
    // 状态变更事件
    struct StateChangeEvent {
        StateType type;
        std::string state_name;
        std::string animation_file;
        int animation_index;    // 动画索引（用于 mmap 模式）
        bool loop;
        int fps;
        Priority priority;
        bool force_interrupt;   // 是否强制打断
        uint32_t timeout_ms;    // 超时时间（0=永久，用于感情状态自动恢复）
        
        StateChangeEvent()
            : type(StateType::Unknown)
            , animation_index(-1)
            , loop(true)
            , fps(15)
            , priority(Priority::Normal)
            , force_interrupt(false)
            , timeout_ms(0) {
        }
    };
    
    // 状态变更回调
    using StateChangeCallback = std::function<void(const StateChangeEvent&)>;
    
    AnimationStateManager();
    ~AnimationStateManager() = default;
    
    // 禁止拷贝
    AnimationStateManager(const AnimationStateManager&) = delete;
    AnimationStateManager& operator=(const AnimationStateManager&) = delete;
    
    // 状态切换（使用默认优先级）
    bool SetDeviceState(DeviceState state);
    bool SetEmotionState(const char* emotion);
    
    // 状态切换（显式指定优先级）
    bool SetDeviceStateWithPriority(DeviceState state, Priority priority, bool force = false);
    bool SetEmotionStateWithPriority(const char* emotion, Priority priority, 
                                      bool force = false, uint32_t timeout_ms = 5000);
    
    // 通用接口（最灵活）
    bool RequestStateChange(const StateChangeEvent& event);
    
    // 状态查询 - 返回公开的状态信息结构
    struct StateInfo {
        StateType type;
        std::string name;
        std::string animation_file;
        int animation_index;
        bool loop;
        int fps;
        Priority priority;
        uint32_t timestamp;
        uint32_t timeout_ms;
        
        StateInfo()
            : type(StateType::Unknown)
            , animation_index(-1)
            , loop(true)
            , fps(15)
            , priority(Priority::Normal)
            , timestamp(0)
            , timeout_ms(0) {}
    };
    
    StateInfo GetCurrentState() const { return current_state_; }
    StateInfo GetPreviousState() const;
    Priority GetCurrentPriority() const { return current_state_.priority; }
    bool IsEmotionActive() const { return current_state_.type == StateType::Emotion; }
    bool CanInterrupt(Priority new_priority, bool force = false) const;
    
    // 状态恢复（感情状态结束后）
    bool RestorePreviousState();
    
    // 恢复到当前设备状态（推荐：情感动画超时后使用）
    bool RestoreToCurrentDeviceState();
    
    // 获取当前设备状态
    DeviceState GetCurrentDeviceState() const { return current_device_state_; }
    
    // 映射配置
    void RegisterDeviceStateMapping(DeviceState state, 
                                    const char* anim_file,
                                    int anim_index,
                                    bool loop, 
                                    int fps,
                                    Priority default_priority = Priority::Normal);
    
    void RegisterEmotionMapping(const char* emotion,
                                const char* anim_file,
                                int anim_index,
                                bool loop, 
                                int fps,
                                Priority default_priority = Priority::High);
    
    // 设置状态变更回调
    void SetStateChangeCallback(StateChangeCallback callback) {
        state_change_callback_ = callback;
    }
    
    // 获取设备状态名称（用于日志）
    static const char* GetDeviceStateName(DeviceState state);
    
private:
    StateInfo current_state_;
    DeviceState current_device_state_ = DeviceState::Unknown;  // 跟踪当前设备状态
    std::vector<StateInfo> state_history_;  // 最多保留 10 个
    
    // 映射表
    std::unordered_map<DeviceState, StateInfo> device_state_map_;
    std::unordered_map<std::string, StateInfo> emotion_map_;
    
    // 回调
    StateChangeCallback state_change_callback_;
    
    // 辅助函数
    void PushStateHistory(const StateInfo& state);
    StateInfo PopStateHistory();
    bool CheckPriorityAndInterrupt(Priority new_priority, bool force);
    StateInfo ConvertEventToState(const StateChangeEvent& event);
    void OnStateChanged(const StateInfo& state);
    uint32_t GetCurrentTimestamp() const;
};

} // namespace display
} // namespace xiaozhi

