/**
 * @file ws2812_driver.h
 * @brief WS2812 RGB 灯带驱动
 * 
 * 基于 ESP-IDF led_strip 组件的 WS2812 驱动，整合以下功能：
 * - 基础颜色控制（单个/全部像素）
 * - RGB/HSV 颜色模式
 * - 多种灯效（呼吸、彩虹、流水、闪烁）
 * - 异步任务模式
 * 
 * 引脚配置（对齐参考工程）:
 * - 数据引脚: GPIO 38
 * - LED 数量: 6
 * 
 * @note 此驱动仅在 ESP32S3_BLEHD_V1 板子上生效
 */

#ifndef _WS2812_DRIVER_H_
#define _WS2812_DRIVER_H_

#include "sdkconfig.h"

// 仅在 ESP32S3_BLEHD_V1 板子上启用
#ifdef CONFIG_BOARD_TYPE_ESP32S3_BLEHD_V1

#include <driver/gpio.h>
#include <led_strip.h>
#include <esp_err.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <functional>
#include <atomic>

/* ============ 引脚定义（对齐参考工程）============ */
#define WS2812_DEFAULT_GPIO_PIN     GPIO_NUM_38
#define WS2812_DEFAULT_LED_COUNT    6

/**
 * @brief RGB 颜色结构体
 */
typedef struct {
    uint8_t r;      // 红色 (0-255)
    uint8_t g;      // 绿色 (0-255)
    uint8_t b;      // 蓝色 (0-255)
} ws2812_rgb_t;

/**
 * @brief HSV 颜色结构体
 */
typedef struct {
    uint16_t h;     // 色相 (0-360)
    uint8_t s;      // 饱和度 (0-255)
    uint8_t v;      // 明度 (0-255)
} ws2812_hsv_t;

/**
 * @brief 灯效类型枚举
 */
typedef enum {
    WS2812_EFFECT_NONE = 0,     // 无灯效（静态）
    WS2812_EFFECT_BREATH,       // 呼吸灯效
    WS2812_EFFECT_RAINBOW,      // 彩虹灯效
    WS2812_EFFECT_FLOW,         // 流水灯效
    WS2812_EFFECT_BLINK,        // 闪烁灯效
    WS2812_EFFECT_FADE_OUT,     // 渐灭灯效
    WS2812_EFFECT_SCROLL        // 滚动灯效
} ws2812_effect_t;

/**
 * @brief WS2812 驱动配置
 */
typedef struct {
    gpio_num_t gpio_pin;        // 数据引脚
    uint32_t led_count;         // LED 数量
    uint32_t rmt_resolution_hz; // RMT 时钟频率 (默认 10MHz)
    bool use_dma;               // 是否使用 DMA
} ws2812_config_t;

/**
 * @brief WS2812 RGB 灯带驱动类
 * 
 * 使用示例：
 * @code
 *     Ws2812Driver strip(GPIO_NUM_38, 6);
 *     strip.Init();
 *     
 *     // 设置所有灯为红色
 *     strip.SetAllColor({255, 0, 0});
 *     
 *     // 启动彩虹灯效
 *     strip.StartEffect(WS2812_EFFECT_RAINBOW, 50);
 * @endcode
 */
class Ws2812Driver {
public:
    /**
     * @brief 使用默认配置构造
     */
    Ws2812Driver();

    /**
     * @brief 使用指定引脚和 LED 数量构造
     * @param gpio_pin 数据引脚
     * @param led_count LED 数量
     */
    Ws2812Driver(gpio_num_t gpio_pin, uint32_t led_count);

    /**
     * @brief 使用完整配置构造
     * @param config 配置结构体
     */
    explicit Ws2812Driver(const ws2812_config_t& config);

    ~Ws2812Driver();

    /**
     * @brief 初始化驱动
     * @return ESP_OK 成功
     */
    esp_err_t Init();

    /**
     * @brief 释放资源
     */
    void Deinit();

    // ========== 基础颜色控制 ==========

    /**
     * @brief 设置单个像素颜色 (RGB)
     * @param index 像素索引
     * @param color RGB 颜色
     * @return ESP_OK 成功
     */
    esp_err_t SetPixel(uint32_t index, ws2812_rgb_t color);

    /**
     * @brief 设置单个像素颜色 (分量)
     * @param index 像素索引
     * @param r 红色
     * @param g 绿色
     * @param b 蓝色
     * @return ESP_OK 成功
     */
    esp_err_t SetPixel(uint32_t index, uint8_t r, uint8_t g, uint8_t b);

    /**
     * @brief 设置单个像素颜色 (HSV)
     * @param index 像素索引
     * @param color HSV 颜色
     * @return ESP_OK 成功
     */
    esp_err_t SetPixelHsv(uint32_t index, ws2812_hsv_t color);

