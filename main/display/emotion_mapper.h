#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <functional>

namespace xiaozhi {
namespace display {

/**
 * @brief 情感映射结果
 */
struct EmotionMappingResult {
    std::string animation_name;     // 动画文件名（如 "idle.aaf"）
    int animation_index;            // 动画索引（用于 Assets 分区）
    bool is_loop;                   // 是否循环播放
    int fps;                        // 帧率
    uint32_t timeout_ms;            // 超时时间（0=永久）
    bool success;                   // 映射是否成功
    
    EmotionMappingResult()
        : animation_index(-1)
        , is_loop(true)
        , fps(15)
        , timeout_ms(5000)
        , success(false) {}
};

/**
 * @brief 情感映射器
 * 
 * 负责将人物情感映射到动画状态。
 * 
 * 设计目标：
 * 1. 解耦情感输入与动画播放
 * 2. 支持多种情感输入源（手动、服务器、LLM）
 * 3. 提供可扩展的映射规则
 * 
 * 未来扩展点：
 * - LLM 情感分析结果输入
 * - 服务器推送的情感指令
 * - 语音情感识别结果
 * - 用户交互触发的情感
 * 
 * 使用示例：
 * @code
 *     auto& mapper = EmotionMapper::GetInstance();
 *     auto result = mapper.MapEmotion("happy");
 *     if (result.success) {
 *         // 播放 result.animation_name 动画
 *     }
 * @endcode
 */
class EmotionMapper {
public:
    /**
     * @brief 情感输入源类型
     */
    enum class InputSource {
        Manual,     // 手动测试输入（BLE 测试指令）
        Server,     // 服务器推送
        LLM,        // LLM 情感分析
        Voice,      // 语音情感识别
        Interaction // 用户交互触发
    };
    
    /**
     * @brief 情感信息结构
     */
    struct EmotionInfo {
        std::string name;               // 情感名称
        std::string display_name;       // 显示名称（中文）
        std::string category;           // 情感分类
        std::string mapped_animation;   // 映射的动画
        int animation_index;            // 动画索引
        bool is_loop;                   // 是否循环
        int fps;                        // 帧率
        uint32_t default_timeout_ms;    // 默认超时
        
        EmotionInfo()
            : animation_index(-1)
            , is_loop(true)
            , fps(15)
            , default_timeout_ms(5000) {}
    };
    
    /**
     * @brief 获取单例实例
     */
    static EmotionMapper& GetInstance();
    
    /**
     * @brief 初始化情感映射器
     * @return 是否成功
     */
    bool Initialize();
    
    /**
     * @brief 映射情感到动画
     * @param emotion 情感名称
     * @param source 输入源（用于日志和统计）
     * @return 映射结果
     */
    EmotionMappingResult MapEmotion(const std::string& emotion, 
                                     InputSource source = InputSource::Manual);
    
    /**
     * @brief 检查情感是否已注册
     * @param emotion 情感名称
     * @return 是否已注册
     */
    bool IsEmotionRegistered(const std::string& emotion) const;
    
    /**
     * @brief 获取所有已注册的情感列表
     * @return 情感名称列表
     */
    std::vector<std::string> GetRegisteredEmotions() const;
    
    /**
     * @brief 获取情感信息
     * @param emotion 情感名称
     * @return 情感信息（如果未找到返回空 EmotionInfo）
     */
    EmotionInfo GetEmotionInfo(const std::string& emotion) const;
    
    /**
     * @brief 获取情感分类列表
     * @return 分类名称列表
     */
    std::vector<std::string> GetCategories() const;
    
    /**
     * @brief 获取指定分类下的情感
     * @param category 分类名称
     * @return 情感名称列表
     */
    std::vector<std::string> GetEmotionsByCategory(const std::string& category) const;
    
    // ========================================
    // 扩展接口（用于自定义映射规则）
    // ========================================
    
    /**
     * @brief 注册自定义情感
     * @param emotion 情感名称
     * @param animation 动画文件名
     * @param index 动画索引
     * @param loop 是否循环
     * @param fps 帧率
     * @param timeout_ms 超时时间
     * @param display_name 显示名称
     * @param category 分类
     */
    void RegisterEmotion(const std::string& emotion,
                         const std::string& animation,
                         int index,
                         bool loop = true,
                         int fps = 15,
                         uint32_t timeout_ms = 5000,
                         const std::string& display_name = "",
                         const std::string& category = "custom");
    
    /**
     * @brief 设置情感变化回调（用于日志、统计、或触发其他逻辑）
     * @param callback 回调函数
     */
    using EmotionChangeCallback = std::function<void(const std::string& emotion, 
                                                      InputSource source,
                                                      const EmotionMappingResult& result)>;
    void SetEmotionChangeCallback(EmotionChangeCallback callback);
    
    // ========================================
    // LLM 扩展接口（预留）
    // ========================================
    
    /**
     * @brief 从 LLM 响应中解析情感（预留接口）
     * @param llm_response LLM 原始响应文本
     * @return 解析出的情感名称（如果无法解析返回空字符串）
     * 
     * @note 这是预留接口，未来可以实现：
     * - 解析 LLM 响应中的情感标记
     * - 支持多种 LLM 输出格式
     * - 情感置信度评估
     */
    std::string ParseEmotionFromLLM(const std::string& llm_response);
    
    /**
     * @brief 设置 LLM 情感解析器（预留接口）
     * @param parser 自定义解析函数
     * 
     * @note 可以通过这个接口注入自定义的 LLM 情感解析逻辑
     */
    using LLMEmotionParser = std::function<std::string(const std::string&)>;
    void SetLLMEmotionParser(LLMEmotionParser parser);
    
private:
    EmotionMapper();
    ~EmotionMapper() = default;
    
    // 禁止拷贝
    EmotionMapper(const EmotionMapper&) = delete;
    EmotionMapper& operator=(const EmotionMapper&) = delete;
    
    // 初始化默认映射
    void InitializeDefaultMappings();
    
    // 成员变量
    bool initialized_;
    std::unordered_map<std::string, EmotionInfo> emotion_map_;
    EmotionChangeCallback on_emotion_change_;
    LLMEmotionParser llm_parser_;
};

/**
 * @brief 输入源名称转换
 */
inline const char* InputSourceToString(EmotionMapper::InputSource source) {
    switch (source) {
        case EmotionMapper::InputSource::Manual:      return "Manual";
        case EmotionMapper::InputSource::Server:      return "Server";
        case EmotionMapper::InputSource::LLM:         return "LLM";
        case EmotionMapper::InputSource::Voice:       return "Voice";
        case EmotionMapper::InputSource::Interaction: return "Interaction";
        default:                                      return "Unknown";
    }
}

} // namespace display
} // namespace xiaozhi

