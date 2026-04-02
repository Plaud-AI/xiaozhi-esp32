#ifndef WAKE_WORD_MANAGER_H
#define WAKE_WORD_MANAGER_H

#include <string>
#include <vector>
#include <memory>

#include "audio/wake_words/wake_word_constants.h"

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
     * @param threshold 检测阈值 (0.0-1.0)，默认使用 DEFAULT_WAKE_WORD_THRESHOLD
     * @param replace 是否替换现有配置（true）或追加（false）
     * @return 是否成功
     */
    bool SetWakeWords(const std::vector<WakeWordConfig>& words, 
                      float threshold = DEFAULT_WAKE_WORD_THRESHOLD, 
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

    // ──────────────────────────────────────────────────────────────────────
    // 自定义训练模型相关（v2.2 新增）
    // ──────────────────────────────────────────────────────────────────────

    /**
     * 从 SPIFFS /model/{wakeword_id}.tflite 加载已下载的模型，热替换当前推理引擎。
     *
     * 步骤：
     *   1. 将文件读入 SPIRAM 堆内存
     *   2. 调用 TFCustomWakeWord::ReinitWithCustomModel(...)
     *   3. 保存已安装模型元数据到 NVS
     *   4. 释放旧自定义模型内存（若有）
     *
     * @param wakeword_id    训练服务分配的模型 ID
     * @param wake_word_text 唤醒词文字（注册为 class 2）
     * @param display_name   显示名称
     * @return true 成功，false 失败
     */
    bool LoadCustomModel(const std::string& wakeword_id,
                         const std::string& wake_word_text,
                         const std::string& display_name);

    /**
     * 将已安装的自定义模型元数据持久化到 NVS，重启后可恢复。
     * namespace: "ww_model", key: "installed"
     * 格式: {"wakeword_id":"...","wake_word_text":"...","display":"..."}
     */
    void SaveInstalledModelMeta(const std::string& wakeword_id,
                                const std::string& wake_word_text,
                                const std::string& display_name);

    /**
     * 启动时调用：若 NVS 中有已安装的自定义模型记录，则从 SPIFFS 加载；
     * 否则使用编译期默认模型（不修改任何配置）。
     */
    void LoadOnBoot();

private:
    WakeWordManager();
    ~WakeWordManager() = default;
    
    // 获取默认唤醒词
    std::vector<WakeWordConfig> GetDefaultWakeWords();
    
    // 验证唤醒词配置
    bool ValidateConfig(const WakeWordConfig& config);
    
    std::vector<WakeWordConfig> wake_words_;
    float threshold_ = DEFAULT_WAKE_WORD_THRESHOLD;

    // 自定义模型缓冲区（从 SPIFFS 读入 SPIRAM，由 WakeWordManager 持有）
    uint8_t* custom_model_data_ = nullptr;
    size_t   custom_model_size_ = 0;

    static constexpr const char* TAG = "WakeWordManager";
    static constexpr const char* NVS_NAMESPACE = "wake_words";
    static constexpr const char* NVS_KEY_CONFIG = "config";
    // 自定义已安装模型元数据使用独立的 namespace
    static constexpr const char* NVS_MODEL_NAMESPACE = "ww_model";
    static constexpr const char* NVS_MODEL_KEY        = "installed";
    static constexpr size_t MAX_WAKE_WORDS = 10;
};

#endif // WAKE_WORD_MANAGER_H

