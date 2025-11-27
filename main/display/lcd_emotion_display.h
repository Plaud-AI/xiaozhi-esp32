#pragma once

#include "display.h"
#include "emotion_coordinator.h"
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <lvgl.h>

/**
 * @file lcd_emotion_display.h
 * @brief LCD 显示 + 情感动画的集成实现
 * 
 * 集成了：
 * - LCD 硬件初始化（来自 LcdDisplay）
 * - 情感动画系统（使用 EmotionCoordinator）
 * - 简洁的 UI（仅顶部状态栏 + 动画区域）
 */

/**
 * @brief LCD 情感显示类
 * 
 * 专门为情感动画优化的 LCD 显示实现
 */
class LcdEmotionDisplay : public Display {
public:
    /**
     * @brief 构造函数（SPI LCD）
     */
    LcdEmotionDisplay(esp_lcd_panel_io_handle_t panel_io,
                     esp_lcd_panel_handle_t panel,
                     int width, int height,
                     int offset_x = 0, int offset_y = 0,
                     bool mirror_x = false, bool mirror_y = false,
                     bool swap_xy = false);

    /**
     * @brief 析构函数
     */
    virtual ~LcdEmotionDisplay();

    // ========================================================================
    // Display 接口实现
    // ========================================================================

    bool Lock(int timeout_ms = -1) override;
    void Unlock() override;
    void SetStatus(const char* status) override;
    void ShowNotification(const char* message, int duration_ms = 3000) override;
    void SetChatMessage(const char* role, const char* content) override;
    void SetEmotion(const char* emotion) override;
    void UpdateStatusBar(bool update_all = false) override;

    // ========================================================================
    // 情感系统相关
    // ========================================================================

    /**
     * @brief 初始化情感系统（从 Assets 分区）
     * 
     * @return true 成功
     */
    bool InitEmotionSystem();

    /**
     * @brief 设置设备状态（自动映射到情感）
     * 
     * @param state 设备状态
     */
    void SetDeviceState(emotion::DeviceState state);

    /**
     * @brief 直接显示指定情感
     * 
     * @param emotion 情感状态
     */
    void ShowEmotion(emotion::EmotionState emotion);

private:
    esp_lcd_panel_io_handle_t panel_io_;
    esp_lcd_panel_handle_t panel_;
    lv_display_t* display_;

    // LVGL UI 元素
    lv_obj_t* screen_;
    lv_obj_t* status_bar_;
    lv_obj_t* status_label_;
    lv_obj_t* notification_label_;
    lv_timer_t* notification_timer_;

    bool emotion_system_initialized_;

    /**
     * @brief 初始化 LVGL
     */
    void InitializeLvgl(int offset_x, int offset_y,
                       bool mirror_x, bool mirror_y, bool swap_xy);

    /**
     * @brief 设置 UI
     */
    void SetupUI();

    /**
     * @brief 创建状态栏
     */
    void CreateStatusBar();

    /**
     * @brief 隐藏通知（定时器回调）
     */
    static void HideNotificationCallback(lv_timer_t* timer);
};


