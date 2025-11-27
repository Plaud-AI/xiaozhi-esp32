#pragma once

#include "emotion_display.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"

namespace display {

/**
 * @file emotion_display_factory.h
 * @brief EmotionDisplay 工厂函数
 * 
 * 提供便捷的创建函数，用于在不同硬件上创建 EmotionDisplay
 */

/**
 * @brief 创建 SPI LCD 情感显示
 * 
 * @param panel_io LCD IO 句柄
 * @param panel LCD 面板句柄
 * @param width 屏幕宽度
 * @param height 屏幕高度
 * @return EmotionDisplay* 显示对象指针
 */
EmotionDisplay* CreateSpiLcdEmotionDisplay(
    esp_lcd_panel_io_handle_t panel_io,
    esp_lcd_panel_handle_t panel,
    int width, 
    int height);

/**
 * @brief 创建 RGB LCD 情感显示
 * 
 * @param panel_io LCD IO 句柄
 * @param panel LCD 面板句柄
 * @param width 屏幕宽度
 * @param height 屏幕高度
 * @return EmotionDisplay* 显示对象指针
 */
EmotionDisplay* CreateRgbLcdEmotionDisplay(
    esp_lcd_panel_io_handle_t panel_io,
    esp_lcd_panel_handle_t panel,
    int width, 
    int height);

/**
 * @brief 创建标准情感显示（基于已有的 LVGL 初始化）
 * 
 * 如果 LVGL 已经初始化，使用此函数
 * 
 * @param width 屏幕宽度
 * @param height 屏幕高度
 * @return EmotionDisplay* 显示对象指针
 */
EmotionDisplay* CreateEmotionDisplay(int width, int height);

} // namespace display

