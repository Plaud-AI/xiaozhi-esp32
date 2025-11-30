#pragma once

/**
 * @file emotion_animation_driver.h
 * @brief 情感状态驱动动画接口（预留接口，待实现）
 * 
 * 这是一个高级接口，用于未来实现基于情感状态的动画驱动
 * 
 * 设计理念：
 * - 设备状态 → 情感识别 → 情感状态 → 动画文件
 * - 支持更复杂的情感表达和动画切换
 * - 可以根据上下文动态调整情感表现
 * 
 * 当前状态：接口预留，暂未实现
 */

#include <string>
#include <functional>

namespace display {

/**
 * @brief 情感状态（预留）
 * 
 * 这些是抽象的情感状态，可以由多种设备状态、用户交互、
 * 语音情感识别等输入源驱动
 */
enum class EmotionState {
    NEUTRAL,      // 中性/平静
    HAPPY,        // 高兴/满意
    THINKING,     // 思考/处理中
    LISTENING,    // 倾听/接收中
    SPEAKING,     // 说话/输出中
    SURPRISED,    // 惊讶/意外
    CONFUSED,     // 困惑/不确定
    SLEEPY,       // 困倦/待机
    BUSY,         // 忙碌/处理中
    ERROR,        // 错误/问题
    EXCITED,      // 兴奋/激动
    CALM,         // 安静/放松
    SETTINGS,     // 设置/配置中
    UPDATING,     // 更新/升级中
};

/**
 * @brief 情感优先级
 */
enum class EmotionPriority {
    LOW,          // 低优先级（可被打断）
    NORMAL,       // 普通优先级
    HIGH,         // 高优先级（不轻易被打断）
    CRITICAL,     // 关键优先级（必须完成）
};

/**
 * @brief 情感动画请求
 */
struct EmotionAnimationRequest {
    EmotionState emotion;           // 情感状态
    std::string animation_url;      // 动画文件路径或 URL
    bool loop;                      // 是否循环播放
    EmotionPriority priority;       // 优先级
    int duration_ms;                // 持续时长（-1 表示无限）
    
    EmotionAnimationRequest()
        : emotion(EmotionState::NEUTRAL)
        , loop(true)
        , priority(EmotionPriority::NORMAL)
        , duration_ms(-1) {}
};

/**
 * @brief 情感动画回调
 */
using EmotionAnimationCallback = std::function<void(EmotionState, bool /*success*/)>;

/**
 * @brief 情感动画驱动器（接口类，待实现）
 * 
 * 这个类定义了情感驱动动画的标准接口
 * 未来可以接入：
 * - 语音情感识别
 * - 用户行为分析
 * - 智能情感推理
 * - 多模态情感融合
 */
class EmotionAnimationDriver {
public:
    virtual ~EmotionAnimationDriver() = default;

    /**
     * @brief 初始化
     */
    virtual void Init() = 0;

    /**
     * @brief 请求播放情感动画
     * 
     * @param request 动画请求
     * @param callback 完成回调（可选）
     * @return true 请求成功
     * @return false 请求失败
     */
    virtual bool RequestEmotionAnimation(
        const EmotionAnimationRequest& request,
        EmotionAnimationCallback callback = nullptr) = 0;

    /**
     * @brief 设置情感状态（不指定动画文件）
     * 
     * 系统会根据情感状态自动选择合适的动画
     * 
     * @param emotion 情感状态
     * @param priority 优先级
     * @param duration_ms 持续时长（-1 表示无限）
     * @return true 设置成功
     */
    virtual bool SetEmotion(
        EmotionState emotion,
        EmotionPriority priority = EmotionPriority::NORMAL,
        int duration_ms = -1) = 0;

    /**
     * @brief 直接播放动画文件（指定情感上下文）
     * 
     * @param animation_url 动画文件路径或 URL
     * @param emotion 情感上下文（用于记录和分析）
     * @param loop 是否循环
     * @return true 播放成功
     */
    virtual bool PlayAnimation(
        const std::string& animation_url,
        EmotionState emotion = EmotionState::NEUTRAL,
        bool loop = true) = 0;

    /**
     * @brief 停止当前动画
     */
    virtual void StopAnimation() = 0;

    /**
     * @brief 获取当前情感状态
     */
    virtual EmotionState GetCurrentEmotion() const = 0;

    /**
     * @brief 判断是否正在播放动画
     */
    virtual bool IsPlaying() const = 0;
};

/**
 * @brief 情感动画驱动器的简单实现（待完善）
 * 
 * 当前仅提供基础功能，未来可扩展为完整的情感系统
 */
class SimpleEmotionAnimationDriver : public EmotionAnimationDriver {
public:
    static SimpleEmotionAnimationDriver& GetInstance();

    void Init() override;
    
    bool RequestEmotionAnimation(
        const EmotionAnimationRequest& request,
        EmotionAnimationCallback callback = nullptr) override;
    
    bool SetEmotion(
        EmotionState emotion,
        EmotionPriority priority = EmotionPriority::NORMAL,
        int duration_ms = -1) override;
    
    bool PlayAnimation(
        const std::string& animation_url,
        EmotionState emotion = EmotionState::NEUTRAL,
        bool loop = true) override;
    
    void StopAnimation() override;
    
    EmotionState GetCurrentEmotion() const override;
    
    bool IsPlaying() const override;

private:
    SimpleEmotionAnimationDriver() = default;
    ~SimpleEmotionAnimationDriver() = default;
    SimpleEmotionAnimationDriver(const SimpleEmotionAnimationDriver&) = delete;
    SimpleEmotionAnimationDriver& operator=(const SimpleEmotionAnimationDriver&) = delete;

    bool initialized_ = false;
    EmotionState current_emotion_ = EmotionState::NEUTRAL;
    bool is_playing_ = false;
};

// ============================================================================
// 便捷函数（全局接口）
// ============================================================================

/**
 * @brief 设置情感状态并播放对应动画
 * 
 * @param emotion 情感状态
 * @return true 成功
 */
inline bool SetEmotionState(EmotionState emotion) {
    return SimpleEmotionAnimationDriver::GetInstance().SetEmotion(emotion);
}

/**
 * @brief 播放情感动画
 * 
 * @param animation_url 动画文件路径
 * @param emotion 情感上下文
 * @return true 成功
 */
inline bool PlayEmotionAnimation(const std::string& animation_url, 
                                  EmotionState emotion = EmotionState::NEUTRAL) {
    return SimpleEmotionAnimationDriver::GetInstance().PlayAnimation(animation_url, emotion);
}

} // namespace display

