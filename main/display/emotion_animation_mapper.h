#pragma once

/**
 * @file emotion_animation_mapper.h
 * @brief 情感到动画映射模块
 * 
 * 负责将情感状态映射到具体的 Lottie 动画文件
 * 特性：
 * - 一对多映射（一种情感可以对应多个动画）
 * - 支持随机选择
 * - 支持权重选择
 * - 支持动画序列
 * - 支持回退机制（找不到动画时的处理）
 */

#include "emotion_state_manager.h"
#include <string>
#include <vector>
#include <map>
#include <random>

namespace emotion {

/**
 * @brief 动画选择策略
 */
enum class AnimationSelectionStrategy {
    SEQUENTIAL,     // 顺序选择
    RANDOM,         // 随机选择
    WEIGHTED,       // 权重选择
    ROUND_ROBIN     // 轮询选择
};

/**
 * @brief 动画配置
 */
struct AnimationInfo {
    std::string file_path;      // 动画文件路径
    int weight;                 // 权重（用于权重选择）
    bool loop;                  // 是否循环
    int duration_ms;            // 建议播放时长（-1 表示使用动画本身时长）
    
    AnimationInfo()
        : weight(1), loop(false), duration_ms(-1) {}
        
    AnimationInfo(const std::string& path, int w = 1, bool l = false, int dur = -1)
        : file_path(path), weight(w), loop(l), duration_ms(dur) {}
};

/**
 * @brief 情感动画映射配置
 */
struct EmotionAnimationMapping {
    EmotionState emotion;                       // 情感类型
    std::vector<AnimationInfo> animations;      // 动画列表
    AnimationSelectionStrategy strategy;        // 选择策略
    std::string fallback_animation;             // 回退动画（找不到时使用）
    
    EmotionAnimationMapping()
        : emotion(EmotionState::UNKNOWN)
        , strategy(AnimationSelectionStrategy::RANDOM) {}
};

/**
 * @brief 情感动画映射器
 * 
 * 核心功能：
 * - 管理情感到动画的映射关系
 * - 根据策略选择动画
 * - 提供默认映射配置
 */
class EmotionAnimationMapper {
public:
    /**
     * @brief 获取单例
     */
    static EmotionAnimationMapper& Instance();

    /**
     * @brief 初始化
     * 
     * @param anim_base_path 动画文件基础路径（如 "/spiffs/anim/"）
     */
    void Init(const std::string& anim_base_path = "/spiffs/anim/");

    /**
     * @brief 注册单个动画映射
     * 
     * @param emotion 情感类型
     * @param animation_path 动画文件路径
     * @param weight 权重
     * @param loop 是否循环
     */
    void RegisterAnimation(EmotionState emotion, const std::string& animation_path, 
                          int weight = 1, bool loop = false);

    /**
     * @brief 注册多个动画映射
     * 
     * @param emotion 情感类型
     * @param animations 动画列表
     * @param strategy 选择策略
     */
    void RegisterAnimations(EmotionState emotion, 
                           const std::vector<AnimationInfo>& animations,
                           AnimationSelectionStrategy strategy = AnimationSelectionStrategy::RANDOM);

    /**
     * @brief 批量注册标准映射（基于文件命名）
     * 
     * 自动扫描 base_path，根据文件名注册映射
     * 例如：happy.json -> HAPPY, sad.json -> SAD
     * 
     * @return 成功注册的数量
     */
    int RegisterStandardMappings();

    /**
     * @brief 注册默认映射配置
     * 
     * 使用项目中已有的动画文件创建默认映射
     * 
     * @return 成功注册的数量
     */
    int RegisterDefaultMappings();

    /**
     * @brief 获取情感对应的动画
     * 
     * 根据映射策略选择并返回动画路径
     * 
     * @param emotion 情感类型
     * @param[out] out_info 输出动画信息
     * @return true 找到动画
     * @return false 未找到（会使用回退动画）
     */
    bool GetAnimation(EmotionState emotion, AnimationInfo& out_info);

    /**
     * @brief 获取情感对应的所有动画
     * 
     * @param emotion 情感类型
     * @return 动画列表
     */
    std::vector<AnimationInfo> GetAllAnimations(EmotionState emotion) const;

    /**
     * @brief 设置选择策略
     * 
     * @param emotion 情感类型
     * @param strategy 策略
     */
    void SetSelectionStrategy(EmotionState emotion, AnimationSelectionStrategy strategy);

    /**
     * @brief 设置回退动画
     * 
     * @param emotion 情感类型
     * @param fallback_path 回退动画路径
     */
    void SetFallbackAnimation(EmotionState emotion, const std::string& fallback_path);

    /**
     * @brief 设置全局回退动画
     * 
     * @param fallback_path 回退动画路径
     */
    void SetGlobalFallback(const std::string& fallback_path);

    /**
     * @brief 检查情感是否有映射
     * 
     * @param emotion 情感类型
     * @return true 有映射
     */
    bool HasMapping(EmotionState emotion) const;

    /**
     * @brief 获取所有已映射的情感
     * 
     * @return 情感列表
     */
    std::vector<EmotionState> GetMappedEmotions() const;

    /**
     * @brief 清除所有映射
     */
    void ClearAllMappings();

    /**
     * @brief 清除指定情感的映射
     * 
     * @param emotion 情感类型
     */
    void ClearMapping(EmotionState emotion);

    /**
     * @brief 打印映射信息（调试用）
     */
    void PrintMappings() const;

private:
    EmotionAnimationMapper() = default;
    ~EmotionAnimationMapper() = default;

    // 禁止拷贝和赋值
    EmotionAnimationMapper(const EmotionAnimationMapper&) = delete;
    EmotionAnimationMapper& operator=(const EmotionAnimationMapper&) = delete;

    // 内部方法
    AnimationInfo SelectAnimation(const EmotionAnimationMapping& mapping);
    AnimationInfo SelectSequential(const EmotionAnimationMapping& mapping);
    AnimationInfo SelectRandom(const EmotionAnimationMapping& mapping);
    AnimationInfo SelectWeighted(const EmotionAnimationMapping& mapping);
    AnimationInfo SelectRoundRobin(const EmotionAnimationMapping& mapping);
    bool FileExists(const std::string& path) const;

    // 成员变量
    std::string anim_base_path_;
    std::map<EmotionState, EmotionAnimationMapping> mappings_;
    std::map<EmotionState, size_t> round_robin_index_;  // 轮询索引
    std::string global_fallback_;
    
    std::random_device rd_;
    std::mt19937 rng_;
    
    bool initialized_;
};

} // namespace emotion

