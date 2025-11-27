#pragma once

/**
 * @file emotion_setup_helper.h
 * @brief 情感动画系统快速配置助手
 * 
 * 提供便捷的配置和初始化函数，简化情感动画系统的集成
 */

#include "emotion_display.h"
#include "emotion_animation_manager.h"
#include "esp_log.h"

namespace lottie {

/**
 * @brief 情感动画配置方案
 */
enum class EmotionSetupPreset {
    MINIMAL,        // 最小配置：只包含基础情感（开心、悲伤、中性）
    STANDARD,       // 标准配置：常用情感
    FULL,           // 完整配置：所有情感
    CUSTOM          // 自定义配置
};

/**
 * @brief 快速配置助手类
 */
class EmotionSetupHelper {
public:
    /**
     * @brief 快速初始化情感动画系统
     * 
     * @param display EmotionDisplay 对象指针
     * @param preset 配置方案
     * @param anim_dir 动画文件目录（默认 "/spiffs/anim/"）
     * @return true 成功
     * @return false 失败
     */
    static bool QuickSetup(display::EmotionDisplay* display, 
                          EmotionSetupPreset preset = EmotionSetupPreset::STANDARD,
                          const char* anim_dir = "/spiffs/anim/");

    /**
     * @brief 注册标准情感动画
     * 
     * 根据现有的动画文件自动注册
     * 
     * @param anim_dir 动画文件目录
     * @return 成功注册的情感数量
     */
    static int RegisterStandardEmotions(const char* anim_dir = "/spiffs/anim/");

    /**
     * @brief 注册基础情感动画（最小集）
     * 
     * 只注册：happy, sad, neutral
     * 
     * @param anim_dir 动画文件目录
     * @return 成功注册的情感数量
     */
    static int RegisterMinimalEmotions(const char* anim_dir = "/spiffs/anim/");

    /**
     * @brief 注册完整情感动画
     * 
     * 注册所有可用的情感类型
     * 
     * @param anim_dir 动画文件目录
     * @return 成功注册的情感数量
     */
    static int RegisterFullEmotions(const char* anim_dir = "/spiffs/anim/");

    /**
     * @brief 测试所有已注册的情感动画
     * 
     * 依次播放每个情感动画，用于验证系统工作正常
     * 
     * @param each_duration_ms 每个动画播放时长（毫秒）
     */
    static void TestAllEmotions(int each_duration_ms = 3000);

    /**
     * @brief 打印已注册的情感列表
     */
    static void PrintRegisteredEmotions();

