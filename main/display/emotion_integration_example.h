#pragma once

/**
 * @file emotion_integration_example.h
 * @brief 情感动画系统集成示例
 * 
 * 本文件展示如何将情感动画系统集成到主应用程序中
 * 
 * 集成步骤：
 * 1. 在系统初始化时调用 InitEmotionSystem()
 * 2. 准备好 Lottie 动画文件（放在 /spiffs/emotions/ 目录）
 * 3. 从 AI/MCP/其他模块接收情感输入
 * 4. 调用对应的情感显示函数
 */

#include "emotion_animation_manager.h"
#include "animation_manager.h"
#include "esp_log.h"

namespace emotion {

/**
 * @brief 情感系统集成助手类
 */
class EmotionSystemIntegration {
public:
    /**
     * @brief 初始化情感动画系统
     * 
     * @param lvgl_parent LVGL 父对象
     * @param anim_width 动画宽度
     * @param anim_height 动画高度
     * @param emotions_dir 情感动画文件目录
     * @return true 成功
     * @return false 失败
     */
    static bool Init(lv_obj_t* lvgl_parent, 
                    int anim_width = 240, 
                    int anim_height = 240,
                    const char* emotions_dir = "/spiffs/emotions/") {
        
        ESP_LOGI("EmotionIntegration", "Initializing emotion system...");
        
        // 1. 初始化基础动画管理器
        lottie::AnimationManager::Instance().Init(lvgl_parent, anim_width, anim_height);
        
        // 2. 初始化情感动画管理器
        if (!lottie::EmotionAnimationManager::Instance().Init()) {
            ESP_LOGE("EmotionIntegration", "Failed to init EmotionAnimationManager");
            return false;
        }
        
        // 3. 注册情感动画
        auto& mgr = lottie::EmotionAnimationManager::Instance();
        int count = mgr.RegisterEmotionsFromDirectory(emotions_dir);
        ESP_LOGI("EmotionIntegration", "Registered %d emotions", count);
        
        // 4. 设置默认情感
        mgr.SetDefaultEmotion(lottie::EmotionType::NEUTRAL);
        
        // 5. 启用自动返回中性状态（5秒后）
        mgr.SetAutoReturnNeutral(true, 5000);
        
        // 6. 设置回调（可选）
        SetupCallbacks();
        
        ESP_LOGI("EmotionIntegration", "Emotion system initialized successfully");
        return true;
    }
    
    /**
     * @brief 处理来自 AI 的情感输入
     * 
     * @param emotion_str 情感字符串（如 "happy", "sad"）
     * @param duration_ms 持续时间（-1 表示持续到下次切换）
     */
    static void HandleAIEmotion(const char* emotion_str, int duration_ms = 0) {
        if (!emotion_str) return;
        
        lottie::EmotionType emotion = lottie::StringToEmotionType(emotion_str);
        lottie::EmotionAnimationManager::Instance().ShowEmotion(emotion, duration_ms);
        
        ESP_LOGI("EmotionIntegration", "AI emotion: %s", emotion_str);
    }
    
    /**
     * @brief 处理系统事件触发的情感
     * 
     * 根据系统事件自动选择合适的情感
     */
    static void HandleSystemEvent(const char* event_type) {
        auto& mgr = lottie::EmotionAnimationManager::Instance();
        
        if (strcmp(event_type, "wifi_connecting") == 0) {
            mgr.ShowEmotion(lottie::EmotionType::CONFUSED, -1);
        }
        else if (strcmp(event_type, "wifi_connected") == 0) {
            mgr.ShowEmotion(lottie::EmotionType::HAPPY, 2000);
        }
        else if (strcmp(event_type, "wifi_failed") == 0) {
            mgr.ShowEmotion(lottie::EmotionType::SAD, 3000);
        }
        else if (strcmp(event_type, "low_battery") == 0) {
            mgr.ShowEmotion(lottie::EmotionType::SLEEPY, -1);
        }
        else if (strcmp(event_type, "error") == 0) {
            mgr.ShowEmotion(lottie::EmotionType::ANGRY, 2000);
        }
        else if (strcmp(event_type, "thinking") == 0) {
            mgr.ShowEmotion(lottie::EmotionType::THINKING, -1);
        }
        else if (strcmp(event_type, "listening") == 0) {
            mgr.ShowEmotion(lottie::EmotionType::NEUTRAL, -1);
        }
        else if (strcmp(event_type, "speaking") == 0) {
            mgr.ShowEmotion(lottie::EmotionType::HAPPY, -1);
        }
    }
    
