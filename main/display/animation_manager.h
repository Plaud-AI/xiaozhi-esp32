#pragma once

#include "lottie_animation.h"
#include <string>
#include <map>
#include <memory>

namespace lottie {

/**
 * @brief 动画状态枚举
 */
enum class AnimState {
    IDLE,           // 待机状态（呼吸灯效果）
    WAKEUP,         // 唤醒动画
    LISTENING,      // 聆听状态（音频波形）
    THINKING,       // 思考状态（加载动画）
    SPEAKING,       // 说话状态（音波动画）
    ERROR,          // 错误提示
    CONNECTING,     // 网络连接中
    CUSTOM          // 自定义动画
};

/**
 * @brief 动画状态到字符串的映射
 */
inline const char* AnimStateToString(AnimState state) {
    switch(state) {
        case AnimState::IDLE:       return "idle";
        case AnimState::WAKEUP:     return "wakeup";
        case AnimState::LISTENING:  return "listening";
        case AnimState::THINKING:   return "thinking";
        case AnimState::SPEAKING:   return "speaking";
        case AnimState::ERROR:      return "error";
        case AnimState::CONNECTING: return "connecting";
        case AnimState::CUSTOM:     return "custom";
        default:                    return "unknown";
    }
}

/**
 * @brief 动画配置结构
 */
struct AnimConfig {
    std::string file_path;      // 动画文件路径
    bool loop;                  // 是否循环
    int width;                  // 宽度（-1 表示使用默认）
    int height;                 // 高度（-1 表示使用默认）
    float speed;                // 播放速度

    AnimConfig()
        : loop(true), width(-1), height(-1), speed(1.0f) {}

    AnimConfig(const std::string& path, bool l = true, int w = -1, int h = -1, float s = 1.0f)
        : file_path(path), loop(l), width(w), height(h), speed(s) {}
};

/**
 * @brief 动画管理器类
 * 
 * 管理多个 Lottie 动画，支持状态切换、预加载等功能
 */
class AnimationManager {
public:
    /**
     * @brief 获取单例实例
     */
    static AnimationManager& Instance();

    /**
     * @brief 初始化动画管理器
     * 
     * @param parent LVGL 父对象
     * @param width 默认动画宽度
     * @param height 默认动画高度
     */
    void Init(lv_obj_t* parent, int width = 240, int height = 240);

    /**
     * @brief 注册动画配置
     * 
     * @param state 动画状态
     * @param config 动画配置
     */
    void RegisterAnimation(AnimState state, const AnimConfig& config);

    /**
     * @brief 注册动画（简化接口）
     * 
     * @param state 动画状态
     * @param file_path 动画文件路径
     * @param loop 是否循环
     */
    void RegisterAnimation(AnimState state, const std::string& file_path, bool loop = true);

    /**
     * @brief 切换到指定状态
     * 
     * @param state 目标状态
     * @param force 是否强制切换（即使当前已是该状态）
     */
    void SetState(AnimState state, bool force = false);

    /**
     * @brief 播放自定义动画
     * 
     * @param file_path 动画文件路径
     * @param loop 是否循环
     * @param auto_return 播放完成后是否自动返回之前的状态
     */
    void PlayCustomAnimation(const std::string& file_path, bool loop = false, bool auto_return = true);

    /**
     * @brief 获取当前状态
     */
    AnimState GetCurrentState() const { return current_state_; }

    /**
     * @brief 暂停当前动画
     */
    void Pause();

    /**
     * @brief 恢复播放
     */
    void Resume();

    /**
     * @brief 停止当前动画
     */
    void Stop();

    /**
     * @brief 设置音频电平（用于音频同步）
     * 
     * @param level 音频电平 (0.0 - 1.0)
     */
    void SetAudioLevel(float level);

    /**
     * @brief 设置动画完成回调
     * 
     * @param callback 回调函数
     */
    void SetCompleteCallback(std::function<void(AnimState)> callback);

    /**
     * @brief 显示/隐藏动画
     */
    void SetVisible(bool visible);

    /**
     * @brief 获取当前动画对象
     */
    LottieAnimation* GetCurrentAnimation() const { return current_animation_.get(); }

private:
    AnimationManager() = default;
    ~AnimationManager() = default;

    // 禁止拷贝和赋值
    AnimationManager(const AnimationManager&) = delete;
    AnimationManager& operator=(const AnimationManager&) = delete;

    void LoadAndPlayAnimation(AnimState state);
    void OnAnimationComplete();

    lv_obj_t* parent_;                              // LVGL 父对象
    int default_width_;                             // 默认宽度
    int default_height_;                            // 默认高度

    AnimState current_state_;                       // 当前状态
    AnimState previous_state_;                      // 之前的状态
    bool auto_return_;                              // 是否自动返回

    std::map<AnimState, AnimConfig> anim_configs_;  // 动画配置
    std::unique_ptr<LottieAnimation> current_animation_;  // 当前动画
    std::function<void(AnimState)> complete_callback_;    // 完成回调

    float audio_level_;                             // 音频电平
    bool initialized_;                              // 初始化标志
};

} // namespace lottie

