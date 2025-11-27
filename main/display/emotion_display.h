#pragma once

#include "display.h"
#include "emotion_animation_manager.h"
#include "animation_manager.h"
#include "lvgl.h"
#include <string>
#include <functional>

/**
 * @file emotion_display.h
 * @brief 情感动画显示 UI 系统
 * 
 * 专门用于显示 Lottie 情感动画的 UI 界面
 * - 主要区域：情感动画（占据 90%+ 区域）
 * - 顶部状态栏：可选显示图标和文字（默认隐藏）
 * - 自动管理动画播放和切换
 */

namespace display {

/**
 * @brief 顶部状态栏配置
 */
struct TopBarConfig {
    bool visible;           // 是否显示
    int height;            // 高度（像素）
    lv_color_t bg_color;   // 背景色
    uint8_t bg_opacity;    // 背景透明度 (0-255)
    
    TopBarConfig()
        : visible(false)
        , height(40)
        , bg_color(lv_color_black())
        , bg_opacity(LV_OPA_50) {}
};

/**
 * @brief 情感动画显示类
 * 
 * 提供一个专门用于显示情感动画的 UI 界面
 * 集成了 EmotionAnimationManager，提供简洁的 API
 */
class EmotionDisplay : public Display {
public:
    /**
     * @brief 构造函数
     * 
     * @param width 屏幕宽度
     * @param height 屏幕高度
     */
    EmotionDisplay(int width, int height);
    
    /**
     * @brief 析构函数
     */
    virtual ~EmotionDisplay();

    // ========================================================================
    // Display 接口实现
    // ========================================================================
    
    bool Lock(int timeout_ms = -1) override;
    void Unlock() override;
    void SetChatMessage(const char* role, const char* content) override;
    void ShowNotification(const char* message, int duration_ms = 3000) override;

    // ========================================================================
    // 扩展接口（非 override）
    // ========================================================================

    /**
     * @brief 显示低电量警告
     */
    void ShowLowBatteryWarning();

    /**
     * @brief 隐藏低电量警告
     */
    void HideLowBatteryWarning();

    /**
     * @brief 设置电池电量
     */
    void SetBatteryLevel(int percentage);

    /**
     * @brief 设置网络状态
     */
    void SetNetworkStatus(bool connected);

    /**
     * @brief 设置音量
     */
    void SetVolume(int level);

    // ========================================================================
    // 情感动画相关接口
    // ========================================================================

    /**
     * @brief 初始化情感动画系统
     * 
     * @param emotions_dir 情感动画文件目录（如 "/spiffs/emotions/"）
     * @return true 成功
     * @return false 失败
     */
    bool InitEmotionSystem(const char* emotions_dir = "/spiffs/emotions/");

    /**
     * @brief 显示指定情感
     * 
     * @param emotion 情感类型
     * @param duration_ms 持续时间（-1 表示持续到下次切换，0 表示播放完整动画）
     * @return true 成功
     * @return false 失败
     */
    bool ShowEmotion(lottie::EmotionType emotion, int duration_ms = 0);

    /**
     * @brief 显示指定情感（字符串接口）
     * 
     * @param emotion_name 情感名称（如 "happy", "sad"）
     * @param duration_ms 持续时间
     * @return true 成功
     * @return false 失败
     */
    bool ShowEmotion(const char* emotion_name, int duration_ms = 0);

    /**
     * @brief 播放情感序列
     * 
     * @param sequence 情感序列
     * @param loop 是否循环播放
     * @return true 成功
     * @return false 失败
     */
    bool PlayEmotionSequence(const std::vector<lottie::EmotionSequenceItem>& sequence, 
                            bool loop = false);

    /**
     * @brief 停止当前情感动画
     */
    void StopEmotion();

    /**
     * @brief 暂停当前情感动画
     */
    void PauseEmotion();

    /**
     * @brief 恢复播放情感动画
     */
    void ResumeEmotion();

    /**
     * @brief 获取当前显示的情感
     */
    lottie::EmotionType GetCurrentEmotion() const;

    // ========================================================================
    // 顶部状态栏接口
    // ========================================================================

    /**
     * @brief 显示顶部状态栏
     * 
     * @param icon 图标文件路径（nullptr 表示不显示图标）
     * @param text 文字内容（nullptr 表示不显示文字）
     * @param duration_ms 显示时长（-1 表示持续显示，0 表示使用默认 3 秒）
     */
    void ShowTopBar(const char* icon = nullptr, const char* text = nullptr, 
                    int duration_ms = 0);

    /**
     * @brief 隐藏顶部状态栏
     */
    void HideTopBar();

    /**
     * @brief 更新顶部文字
     * 
     * @param text 新的文字内容
     */
    void UpdateTopBarText(const char* text);

    /**
     * @brief 更新顶部图标
     * 
     * @param icon 新的图标路径
     */
    void UpdateTopBarIcon(const char* icon);

    /**
     * @brief 设置顶部状态栏配置
     * 
     * @param config 配置
     */
    void SetTopBarConfig(const TopBarConfig& config);

    /**
     * @brief 顶部状态栏是否可见
     */
    bool IsTopBarVisible() const { return top_bar_visible_; }

    // ========================================================================
    // 高级功能
    // ========================================================================

    /**
     * @brief 设置情感切换回调
     * 
     * @param callback 回调函数
     */
    void SetEmotionChangeCallback(std::function<void(lottie::EmotionType)> callback);

    /**
     * @brief 设置自动返回中性状态
     * 
     * @param enable 是否启用
     * @param delay_ms 延迟时间
     */
    void SetAutoReturnNeutral(bool enable, int delay_ms = 5000);

    /**
     * @brief 设置过渡动画
     * 
     * @param transition_path 过渡动画文件路径
     */
    void SetTransitionAnimation(const char* transition_path);

    /**
     * @brief 测试情感动画
     * 
     * @param emotion 情感类型
     * @param repeat_count 重复次数（0 表示无限循环）
     */
    void TestEmotion(lottie::EmotionType emotion, int repeat_count = 1);

private:
    /**
     * @brief 设置 UI 布局
     */
    void SetupUI();

    /**
     * @brief 创建顶部状态栏
     */
    void CreateTopBar();

    /**
     * @brief 创建动画区域
     */
    void CreateAnimationArea();

    /**
     * @brief 隐藏顶部状态栏的定时器回调
     */
    static void OnTopBarTimeout(lv_timer_t* timer);

    // 尺寸
    int width_;
    int height_;
    
    // UI 对象
    lv_obj_t* screen_;              // 主屏幕
    lv_obj_t* top_bar_;             // 顶部状态栏
    lv_obj_t* top_bar_icon_;        // 顶部图标
    lv_obj_t* top_bar_label_;       // 顶部文字
    lv_obj_t* animation_container_; // 动画容器
    
    lv_timer_t* top_bar_timer_;     // 顶部状态栏定时器
    
    // 状态
    bool emotion_system_initialized_;
    bool top_bar_visible_;
    TopBarConfig top_bar_config_;
};

} // namespace display

