#pragma once

/**
 * @file emotion_quick_start.h
 * @brief 情感动画系统快速入门示例
 * 
 * 提供完整的示例代码，展示如何集成和使用情感动画系统
 */

#include "emotion_display.h"
#include "emotion_display_factory.h"
#include "emotion_setup_helper.h"
#include "emotion_animation_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace example {

static const char* TAG = "EmotionExample";

// ============================================================================
// 示例 1: 基础使用 - 创建和显示单个情感
// ============================================================================

void Example1_BasicUsage() 
{
    ESP_LOGI(TAG, "=== Example 1: Basic Usage ===");

    // 1. 创建 EmotionDisplay（假设屏幕是 240x240）
    auto* emotion_display = display::CreateEmotionDisplay(240, 240);

    // 2. 快速设置（使用标准配置）
    if (!lottie::EmotionSetupHelper::QuickSetup(emotion_display, 
                                                 lottie::EmotionSetupPreset::STANDARD)) {
        ESP_LOGE(TAG, "Quick setup failed!");
        return;
    }

    // 3. 显示情感
    emotion_display->ShowEmotion(lottie::EmotionType::HAPPY);
    ESP_LOGI(TAG, "Showing happy emotion");

    // 等待 3 秒
    vTaskDelay(pdMS_TO_TICKS(3000));

    // 4. 切换到其他情感
    emotion_display->ShowEmotion(lottie::EmotionType::EXCITED);
    ESP_LOGI(TAG, "Showing excited emotion");

    ESP_LOGI(TAG, "Example 1 completed");
}

// ============================================================================
// 示例 2: 播放情感序列
// ============================================================================

void Example2_EmotionSequence()
{
    ESP_LOGI(TAG, "=== Example 2: Emotion Sequence ===");

    auto* emotion_display = display::CreateEmotionDisplay(240, 240);
    lottie::EmotionSetupHelper::QuickSetup(emotion_display);

    // 创建情感序列：开心 -> 兴奋 -> 平静
    std::vector<lottie::EmotionSequenceItem> sequence = {
        {lottie::EmotionType::HAPPY, 2000},      // 开心 2 秒
        {lottie::EmotionType::EXCITED, 2000},    // 兴奋 2 秒
        {lottie::EmotionType::CALM, 2000}        // 平静 2 秒
    };

    // 播放序列（不循环）
    emotion_display->PlayEmotionSequence(sequence, false);
    ESP_LOGI(TAG, "Playing emotion sequence");

    // 等待序列播放完成
    vTaskDelay(pdMS_TO_TICKS(7000));

    ESP_LOGI(TAG, "Example 2 completed");
}

// ============================================================================
// 示例 3: 使用回调函数
// ============================================================================

void Example3_Callbacks()
{
    ESP_LOGI(TAG, "=== Example 3: Callbacks ===");

    auto* emotion_display = display::CreateEmotionDisplay(240, 240);
    lottie::EmotionSetupHelper::QuickSetup(emotion_display);

    // 设置情感切换回调
    emotion_display->SetEmotionChangeCallback([](lottie::EmotionType emotion) {
        ESP_LOGI(TAG, "Emotion changed to: %s", lottie::EmotionTypeToString(emotion));
    });

    // 测试几个情感
    emotion_display->ShowEmotion(lottie::EmotionType::HAPPY);
    vTaskDelay(pdMS_TO_TICKS(2000));

    emotion_display->ShowEmotion(lottie::EmotionType::SAD);
    vTaskDelay(pdMS_TO_TICKS(2000));

    emotion_display->ShowEmotion(lottie::EmotionType::SURPRISED);
    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_LOGI(TAG, "Example 3 completed");
}

// ============================================================================
// 示例 4: 与音频系统集成
// ============================================================================

// 模拟音频事件
enum class AudioEvent {
    WAKE_UP,
    START_LISTENING,
    START_THINKING,
    START_SPEAKING,
    IDLE
};