    /**
     * @brief 设置单个像素颜色 (HSV 分量)
     * @param index 像素索引
     * @param h 色相 (0-360)
     * @param s 饱和度 (0-255)
     * @param v 明度 (0-255)
     * @return ESP_OK 成功
     */
    esp_err_t SetPixelHsv(uint32_t index, uint16_t h, uint8_t s, uint8_t v);

    /**
     * @brief 设置所有像素颜色 (RGB)
     * @param color RGB 颜色
     * @return ESP_OK 成功
     */
    esp_err_t SetAllColor(ws2812_rgb_t color);

    /**
     * @brief 设置所有像素颜色 (分量)
     * @param r 红色
     * @param g 绿色
     * @param b 蓝色
     * @return ESP_OK 成功
     */
    esp_err_t SetAllColor(uint8_t r, uint8_t g, uint8_t b);

    /**
     * @brief 刷新显示
     * @return ESP_OK 成功
     */
    esp_err_t Refresh();

    /**
     * @brief 清空所有像素（关灯）
     * @return ESP_OK 成功
     */
    esp_err_t Clear();

    // ========== 灯效控制 ==========

    /**
     * @brief 启动灯效
     * @param effect 灯效类型
     * @param speed_ms 灯效速度（毫秒）
     * @param color 灯效颜色（部分灯效需要）
     * @return ESP_OK 成功
     */
    esp_err_t StartEffect(ws2812_effect_t effect, uint32_t speed_ms, 
                          ws2812_rgb_t color = {255, 255, 255});

    /**
     * @brief 停止灯效
     */
    void StopEffect();

    /**
     * @brief 呼吸灯效
     * @param color 目标颜色
     * @param duration_ms 呼吸周期（毫秒）
     */
    void EffectBreath(ws2812_rgb_t color, uint32_t duration_ms);

    /**
     * @brief 彩虹灯效
     * @param speed_ms 变化速度（毫秒）
     */
    void EffectRainbow(uint32_t speed_ms);

    /**
     * @brief 流水灯效
     * @param color 流水颜色
     * @param speed_ms 流动速度（毫秒）
     */
    void EffectFlow(ws2812_rgb_t color, uint32_t speed_ms);

    /**
     * @brief 闪烁灯效
     * @param color 闪烁颜色
     * @param interval_ms 闪烁间隔（毫秒）
     */
    void EffectBlink(ws2812_rgb_t color, uint32_t interval_ms);

    /**
     * @brief 渐灭灯效
     * @param interval_ms 渐灭间隔（毫秒）
     */
    void EffectFadeOut(uint32_t interval_ms);

    /**
     * @brief 滚动灯效
     * @param low_color 背景颜色
     * @param high_color 高亮颜色
     * @param length 高亮长度
     * @param interval_ms 滚动间隔（毫秒）
     */
    void EffectScroll(ws2812_rgb_t low_color, ws2812_rgb_t high_color, 
                      int length, uint32_t interval_ms);

    // ========== 状态查询 ==========

    /**
     * @brief 检查是否已初始化
     */
    bool IsInitialized() const { return initialized_; }

    /**
     * @brief 检查灯效是否运行中
     */
    bool IsEffectRunning() const { return effect_running_.load(); }

    /**
     * @brief 获取当前灯效类型
     */
    ws2812_effect_t GetCurrentEffect() const { return current_effect_; }

    /**
     * @brief 获取 LED 数量
     */
    uint32_t GetLedCount() const { return config_.led_count; }

    /**
     * @brief 获取 GPIO 引脚
     */
    gpio_num_t GetGpioPin() const { return config_.gpio_pin; }

    /**
     * @brief 启动自动灯效任务
     */
    void StartEffectTask();

    /**
     * @brief 停止自动灯效任务
     */
    void StopEffectTask();

private:
    ws2812_config_t config_;            // 配置
    led_strip_handle_t led_strip_;      // LED 灯带句柄
    bool initialized_;                   // 初始化标志
    std::atomic<bool> effect_running_;   // 灯效运行标志
    ws2812_effect_t current_effect_;    // 当前灯效
    uint32_t effect_speed_ms_;          // 灯效速度
    ws2812_rgb_t effect_color_;         // 灯效颜色
    TaskHandle_t effect_task_;          // 灯效任务句柄
    esp_timer_handle_t effect_timer_;   // 灯效定时器
    std::function<void()> timer_callback_;  // 定时器回调

    // 像素颜色缓存
    ws2812_rgb_t* pixel_colors_;

    /**
     * @brief 启动定时器灯效
     */
    void StartTimerEffect(uint32_t interval_ms, std::function<void()> callback);

    /**
     * @brief 获取默认配置
     */
    static ws2812_config_t GetDefaultConfig();

    /**
     * @brief 灯效任务函数
     */
    static void EffectTaskFunc(void* arg);

    /**
     * @brief 定时器回调
     */
    static void TimerCallback(void* arg);
};

#endif // CONFIG_BOARD_TYPE_ESP32S3_BLEHD_V1

#endif // _WS2812_DRIVER_H_