    /**
     * @brief 验证动画文件是否存在
     * 
     * @param file_path 文件路径
     * @return true 文件存在
     * @return false 文件不存在
     */
    static bool VerifyAnimationFile(const char* file_path);

private:
    static const char* TAG;
};

// ============================================================================
// 实现部分（内联实现，简化集成）
// ============================================================================

const char* EmotionSetupHelper::TAG = "EmotionSetup";

bool EmotionSetupHelper::QuickSetup(display::EmotionDisplay* display, 
                                    EmotionSetupPreset preset,
                                    const char* anim_dir)
{
    if (!display) {
        ESP_LOGE(TAG, "EmotionDisplay is null");
        return false;
    }

    ESP_LOGI(TAG, "Quick setup with preset: %d, dir: %s", preset, anim_dir);

    // 初始化情感系统
    if (!display->InitEmotionSystem(anim_dir)) {
        ESP_LOGE(TAG, "Failed to init emotion system");
        return false;
    }

    // 根据预设注册情感
    int registered_count = 0;
    switch (preset) {
        case EmotionSetupPreset::MINIMAL:
            registered_count = RegisterMinimalEmotions(anim_dir);
            break;
        case EmotionSetupPreset::STANDARD:
            registered_count = RegisterStandardEmotions(anim_dir);
            break;
        case EmotionSetupPreset::FULL:
            registered_count = RegisterFullEmotions(anim_dir);
            break;
        case EmotionSetupPreset::CUSTOM:
            ESP_LOGI(TAG, "Custom preset - please register emotions manually");
            return true;
    }

    ESP_LOGI(TAG, "Registered %d emotions", registered_count);

    if (registered_count == 0) {
        ESP_LOGW(TAG, "No emotions registered!");
        return false;
    }

    // 设置自动返回中性状态
    display->SetAutoReturnNeutral(true, 5000);

    ESP_LOGI(TAG, "Quick setup completed successfully");
    return true;
}

int EmotionSetupHelper::RegisterStandardEmotions(const char* anim_dir)
{
    auto& mgr = EmotionAnimationManager::Instance();
    int count = 0;

    std::string dir(anim_dir);

    // 标准情感映射
    struct EmotionMapping {
        EmotionType type;
        const char* filename;
    };

    EmotionMapping mappings[] = {
        {EmotionType::HAPPY,      "happy.json"},
        {EmotionType::SAD,        "sad.json"},
        {EmotionType::SURPRISED,  "surprised.json"},
        {EmotionType::SLEEPY,     "sleepy.json"},
        {EmotionType::CALM,       "calm.json"},
        {EmotionType::EXCITED,    "excited.json"},
    };

    for (const auto& m : mappings) {
        std::string path = dir + m.filename;
        if (VerifyAnimationFile(path.c_str())) {
            mgr.RegisterEmotion(m.type, path, false, true);
            ESP_LOGI(TAG, "Registered: %s -> %s", EmotionTypeToString(m.type), path.c_str());
            count++;
        } else {
            ESP_LOGW(TAG, "Animation file not found: %s", path.c_str());
        }
    }

    return count;
}

int EmotionSetupHelper::RegisterMinimalEmotions(const char* anim_dir)
{
    auto& mgr = EmotionAnimationManager::Instance();
    int count = 0;

    std::string dir(anim_dir);

    struct EmotionMapping {
        EmotionType type;
        const char* filename;
    };

    EmotionMapping mappings[] = {
        {EmotionType::HAPPY,   "happy.json"},
        {EmotionType::SAD,     "sad.json"},
        {EmotionType::CALM,    "calm.json"},  // 用作 neutral
    };

    for (const auto& m : mappings) {
        std::string path = dir + m.filename;
        if (VerifyAnimationFile(path.c_str())) {
            mgr.RegisterEmotion(m.type, path, false, true);
            ESP_LOGI(TAG, "Registered: %s -> %s", EmotionTypeToString(m.type), path.c_str());
            count++;
        }
    }

    return count;
}

int EmotionSetupHelper::RegisterFullEmotions(const char* anim_dir)
{
    auto& mgr = EmotionAnimationManager::Instance();
    int count = 0;

    std::string dir(anim_dir);

    struct EmotionMapping {
        EmotionType type;
        const char* filename;
    };

    EmotionMapping mappings[] = {
        {EmotionType::HAPPY,      "happy.json"},
        {EmotionType::SAD,        "sad.json"},
        {EmotionType::SURPRISED,  "surprised.json"},
        {EmotionType::SLEEPY,     "sleepy.json"},
        {EmotionType::NEUTRAL,    "calm.json"},      // 使用 calm 作为 neutral
        {EmotionType::EXCITED,    "excited.json"},
        {EmotionType::CALM,       "calm.json"},
        // 额外的动画（不对应标准 EmotionType）
        // 可以通过自定义路径播放
    };

    for (const auto& m : mappings) {
        std::string path = dir + m.filename;
        if (VerifyAnimationFile(path.c_str())) {
            mgr.RegisterEmotion(m.type, path, false, true);
            ESP_LOGI(TAG, "Registered: %s -> %s", EmotionTypeToString(m.type), path.c_str());
            count++;
        }
    }

    return count;
}

void EmotionSetupHelper::TestAllEmotions(int each_duration_ms)
{
    auto& mgr = EmotionAnimationManager::Instance();
    auto emotions = mgr.GetRegisteredEmotions();

    ESP_LOGI(TAG, "Testing %d emotions, %d ms each", emotions.size(), each_duration_ms);

    for (const auto& emotion : emotions) {
        ESP_LOGI(TAG, "Testing: %s", EmotionTypeToString(emotion));
        mgr.ShowEmotion(emotion, each_duration_ms);
        vTaskDelay(pdMS_TO_TICKS(each_duration_ms + 500));
    }

    ESP_LOGI(TAG, "Test completed");
}

void EmotionSetupHelper::PrintRegisteredEmotions()
{
    auto& mgr = EmotionAnimationManager::Instance();
    auto emotions = mgr.GetRegisteredEmotions();

    ESP_LOGI(TAG, "=== Registered Emotions (%d) ===", emotions.size());
    for (const auto& emotion : emotions) {
        ESP_LOGI(TAG, "  - %s", EmotionTypeToString(emotion));
    }
    ESP_LOGI(TAG, "===========================");
}

bool EmotionSetupHelper::VerifyAnimationFile(const char* file_path)
{
    if (!file_path) return false;

    FILE* f = fopen(file_path, "r");
    if (f) {
        fclose(f);
        return true;
    }
    return false;
}

} // namespace lottie

