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
    // 自定义训练模型相关（v2.3 多槽位：与编译期内置模型并列共存）
    // ──────────────────────────────────────────────────────────────────────

    /**
     * 已安装的自定义模型描述（运行时状态 + 持久化元数据）。
     */
    struct InstalledModel {
        std::string wakeword_id;     // 训练服务分配的 ID
        std::string wake_word_text;  // 唤醒词文字（label）
        std::string display;         // 显示名称
        std::string file_md5;        // 文件 MD5（启动校验用）
        size_t file_size = 0;        // 文件字节数
        uint8_t* buffer = nullptr;   // 运行时 SPIRAM buffer（由本类持有）
    };

    /**
     * 从 SPIFFS /ww/{wakeword_id}.tflite 加载模型并注册到 MicroWakeWord。
     * 与编译期内置模型并列共存；若已存在相同 wakeword_id 则先卸载再重注册。
     * 加载成功后调用 DisableModelsByLabel 把同 label 的内置模型禁用，
     * 让动态模型覆盖编译期版本。
     *
     * @param wakeword_id    训练服务分配的 ID
     * @param wake_word_text 唤醒词文字（匹配 label 用，忽略大小写）
     * @param display_name   显示名称
     * @param expected_md5   文件 MD5（可空）
     * @param expected_size  文件字节数（0 表示不校验）
     * @return true 成功
     */
    bool LoadCustomModel(const std::string& wakeword_id,
                         const std::string& wake_word_text,
                         const std::string& display_name,
                         const std::string& expected_md5 = "",
                         size_t expected_size = 0);

    /**
     * 卸载指定 wakeword_id 的动态模型（从 MicroWakeWord 移除 + 释放 SPIRAM）。
     * 同时更新 NVS 列表。不恢复被覆盖的内置模型（需要用户另行 enable_model）。
     */
    bool UnloadCustomModel(const std::string& wakeword_id);

    /**
     * 获取当前已安装的动态模型列表（只读，buffer 字段用户应忽略）。
     */
    std::vector<InstalledModel> GetInstalledModels() const;

    /**
     * 启动时调用：遍历 NVS 中的动态模型列表，对每条记录做 MD5 完整性校验，
     * 通过的模型调用 LoadCustomModel 注册到推理引擎；失败的清理掉。
     * 编译期内置模型不受影响。
     */
    void LoadOnBoot();

    /**
     * 把 installed_models_ 整体持久化到 NVS。
     * namespace: "ww_model", key: "list"
     * 格式: {"version":2,"list":[{wakeword_id,wake_word_text,display,file_md5,file_size},...]}
     */
    void SaveInstalledModelList();

    /**
     * 清除 NVS 中自定义模型列表（会清空所有槽位记录，但不卸载当前已加载的引擎实例）。
     */
    void ClearInstalledModelMeta();

private:
    WakeWordManager();
    ~WakeWordManager() = default;
    
    // 获取默认唤醒词
    std::vector<WakeWordConfig> GetDefaultWakeWords();
    
    // 验证唤醒词配置
    bool ValidateConfig(const WakeWordConfig& config);
    
    std::vector<WakeWordConfig> wake_words_;
    float threshold_ = DEFAULT_WAKE_WORD_THRESHOLD;

    // 已安装的动态模型（buffer 字段由本类持有，生命周期绑定 MicroWakeWord 注册）
    std::vector<InstalledModel> installed_models_;

    static constexpr const char* TAG = "WakeWordManager";
    static constexpr const char* NVS_NAMESPACE = "wake_words";
    static constexpr const char* NVS_KEY_CONFIG = "config";
    // 自定义已安装模型列表 NVS
    static constexpr const char* NVS_MODEL_NAMESPACE = "ww_model";
    static constexpr const char* NVS_MODEL_KEY        = "list";      // v2: 列表
    static constexpr const char* NVS_MODEL_KEY_LEGACY = "installed"; // v1: 单槽（升级兼容）
    static constexpr size_t MAX_WAKE_WORDS = 10;
    // 动态模型注册到 MicroWakeWord 时的参数
    static constexpr float  DYN_PROBABILITY_CUTOFF  = 0.70f;
    static constexpr size_t DYN_SLIDING_WINDOW_SIZE = 5;
    static constexpr size_t DYN_TENSOR_ARENA_SIZE   = 32 * 1024;  // 32KB（训练模型偏小）
    static constexpr size_t MAX_CUSTOM_MODEL_BYTES  = 900 * 1024;

    // 格式化 MicroWakeWord 的 model_id（dyn_ 前缀避免与内置模型冲突）
    static std::string MakeDynamicModelId(const std::string& wakeword_id) {
        return std::string("dyn_") + wakeword_id;
    }
};

#endif // WAKE_WORD_MANAGER_H