    /**
     * @brief 播放问候序列
     * 
     * 系统启动或唤醒时播放
     */
    static void PlayGreetingSequence() {
        std::vector<lottie::EmotionSequenceItem> sequence;
        sequence.emplace_back(lottie::EmotionType::NEUTRAL, 500);
        sequence.emplace_back(lottie::EmotionType::HAPPY, 1500, true);
        sequence.emplace_back(lottie::EmotionType::EXCITED, 1000, false);
        
        lottie::EmotionAnimationManager::Instance().PlayEmotionSequence(sequence, false);
    }
    
    /**
     * @brief 播放道别序列
     * 
     * 系统休眠或关闭时播放
     */
    static void PlayGoodbyeSequence() {
        std::vector<lottie::EmotionSequenceItem> sequence;
        sequence.emplace_back(lottie::EmotionType::HAPPY, 1000);
        sequence.emplace_back(lottie::EmotionType::CALM, 1000, true);
        sequence.emplace_back(lottie::EmotionType::SLEEPY, 1500, true);
        
        lottie::EmotionAnimationManager::Instance().PlayEmotionSequence(sequence, false);
    }

private:
    /**
     * @brief 设置回调函数
     */
    static void SetupCallbacks() {
        auto& mgr = lottie::EmotionAnimationManager::Instance();
        
        // 情感切换回调
        mgr.SetEmotionChangeCallback([](lottie::EmotionType emotion) {
            ESP_LOGI("EmotionIntegration", "Emotion changed: %s", 
                     lottie::EmotionTypeToString(emotion));
            
            // TODO: 在这里同步其他模块
            // - LED 颜色/动画
            // - 音效提示
            // - 事件总线通知
            // - 日志记录
        });
        
        // 序列完成回调
        mgr.SetSequenceCompleteCallback([]() {
            ESP_LOGI("EmotionIntegration", "Emotion sequence completed");
            
            // TODO: 执行后续操作
        });
    }
};

} // namespace emotion

// ============================================================================
// C 接口（方便从 C 代码调用）
// ============================================================================

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化情感系统（C 接口）
 * 
 * @param lvgl_parent LVGL 父对象
 * @return 0 成功，-1 失败
 */
inline int emotion_system_init(lv_obj_t* lvgl_parent) {
    return emotion::EmotionSystemIntegration::Init(lvgl_parent) ? 0 : -1;
}

/**
 * @brief 处理 AI 情感（C 接口）
 * 
 * @param emotion_str 情感字符串
 * @param duration_ms 持续时间
 */
inline void emotion_handle_ai(const char* emotion_str, int duration_ms) {
    emotion::EmotionSystemIntegration::HandleAIEmotion(emotion_str, duration_ms);
}

/**
 * @brief 处理系统事件（C 接口）
 * 
 * @param event_type 事件类型
 */
inline void emotion_handle_event(const char* event_type) {
    emotion::EmotionSystemIntegration::HandleSystemEvent(event_type);
}

/**
 * @brief 播放问候序列（C 接口）
 */
inline void emotion_play_greeting() {
    emotion::EmotionSystemIntegration::PlayGreetingSequence();
}

/**
 * @brief 播放道别序列（C 接口）
 */
inline void emotion_play_goodbye() {
    emotion::EmotionSystemIntegration::PlayGoodbyeSequence();
}

#ifdef __cplusplus
}
#endif

