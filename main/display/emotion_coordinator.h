#pragma once

/**
 * @file emotion_coordinator.h
 * @brief 情感系统协调器
 * 
 * 整合所有情感相关模块，提供统一的接口
 * 
 * 架构：
 * DeviceState → DeviceStateMapper → EmotionState → EmotionStateManager
 *                                                  ↓
 *                                    EmotionAnimationMapper
 *                                                  ↓
 *                                    Lottie Animation Player
 * 
 * 这是整个情感系统的门面（Facade），对外提供简洁的 API
 */

#include "emotion_state_manager.h"
#include "emotion_animation_mapper.h"
#include "device_state_mapper.h"
#include "animation_manager.h"
#include <functional>

namespace emotion {

/**
 * @brief 情感系统配置
 */
struct EmotionSystemConfig {
    std::string animation_base_path;    // 动画文件基础路径
    int screen_width;                   // 屏幕宽度
    int screen_height;                  // 屏幕高度
    EmotionState default_emotion;       // 默认情感
    bool auto_register_mappings;        // 是否自动注册映射
    bool enable_auto_restore;           // 是否启用自动恢复
    int auto_restore_delay_ms;          // 自动恢复延迟
    
    EmotionSystemConfig()
        : animation_base_path("/spiffs/anim/")
        , screen_width(240)
        , screen_height(240)
        , default_emotion(EmotionState::NEUTRAL)
        , auto_register_mappings(true)
        , enable_auto_restore(true)
        , auto_restore_delay_ms(5000) {}
};

/**
 * @brief 情感系统协调器
 * 
 * 核心功能：
 * - 统一初始化所有情感模块
 * - 提供简洁的设备状态切换接口
 * - 自动处理状态映射和动画播放
 * - 提供高级功能（序列、回调等）
 */
class EmotionCoordinator {
public:
    /**
     * @brief 获取单例
     */
    static EmotionCoordinator& Instance();

    /**
     * @brief 初始化情感系统
     * 
     * @param config 系统配置
     * @param parent LVGL 父对象
     * @return true 成功
     * @return false 失败
     */
    bool Init(const EmotionSystemConfig& config, lv_obj_t* parent = nullptr);

    /**
     * @brief 快速初始化（使用默认配置）
     * 
     * @param anim_path 动画文件路径
     * @param width 屏幕宽度
     * @param height 屏幕高度
     * @param parent LVGL 父对象
     * @return true 成功
     */
    bool QuickInit(const char* anim_path = "/spiffs/anim/",
                   int width = 240, int height = 240,
                   lv_obj_t* parent = nullptr);

    // ========================================================================
    // 设备状态接口（推荐使用，最简洁）
    // ========================================================================

    /**
     * @brief 设置设备状态
     * 
     * 自动映射到情感并播放动画
     * 
     * @param device_state 设备状态
     * @param context 映射上下文（可选）
     * @return true 成功
     */
    bool SetDeviceState(DeviceState device_state, const MappingContext* context = nullptr);

    /**
     * @brief 设置设备状态（字符串接口）
     */
    bool SetDeviceState(const char* state_name);

    // ========================================================================
    // 情感状态接口（直接控制情感）
    // ========================================================================

    /**
     * @brief 设置情感状态
     * 
     * @param emotion 情感状态
     * @param config 情感配置
     * @return true 成功
     */
    bool SetEmotion(EmotionState emotion, const EmotionConfig& config = EmotionConfig());

    /**
     * @brief 设置临时情感
     * 
     * @param emotion 情感状态
     * @param duration_ms 持续时长
     * @return true 成功
     */
    bool SetTemporaryEmotion(EmotionState emotion, int duration_ms);

    /**
     * @brief 重置到默认情感
     */
    void ResetToDefault();

    // ========================================================================
    // 动画控制接口
    // ========================================================================

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
     * @brief 播放自定义动画（不改变情感状态）
     * 
     * @param animation_path 动画文件路径
     * @param loop 是否循环
     * @param auto_return 是否自动返回之前状态
     */
    void PlayCustomAnimation(const std::string& animation_path, 
                            bool loop = false, 
                            bool auto_return = true);

    // ========================================================================
    // 状态查询接口
    // ========================================================================

    /**
     * @brief 获取当前情感状态
     */
    EmotionState GetCurrentEmotion() const;

