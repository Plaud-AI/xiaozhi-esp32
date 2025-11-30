#pragma once

#include "animation_manager.h"
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <memory>

namespace lottie {

/**
 * @brief 情感类型枚举
 */
enum class EmotionType {
    HAPPY,          // 😊 开心
    SAD,            // 😢 难过
    SURPRISED,      // 😮 惊讶
    ANGRY,          // 😠 生气
    CONFUSED,       // 🤔 疑惑
    SLEEPY,         // 😴 困倦
    NEUTRAL,        // 😐 中性
    EXCITED,        // 😆 兴奋
    CALM,           // 😌 平静
    THINKING,       // 🤓 思考
    LOVE,           // 😍 喜爱
    UNKNOWN         // 未知情感
};

/**
 * @brief 情感类型到字符串的映射
 */
inline const char* EmotionTypeToString(EmotionType emotion) {
    switch(emotion) {
        case EmotionType::HAPPY:      return "happy";
        case EmotionType::SAD:        return "sad";
        case EmotionType::SURPRISED:  return "surprised";
        case EmotionType::ANGRY:      return "angry";
        case EmotionType::CONFUSED:   return "confused";
        case EmotionType::SLEEPY:     return "sleepy";
        case EmotionType::NEUTRAL:    return "neutral";
        case EmotionType::EXCITED:    return "excited";
        case EmotionType::CALM:       return "calm";
        case EmotionType::THINKING:   return "thinking";
        case EmotionType::LOVE:       return "love";
        case EmotionType::UNKNOWN:    return "unknown";
        default:                      return "unknown";
    }
}

/**
 * @brief 字符串到情感类型的映射
 */
inline EmotionType StringToEmotionType(const std::string& str) {
    if (str == "happy") return EmotionType::HAPPY;
    if (str == "sad") return EmotionType::SAD;
    if (str == "surprised") return EmotionType::SURPRISED;
    if (str == "angry") return EmotionType::ANGRY;
    if (str == "confused") return EmotionType::CONFUSED;
    if (str == "sleepy") return EmotionType::SLEEPY;
    if (str == "neutral") return EmotionType::NEUTRAL;
    if (str == "excited") return EmotionType::EXCITED;
    if (str == "calm") return EmotionType::CALM;
    if (str == "thinking") return EmotionType::THINKING;
    if (str == "love") return EmotionType::LOVE;
    return EmotionType::UNKNOWN;
}

/**
 * @brief 情感动画配置
 */
struct EmotionAnimConfig {
    std::string file_path;          // 动画文件路径
    bool loop;                      // 是否循环
    int duration_ms;                // 持续时间（毫秒，-1 表示使用动画默认时长）
    float speed;                    // 播放速度
    bool is_local;                  // 是否为本地文件（false 表示需要下载）
    
    EmotionAnimConfig()
        : loop(false), duration_ms(-1), speed(1.0f), is_local(true) {}
        
    EmotionAnimConfig(const std::string& path, bool l = false, int dur = -1, 
                      float s = 1.0f, bool local = true)
        : file_path(path), loop(l), duration_ms(dur), speed(s), is_local(local) {}
};

/**
 * @brief 情感序列项
 */
struct EmotionSequenceItem {
    EmotionType emotion;            // 情感类型
    int duration_ms;                // 持续时间（-1 表示播放完整动画）
    bool use_transition;            // 是否使用过渡动画
    
    EmotionSequenceItem(EmotionType e, int dur = -1, bool trans = false)
        : emotion(e), duration_ms(dur), use_transition(trans) {}
};

/**
 * @brief 情感动画管理器
 * 
 * 管理情感相关的 Lottie 动画，支持情感识别、序列播放、过渡动画等
 */
class EmotionAnimationManager {
public:
    /**
     * @brief 获取单例实例
     */
    static EmotionAnimationManager& Instance();

    /**
     * @brief 初始化情感动画管理器
     * 
     * @param anim_mgr 动画管理器实例指针（可选，默认使用 AnimationManager 单例）
     * @return true 初始化成功
     * @return false 初始化失败
     */
    bool Init(AnimationManager* anim_mgr = nullptr);

    /**
     * @brief 注册情感动画
     * 
     * @param emotion 情感类型
     * @param config 动画配置
     */
    void RegisterEmotion(EmotionType emotion, const EmotionAnimConfig& config);

    /**
     * @brief 注册情感动画（简化接口）
     * 
     * @param emotion 情感类型
     * @param file_path 动画文件路径
     * @param loop 是否循环
     * @param is_local 是否为本地文件
     */
    void RegisterEmotion(EmotionType emotion, const std::string& file_path, 
                        bool loop = false, bool is_local = true);

