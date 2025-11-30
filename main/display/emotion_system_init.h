#pragma once

/**
 * @file emotion_system_init.h
 * @brief 情感系统自动初始化辅助函数
 * 
 * 在板子初始化后自动调用，集成到现有的 Display 系统
 */

#include "emotion_coordinator.h"
#include "display.h"
#include "esp_log.h"

namespace emotion {

static const char* TAG = "EmotionInit";

/**
 * @brief 初始化情感系统
 * 
 * 在 Board 初始化完成后调用此函数
 * 
 * @param display 现有的 Display 对象（LcdDisplay 或其他）
 * @param anim_path 动画文件路径
 * @param width 屏幕宽度
 * @param height 屏幕高度
 * @return true 成功
 */
inline bool InitEmotionSystem(Display* display, 
                               const char* anim_path = "/spiffs/anim/",
                               int width = 240,
                               int height = 240)
{
    if (!display) {
        ESP_LOGW(TAG, "Display is null, cannot init emotion system");
        return false;
    }

    ESP_LOGI(TAG, "Initializing emotion system (integrated mode)");

    // 获取 LVGL 屏幕对象
    // 注意：这里假设 display 已经初始化了 LVGL
    // 如果是 LcdDisplay，LVGL 应该已经运行
    lv_obj_t* screen = lv_scr_act();
    if (!screen) {
        ESP_LOGE(TAG, "LVGL screen not available");
        return false;
    }

    // 初始化情感协调器
    auto& coordinator = EmotionCoordinator::Instance();
    
    emotion::EmotionSystemConfig config;
    config.animation_base_path = anim_path;
    config.screen_width = width;
    config.screen_height = height;
    config.default_emotion = EmotionState::CALM;  // 默认平静状态
    config.auto_register_mappings = true;
    config.enable_auto_restore = true;
    config.auto_restore_delay_ms = 5000;

    if (!coordinator.Init(config, screen)) {
        ESP_LOGE(TAG, "Failed to initialize emotion coordinator");
        return false;
    }

    // 设置状态变化回调（可选）
    coordinator.SetStateChangeCallback([](const EmotionChangeEvent& event) {
        ESP_LOGI(TAG, ">>> Emotion: %s -> %s", 
                 EmotionStateToString(event.from_state),
                 EmotionStateToString(event.to_state));
    });

    ESP_LOGI(TAG, "Emotion system initialized successfully!");
    
    // 打印系统信息
    coordinator.PrintSystemInfo();

    return true;
}

/**
 * @brief 快速初始化（使用默认参数）
 * 
 * @param display Display 对象
 * @return true 成功
 */
inline bool QuickInitEmotionSystem(Display* display)
{
    return InitEmotionSystem(display, "/spiffs/anim/", 240, 240);
}

} // namespace emotion


