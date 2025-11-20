#ifndef WAKE_WORD_MANAGER_H
#define WAKE_WORD_MANAGER_H

#include <string>
#include <vector>
#include <memory>

// 前向声明
class CustomWakeWord;

/**
 * 唤醒词配置结构
 */
struct WakeWordConfig {
    std::string text;                  // 原始文本（如 "hi plaud"）
    std::string display;               // 显示名称（如 "Hi Plaud"）
    std::vector<std::string> phonemes; // 音素列表（多个变体）
    
    WakeWordConfig() = default;
    WakeWordConfig(const std::string& t, const std::string& d, const std::vector<std::string>& p)
        : text(t), display(d), phonemes(p) {}
};

/**
 * 唤醒词管理器（单例）
 * 
 * 功能：
 * - 动态管理唤醒词配置
 * - NVS 持久化存储
 * - 应用到 CustomWakeWord
 */
class WakeWordManager {
public:
    static WakeWordManager& GetInstance();
    
    // 删除拷贝构造和赋值
    WakeWordManager(const WakeWordManager&) = delete;
    WakeWordManager& operator=(const WakeWordManager&) = delete;
    
    /**
     * 设置唤醒词列表
     * @param words 唤醒词配置列表
     * @param threshold 检测阈值 (0.0-1.0)，默认 0.15
     * @param replace 是否替换现有配置（true）或追加（false）
     * @return 是否成功
     */
    bool SetWakeWords(const std::vector<WakeWordConfig>& words, 
                      float threshold = 0.15f, 
                      bool replace = true);
    
    /**
     * 获取当前唤醒词列表
     * @return 唤醒词配置列表
     */
    std::vector<WakeWordConfig> GetWakeWords() const;
    
    /**
     * 删除指定唤醒词
     * @param text 要删除的唤醒词文本
     * @return 是否成功
     */
    bool DeleteWakeWord(const std::string& text);
    
    /**
     * 清除所有唤醒词
     */
    void ClearWakeWords();
    
    /**
     * 重置为默认唤醒词
     * @return 是否成功
     */
    bool ResetToDefault();
    
    /**
     * 保存到 NVS
     * @return 是否成功
     */
    bool SaveToNVS();
    
    /**
     * 从 NVS 加载
     * @return 是否成功
     */
    bool LoadFromNVS();
    
    /**
     * 应用配置到 CustomWakeWord
     * @param wake_word CustomWakeWord 指针
     * @return 是否成功
     */
    bool ApplyToCustomWakeWord(CustomWakeWord* wake_word);
    
    /**
     * 获取当前阈值
     */
    float GetThreshold() const { return threshold_; }
    
    /**
     * 获取唤醒词数量
     */
    size_t GetCount() const { return wake_words_.size(); }
    
    /**
     * 是否为空
     */
    bool IsEmpty() const { return wake_words_.empty(); }
    
private:
    WakeWordManager();
    ~WakeWordManager() = default;
    
    // 获取默认唤醒词
    std::vector<WakeWordConfig> GetDefaultWakeWords();
    
    // 验证唤醒词配置
    bool ValidateConfig(const WakeWordConfig& config);
    
    std::vector<WakeWordConfig> wake_words_;
    float threshold_ = 0.40f;  // 提高默认阈值，减少噪声误触发
    
    static constexpr const char* TAG = "WakeWordManager";
    static constexpr const char* NVS_NAMESPACE = "wake_words";
    static constexpr const char* NVS_KEY_CONFIG = "config";
    static constexpr size_t MAX_WAKE_WORDS = 10;  // 最多支持 10 个唤醒词
};

#endif // WAKE_WORD_MANAGER_H