void OnAudioEvent(AudioEvent event, display::EmotionDisplay* emotion_display)
{
    switch (event) {
        case AudioEvent::WAKE_UP:
            ESP_LOGI(TAG, "Audio: Wake up");
            emotion_display->ShowEmotion(lottie::EmotionType::EXCITED);
            break;

        case AudioEvent::START_LISTENING:
            ESP_LOGI(TAG, "Audio: Start listening");
            emotion_display->ShowEmotion(lottie::EmotionType::SURPRISED);
            break;

        case AudioEvent::START_THINKING:
            ESP_LOGI(TAG, "Audio: Start thinking");
            emotion_display->ShowEmotion(lottie::EmotionType::THINKING);
            break;

        case AudioEvent::START_SPEAKING:
            ESP_LOGI(TAG, "Audio: Start speaking");
            emotion_display->ShowEmotion(lottie::EmotionType::HAPPY);
            break;

        case AudioEvent::IDLE:
            ESP_LOGI(TAG, "Audio: Idle");
            emotion_display->ShowEmotion(lottie::EmotionType::CALM);
            break;
    }
}

void Example4_AudioIntegration()
{
    ESP_LOGI(TAG, "=== Example 4: Audio Integration ===");

    auto* emotion_display = display::CreateEmotionDisplay(240, 240);
    lottie::EmotionSetupHelper::QuickSetup(emotion_display);

    // 模拟音频事件序列
    OnAudioEvent(AudioEvent::WAKE_UP, emotion_display);
    vTaskDelay(pdMS_TO_TICKS(2000));

    OnAudioEvent(AudioEvent::START_LISTENING, emotion_display);
    vTaskDelay(pdMS_TO_TICKS(2000));

    OnAudioEvent(AudioEvent::START_THINKING, emotion_display);
    vTaskDelay(pdMS_TO_TICKS(2000));

    OnAudioEvent(AudioEvent::START_SPEAKING, emotion_display);
    vTaskDelay(pdMS_TO_TICKS(2000));

    OnAudioEvent(AudioEvent::IDLE, emotion_display);

    ESP_LOGI(TAG, "Example 4 completed");
}

// ============================================================================
// 示例 5: 手动注册和管理情感
// ============================================================================

void Example5_ManualRegistration()
{
    ESP_LOGI(TAG, "=== Example 5: Manual Registration ===");

    auto* emotion_display = display::CreateEmotionDisplay(240, 240);
    emotion_display->InitEmotionSystem("/spiffs/anim/");

    // 手动注册特定情感
    auto& mgr = lottie::EmotionAnimationManager::Instance();
    
    // 注册基础情感
    mgr.RegisterEmotion(lottie::EmotionType::HAPPY, "/spiffs/anim/happy.json");
    mgr.RegisterEmotion(lottie::EmotionType::SAD, "/spiffs/anim/sad.json");
    mgr.RegisterEmotion(lottie::EmotionType::EXCITED, "/spiffs/anim/excited.json");

    // 打印已注册的情感
    lottie::EmotionSetupHelper::PrintRegisteredEmotions();

    // 测试每个情感
    emotion_display->ShowEmotion(lottie::EmotionType::HAPPY);
    vTaskDelay(pdMS_TO_TICKS(2000));

    emotion_display->ShowEmotion(lottie::EmotionType::SAD);
    vTaskDelay(pdMS_TO_TICKS(2000));

    emotion_display->ShowEmotion(lottie::EmotionType::EXCITED);
    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_LOGI(TAG, "Example 5 completed");
}

// ============================================================================
// 示例 6: 测试所有情感
// ============================================================================

void Example6_TestAllEmotions()
{
    ESP_LOGI(TAG, "=== Example 6: Test All Emotions ===");

    auto* emotion_display = display::CreateEmotionDisplay(240, 240);
    
    // 使用完整配置
    lottie::EmotionSetupHelper::QuickSetup(emotion_display, 
                                           lottie::EmotionSetupPreset::FULL);

    // 打印已注册的情感
    lottie::EmotionSetupHelper::PrintRegisteredEmotions();

    // 测试所有情感（每个 3 秒）
    lottie::EmotionSetupHelper::TestAllEmotions(3000);

    ESP_LOGI(TAG, "Example 6 completed");
}

// ============================================================================
// 示例 7: 在现有项目中集成（推荐使用）
// ============================================================================

class EmotionDisplayIntegration {
public:
    static EmotionDisplayIntegration& Instance() {
        static EmotionDisplayIntegration instance;
        return instance;
    }