    /**
     * @brief 批量注册情感动画（从目录）
     * 
     * @param dir_path 目录路径（如 "/spiffs/emotions/"）
     * @param name_pattern 文件名模式（如 "{emotion}.json"，{emotion} 会被替换为情感名称）
     * @return 成功注册的情感数量
     */
    int RegisterEmotionsFromDirectory(const std::string& dir_path, 
                                     const std::string& name_pattern = "{emotion}.json");

    /**
     * @brief 显示指定情感
     * 
     * @param emotion 情感类型
     * @param duration_ms 显示持续时间（-1 表示持续到下次切换，0 表示播放完整动画）
     * @return true 成功
     * @return false 失败
     */
    bool ShowEmotion(EmotionType emotion, int duration_ms = 0);

    /**
     * @brief 播放情感序列
     * 
     * @param sequence 情感序列
     * @param loop 是否循环播放整个序列
     * @return true 成功
     * @return false 失败
     */
    bool PlayEmotionSequence(const std::vector<EmotionSequenceItem>& sequence, bool loop = false);

    /**
     * @brief 停止当前情感动画
     */
    void Stop();

    /**
     * @brief 暂停当前情感动画
     */
    void Pause();

    /**
     * @brief 恢复播放
     */
    void Resume();

    /**
     * @brief 获取当前情感
     */
    EmotionType GetCurrentEmotion() const { return current_emotion_; }

    /**
     * @brief 设置过渡动画路径
     * 
     * @param transition_path 过渡动画文件路径
     */
    void SetTransitionAnimation(const std::string& transition_path);

    /**
     * @brief 设置情感切换完成回调
     * 
     * @param callback 回调函数
     */
    void SetEmotionChangeCallback(std::function<void(EmotionType)> callback);

    /**
     * @brief 设置序列完成回调
     * 
     * @param callback 回调函数
     */
    void SetSequenceCompleteCallback(std::function<void()> callback);

    /**
     * @brief 检查情感是否已注册
     */
    bool IsEmotionRegistered(EmotionType emotion) const;

    /**
     * @brief 获取已注册的情感列表
     */
    std::vector<EmotionType> GetRegisteredEmotions() const;

    /**
     * @brief 设置默认情感（未注册的情感会使用此动画）
     */
    void SetDefaultEmotion(EmotionType emotion);

    /**
     * @brief 启用/禁用自动返回中性状态
     * 
     * @param enable 是否启用
     * @param delay_ms 延迟时间（毫秒）
     */
    void SetAutoReturnNeutral(bool enable, int delay_ms = 5000);

    /**
     * @brief 测试单个情感动画
     * 
     * @param emotion 情感类型
     * @param repeat_count 重复次数（0 表示无限循环）
     */
    void TestEmotion(EmotionType emotion, int repeat_count = 1);

private:
    EmotionAnimationManager() = default;
    ~EmotionAnimationManager() = default;

    // 禁止拷贝和赋值
    EmotionAnimationManager(const EmotionAnimationManager&) = delete;
    EmotionAnimationManager& operator=(const EmotionAnimationManager&) = delete;

    // 内部方法
    void PlayNextInSequence();
    void OnAnimationComplete();
    void ScheduleAutoReturn();
    void CancelAutoReturn();
    bool LoadAnimationFile(const EmotionAnimConfig& config);

    // 成员变量
    AnimationManager* anim_mgr_;                                    // 动画管理器
    bool initialized_;                                              // 初始化标志
    
    EmotionType current_emotion_;                                   // 当前情感
    EmotionType default_emotion_;                                   // 默认情感
    
    std::map<EmotionType, EmotionAnimConfig> emotion_configs_;      // 情感配置映射
    std::string transition_anim_path_;                              // 过渡动画路径
    
    // 序列播放相关
    std::vector<EmotionSequenceItem> current_sequence_;             // 当前播放序列
    size_t sequence_index_;                                         // 序列索引
    bool sequence_loop_;                                            // 序列是否循环
    bool playing_sequence_;                                         // 是否正在播放序列
    
    // 回调函数
    std::function<void(EmotionType)> emotion_change_callback_;      // 情感切换回调
    std::function<void()> sequence_complete_callback_;              // 序列完成回调
    
    // 自动返回中性状态
    bool auto_return_neutral_;                                      // 是否自动返回中性
    int auto_return_delay_ms_;                                      // 自动返回延迟
    void* auto_return_timer_;                                       // 自动返回定时器
    
    // 序列播放定时器（修复内存泄漏）
    void* sequence_timer_;                                          // 序列播放定时器
    void* pending_delete_timer_;                                    // 待删除的定时器（延迟删除机制）
    bool in_timer_callback_;                                        // 是否在定时器回调中
};

} // namespace lottie