    /**
     * @brief 获取之前的情感状态
     */
    EmotionState GetPreviousEmotion() const;

    /**
     * @brief 检查系统是否已初始化
     */
    bool IsInitialized() const { return initialized_; }

    // ========================================================================
    // 回调接口
    // ========================================================================

    /**
     * @brief 设置状态变化回调
     * 
     * 当情感状态变化时触发
     * 
     * @param callback 回调函数
     */
    void SetStateChangeCallback(std::function<void(const EmotionChangeEvent&)> callback);

    /**
     * @brief 设置动画开始回调
     * 
     * @param callback 回调函数
     */
    void SetAnimationStartCallback(std::function<void(const std::string&)> callback);

    // ========================================================================
    // 高级功能
    // ========================================================================

    /**
     * @brief 注册自定义设备状态映射
     * 
     * @param device_state 设备状态
     * @param emotion 对应的情感
     * @param priority 优先级
     */
    void RegisterDeviceStateMapping(DeviceState device_state, 
                                    EmotionState emotion,
                                    EmotionPriority priority = EmotionPriority::NORMAL);

    /**
     * @brief 注册情感动画
     * 
     * @param emotion 情感状态
     * @param animation_path 动画文件路径
     * @param weight 权重
     */
    void RegisterEmotionAnimation(EmotionState emotion, 
                                  const std::string& animation_path,
                                  int weight = 1);

    /**
     * @brief 获取统计信息
     */
    EmotionStateManager::Statistics GetStatistics() const;

    /**
     * @brief 打印系统信息（调试用）
     */
    void PrintSystemInfo() const;

    // ========================================================================
    // 访问子模块（高级用法）
    // ========================================================================

    EmotionStateManager& GetStateManager() { return EmotionStateManager::Instance(); }
    EmotionAnimationMapper& GetAnimMapper() { return EmotionAnimationMapper::Instance(); }
    DeviceStateMapper& GetDeviceMapper() { return DeviceStateMapper::Instance(); }
    lottie::AnimationManager& GetAnimationManager() { return lottie::AnimationManager::Instance(); }

    /**
     * @brief 设置当前基于 Assets 的动画对象
     * 
     * 用于管理内存生命周期，自动释放旧动画对象
     * 
     * @param anim 新动画对象指针（所有权转移给 Coordinator）
     */
    void SetCurrentAssetAnimation(lottie::LottieAnimation* anim);

private:
    EmotionCoordinator() = default;
    ~EmotionCoordinator() = default;

    // 禁止拷贝和赋值
    EmotionCoordinator(const EmotionCoordinator&) = delete;
    EmotionCoordinator& operator=(const EmotionCoordinator&) = delete;

    // 内部方法
    void OnEmotionChanged(const EmotionChangeEvent& event);
    void PlayEmotionAnimation(EmotionState emotion);

    // 成员变量
    bool initialized_ = false;
    EmotionSystemConfig config_;
    lv_obj_t* animation_container_ = nullptr;  // LVGL 动画容器（用于创建动画对象）
    
    // 基于 Assets 的当前动画对象（手动管理生命周期）
    lottie::LottieAnimation* current_asset_anim_ = nullptr;

    // 当前正在播放（或加载中）的情感状态
    EmotionState current_playing_emotion_; 
    bool has_emotion_set_ = false;

    std::function<void(const std::string&)> animation_start_callback_;
};

} // namespace emotion

// ============================================================================
// 全局便捷函数（可选）
// ============================================================================

namespace xiaozhi {

/**
 * @brief 全局便捷接口：设置设备状态
 * 
 * 使用示例：
 *   xiaozhi::SetState(emotion::DeviceState::LISTENING);
 */
inline void SetState(emotion::DeviceState state) {
    emotion::EmotionCoordinator::Instance().SetDeviceState(state);
}

/**
 * @brief 全局便捷接口：设置情感
 */
inline void SetEmotion(emotion::EmotionState emotion) {
    emotion::EmotionCoordinator::Instance().SetEmotion(emotion);
}

/**
 * @brief 全局便捷接口：重置到默认状态
 */
inline void ResetEmotion() {
    emotion::EmotionCoordinator::Instance().ResetToDefault();
}

} // namespace xiaozhi