    bool Init(int width, int height) {
        ESP_LOGI(TAG, "Initializing EmotionDisplay integration");

        // 创建显示对象
        emotion_display_ = display::CreateEmotionDisplay(width, height);
        if (!emotion_display_) {
            ESP_LOGE(TAG, "Failed to create EmotionDisplay");
            return false;
        }

        // 快速设置
        if (!lottie::EmotionSetupHelper::QuickSetup(emotion_display_,
                                                     lottie::EmotionSetupPreset::STANDARD)) {
            ESP_LOGE(TAG, "Failed to setup emotion system");
            return false;
        }

        // 设置回调
        emotion_display_->SetEmotionChangeCallback([this](lottie::EmotionType emotion) {
            OnEmotionChanged(emotion);
        });

        // 启用自动返回中性状态
        emotion_display_->SetAutoReturnNeutral(true, 5000);

        ESP_LOGI(TAG, "EmotionDisplay integration initialized");
        initialized_ = true;
        return true;
    }

    void ShowEmotion(lottie::EmotionType emotion) {
        if (!initialized_ || !emotion_display_) {
            ESP_LOGW(TAG, "Not initialized");
            return;
        }
        emotion_display_->ShowEmotion(emotion);
    }

    void ShowEmotion(const char* emotion_name) {
        if (!initialized_ || !emotion_display_) {
            ESP_LOGW(TAG, "Not initialized");
            return;
        }
        emotion_display_->ShowEmotion(emotion_name);
    }

    // 方便的接口
    void ShowHappy() { ShowEmotion(lottie::EmotionType::HAPPY); }
    void ShowSad() { ShowEmotion(lottie::EmotionType::SAD); }
    void ShowExcited() { ShowEmotion(lottie::EmotionType::EXCITED); }
    void ShowSleepy() { ShowEmotion(lottie::EmotionType::SLEEPY); }
    void ShowCalm() { ShowEmotion(lottie::EmotionType::CALM); }

private:
    EmotionDisplayIntegration() = default;

    void OnEmotionChanged(lottie::EmotionType emotion) {
        ESP_LOGI(TAG, ">>> Emotion changed to: %s", 
                 lottie::EmotionTypeToString(emotion));
        // 这里可以添加自定义逻辑
        // 例如：播放声音、发送事件等
    }

    display::EmotionDisplay* emotion_display_ = nullptr;
    bool initialized_ = false;
};

void Example7_ProductionIntegration()
{
    ESP_LOGI(TAG, "=== Example 7: Production Integration ===");

    // 初始化
    auto& emotion = EmotionDisplayIntegration::Instance();
    if (!emotion.Init(240, 240)) {
        ESP_LOGE(TAG, "Failed to initialize");
        return;
    }

    // 使用简洁的接口
    emotion.ShowHappy();
    vTaskDelay(pdMS_TO_TICKS(2000));

    emotion.ShowExcited();
    vTaskDelay(pdMS_TO_TICKS(2000));

    emotion.ShowSleepy();
    vTaskDelay(pdMS_TO_TICKS(2000));

    // 也可以使用字符串
    emotion.ShowEmotion("happy");
    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_LOGI(TAG, "Example 7 completed");
}

// ============================================================================
// 运行所有示例
// ============================================================================

void RunAllExamples()
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  Emotion Animation System Examples");
    ESP_LOGI(TAG, "========================================");

    Example1_BasicUsage();
    vTaskDelay(pdMS_TO_TICKS(1000));

    Example2_EmotionSequence();
    vTaskDelay(pdMS_TO_TICKS(1000));

    Example3_Callbacks();
    vTaskDelay(pdMS_TO_TICKS(1000));

    Example4_AudioIntegration();
    vTaskDelay(pdMS_TO_TICKS(1000));

    Example5_ManualRegistration();
    vTaskDelay(pdMS_TO_TICKS(1000));

    Example6_TestAllEmotions();
    vTaskDelay(pdMS_TO_TICKS(1000));

    Example7_ProductionIntegration();

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  All examples completed!");
    ESP_LOGI(TAG, "========================================");
}

} // namespace example

// ============================================================================
// 在 app_main 中使用示例
// ============================================================================

/*
// 在你的 app_main.cc 或类似文件中：

extern "C" void app_main(void)
{
    // ... 你的初始化代码 ...

    // 初始化 SPIFFS
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = false
    };
    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&conf));

    // 初始化 LVGL
    // ... LVGL 初始化代码 ...

    // 使用情感动画系统（推荐方式）
    auto& emotion = example::EmotionDisplayIntegration::Instance();
    if (emotion.Init(240, 240)) {
        // 显示初始情感
        emotion.ShowHappy();
    }

    // 或者运行所有示例（测试用）
    // example::RunAllExamples();

    // ... 你的主循环 ...
}
*/

