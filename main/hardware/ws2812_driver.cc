/**
 * @file ws2812_driver.cc
 * @brief WS2812 RGB 灯带驱动实现
 */

#include "ws2812_driver.h"

// 仅在 ESP32S3_BLEHD_V1 板子上启用
#ifdef CONFIG_BOARD_TYPE_ESP32S3_BLEHD_V1

#include <esp_log.h>
#include <cstring>
#include <cstdlib>

#define TAG "Ws2812Driver"

// 默认 RMT 时钟频率
#define DEFAULT_RMT_RESOLUTION_HZ   (10 * 1000 * 1000)  // 10MHz

ws2812_config_t Ws2812Driver::GetDefaultConfig() {
    return ws2812_config_t {
        .gpio_pin = WS2812_DEFAULT_GPIO_PIN,
        .led_count = WS2812_DEFAULT_LED_COUNT,
        .rmt_resolution_hz = DEFAULT_RMT_RESOLUTION_HZ,
        .use_dma = false
    };
}

Ws2812Driver::Ws2812Driver()
    : config_(GetDefaultConfig())
    , led_strip_(nullptr)
    , initialized_(false)
    , effect_running_(false)
    , current_effect_(WS2812_EFFECT_NONE)
    , effect_speed_ms_(50)
    , effect_color_({255, 255, 255})
    , effect_task_(nullptr)
    , effect_timer_(nullptr)
    , timer_callback_(nullptr)
    , pixel_colors_(nullptr) {
}

Ws2812Driver::Ws2812Driver(gpio_num_t gpio_pin, uint32_t led_count)
    : config_({gpio_pin, led_count, DEFAULT_RMT_RESOLUTION_HZ, false})
    , led_strip_(nullptr)
    , initialized_(false)
    , effect_running_(false)
    , current_effect_(WS2812_EFFECT_NONE)
    , effect_speed_ms_(50)
    , effect_color_({255, 255, 255})
    , effect_task_(nullptr)
    , effect_timer_(nullptr)
    , timer_callback_(nullptr)
    , pixel_colors_(nullptr) {
}

Ws2812Driver::Ws2812Driver(const ws2812_config_t& config)
    : config_(config)
    , led_strip_(nullptr)
    , initialized_(false)
    , effect_running_(false)
    , current_effect_(WS2812_EFFECT_NONE)
    , effect_speed_ms_(50)
    , effect_color_({255, 255, 255})
    , effect_task_(nullptr)
    , effect_timer_(nullptr)
    , timer_callback_(nullptr)
    , pixel_colors_(nullptr) {
}

Ws2812Driver::~Ws2812Driver() {
    Deinit();
}

esp_err_t Ws2812Driver::Init() {
    if (initialized_) {
        ESP_LOGW(TAG, "WS2812 driver already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing WS2812 driver:");
    ESP_LOGI(TAG, "  GPIO: %d", config_.gpio_pin);
    ESP_LOGI(TAG, "  LED count: %lu", config_.led_count);

    // 配置 LED 灯带
    led_strip_config_t strip_config = {};
    strip_config.strip_gpio_num = config_.gpio_pin;
    strip_config.max_leds = config_.led_count;
    strip_config.led_model = LED_MODEL_WS2812;
    strip_config.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
    strip_config.flags.invert_out = false;

    // 配置 RMT
    led_strip_rmt_config_t rmt_config = {};
    rmt_config.clk_src = RMT_CLK_SRC_DEFAULT;
    rmt_config.resolution_hz = config_.rmt_resolution_hz;
    rmt_config.mem_block_symbols = 0;
    rmt_config.flags.with_dma = config_.use_dma;

    esp_err_t ret = led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LED strip: %s", esp_err_to_name(ret));
        return ret;
    }

    // 分配像素颜色缓存
    pixel_colors_ = (ws2812_rgb_t*)calloc(config_.led_count, sizeof(ws2812_rgb_t));
    if (pixel_colors_ == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate pixel color buffer");
        led_strip_del(led_strip_);
        led_strip_ = nullptr;
        return ESP_ERR_NO_MEM;
    }

    // 创建灯效定时器
    esp_timer_create_args_t timer_args = {
        .callback = TimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "ws2812_effect",
        .skip_unhandled_events = false,
    };
    ret = esp_timer_create(&timer_args, &effect_timer_);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to create effect timer: %s", esp_err_to_name(ret));
        // 不致命，继续
    }

    // 清空灯带
    led_strip_clear(led_strip_);

    initialized_ = true;
    ESP_LOGI(TAG, "WS2812 driver initialized successfully");
    return ESP_OK;
}

void Ws2812Driver::Deinit() {
    if (!initialized_) {
        return;
    }

    ESP_LOGI(TAG, "Deinitializing WS2812 driver");

    // 停止灯效
    StopEffect();
    StopEffectTask();

    // 删除定时器
    if (effect_timer_ != nullptr) {
        esp_timer_stop(effect_timer_);
        esp_timer_delete(effect_timer_);
        effect_timer_ = nullptr;
    }

    // 删除 LED 灯带
    if (led_strip_ != nullptr) {
        led_strip_clear(led_strip_);
        led_strip_del(led_strip_);
        led_strip_ = nullptr;
    }

    // 释放像素缓存
    if (pixel_colors_ != nullptr) {
        free(pixel_colors_);
        pixel_colors_ = nullptr;
    }

    initialized_ = false;
}

esp_err_t Ws2812Driver::SetPixel(uint32_t index, ws2812_rgb_t color) {
    return SetPixel(index, color.r, color.g, color.b);
}

esp_err_t Ws2812Driver::SetPixel(uint32_t index, uint8_t r, uint8_t g, uint8_t b) {
    if (!initialized_ || led_strip_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    if (index >= config_.led_count) {
        return ESP_ERR_INVALID_ARG;
    }

    // 缓存颜色
    pixel_colors_[index] = {r, g, b};

    return led_strip_set_pixel(led_strip_, index, r, g, b);
}

esp_err_t Ws2812Driver::SetPixelHsv(uint32_t index, ws2812_hsv_t color) {
    return SetPixelHsv(index, color.h, color.s, color.v);
}

esp_err_t Ws2812Driver::SetPixelHsv(uint32_t index, uint16_t h, uint8_t s, uint8_t v) {
    if (!initialized_ || led_strip_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    if (index >= config_.led_count) {
        return ESP_ERR_INVALID_ARG;
    }

    return led_strip_set_pixel_hsv(led_strip_, index, h, s, v);
}

esp_err_t Ws2812Driver::SetAllColor(ws2812_rgb_t color) {
    return SetAllColor(color.r, color.g, color.b);
}

esp_err_t Ws2812Driver::SetAllColor(uint8_t r, uint8_t g, uint8_t b) {
    if (!initialized_ || led_strip_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    for (uint32_t i = 0; i < config_.led_count; i++) {
        pixel_colors_[i] = {r, g, b};
        led_strip_set_pixel(led_strip_, i, r, g, b);
    }

    return led_strip_refresh(led_strip_);
}

esp_err_t Ws2812Driver::Refresh() {
    if (!initialized_ || led_strip_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    return led_strip_refresh(led_strip_);
}

esp_err_t Ws2812Driver::Clear() {
    if (!initialized_ || led_strip_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    // 清空缓存
    memset(pixel_colors_, 0, config_.led_count * sizeof(ws2812_rgb_t));

    return led_strip_clear(led_strip_);
}

void Ws2812Driver::TimerCallback(void* arg) {
    Ws2812Driver* driver = static_cast<Ws2812Driver*>(arg);
    if (driver && driver->timer_callback_) {
        driver->timer_callback_();
    }
}

void Ws2812Driver::StartTimerEffect(uint32_t interval_ms, std::function<void()> callback) {
    if (effect_timer_ == nullptr) {
        return;
    }

    // 停止之前的定时器
    esp_timer_stop(effect_timer_);

    timer_callback_ = callback;
    effect_running_.store(true);
    esp_timer_start_periodic(effect_timer_, interval_ms * 1000);
}

esp_err_t Ws2812Driver::StartEffect(ws2812_effect_t effect, uint32_t speed_ms, ws2812_rgb_t color) {
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }

    // 停止之前的灯效
    StopEffect();

    current_effect_ = effect;
    effect_speed_ms_ = speed_ms;
    effect_color_ = color;

    switch (effect) {
        case WS2812_EFFECT_BREATH:
            EffectBreath(color, speed_ms);
            break;
        case WS2812_EFFECT_RAINBOW:
            EffectRainbow(speed_ms);
            break;
        case WS2812_EFFECT_FLOW:
            EffectFlow(color, speed_ms);
            break;
        case WS2812_EFFECT_BLINK:
            EffectBlink(color, speed_ms);
            break;
        case WS2812_EFFECT_FADE_OUT:
            EffectFadeOut(speed_ms);
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

void Ws2812Driver::StopEffect() {
    if (effect_timer_ != nullptr) {
        esp_timer_stop(effect_timer_);
    }
    effect_running_.store(false);
    current_effect_ = WS2812_EFFECT_NONE;
    timer_callback_ = nullptr;
}

void Ws2812Driver::EffectBreath(ws2812_rgb_t color, uint32_t duration_ms) {
    uint32_t steps = 50;
    uint32_t delay = duration_ms / (steps * 2);

    StartTimerEffect(delay, [this, color, steps]() {
        static int brightness_step = 0;
        static bool increasing = true;

        uint8_t brightness = (brightness_step * 255) / steps;
        uint8_t r = (color.r * brightness) / 255;
        uint8_t g = (color.g * brightness) / 255;
        uint8_t b = (color.b * brightness) / 255;

        for (uint32_t i = 0; i < config_.led_count; i++) {
            led_strip_set_pixel(led_strip_, i, r, g, b);
        }
        led_strip_refresh(led_strip_);

        if (increasing) {
            brightness_step++;
            if (brightness_step >= (int)steps) {
                increasing = false;
            }
        } else {
            brightness_step--;
            if (brightness_step <= 0) {
                increasing = true;
            }
        }
    });
}

void Ws2812Driver::EffectRainbow(uint32_t speed_ms) {
    StartTimerEffect(speed_ms, [this]() {
        static uint16_t hue = 0;

        for (uint32_t i = 0; i < config_.led_count; i++) {
            uint16_t h = (hue + i * 360 / config_.led_count) % 360;
            led_strip_set_pixel_hsv(led_strip_, i, h, 255, 50);
        }
        led_strip_refresh(led_strip_);

        hue = (hue + 5) % 360;
    });
}

void Ws2812Driver::EffectFlow(ws2812_rgb_t color, uint32_t speed_ms) {
    StartTimerEffect(speed_ms, [this, color]() {
        static int pos = 0;

        led_strip_clear(led_strip_);
        led_strip_set_pixel(led_strip_, pos, color.r, color.g, color.b);
        led_strip_refresh(led_strip_);

        pos = (pos + 1) % config_.led_count;
    });
}

void Ws2812Driver::EffectBlink(ws2812_rgb_t color, uint32_t interval_ms) {
    // 缓存颜色
    for (uint32_t i = 0; i < config_.led_count; i++) {
        pixel_colors_[i] = color;
    }

    StartTimerEffect(interval_ms, [this]() {
        static bool on = true;

        if (on) {
            for (uint32_t i = 0; i < config_.led_count; i++) {
                led_strip_set_pixel(led_strip_, i, 
                    pixel_colors_[i].r, pixel_colors_[i].g, pixel_colors_[i].b);
            }
        } else {
            led_strip_clear(led_strip_);
        }
        led_strip_refresh(led_strip_);

        on = !on;
    });
}

void Ws2812Driver::EffectFadeOut(uint32_t interval_ms) {
    StartTimerEffect(interval_ms, [this]() {
        bool all_off = true;

        for (uint32_t i = 0; i < config_.led_count; i++) {
            pixel_colors_[i].r /= 2;
            pixel_colors_[i].g /= 2;
            pixel_colors_[i].b /= 2;

            if (pixel_colors_[i].r != 0 || pixel_colors_[i].g != 0 || pixel_colors_[i].b != 0) {
                all_off = false;
            }

            led_strip_set_pixel(led_strip_, i, 
                pixel_colors_[i].r, pixel_colors_[i].g, pixel_colors_[i].b);
        }

        if (all_off) {
            led_strip_clear(led_strip_);
            esp_timer_stop(effect_timer_);
            effect_running_.store(false);
        } else {
            led_strip_refresh(led_strip_);
        }
    });
}

void Ws2812Driver::EffectScroll(ws2812_rgb_t low_color, ws2812_rgb_t high_color, 
                                 int length, uint32_t interval_ms) {
    // 初始化为背景色
    for (uint32_t i = 0; i < config_.led_count; i++) {
        pixel_colors_[i] = low_color;
    }

    StartTimerEffect(interval_ms, [this, low_color, high_color, length]() {
        static int offset = 0;

        // 重置为背景色
        for (uint32_t i = 0; i < config_.led_count; i++) {
            pixel_colors_[i] = low_color;
        }

        // 设置高亮区域
        for (int j = 0; j < length; j++) {
            int idx = (offset + j) % config_.led_count;
            pixel_colors_[idx] = high_color;
        }

        // 应用颜色
        for (uint32_t i = 0; i < config_.led_count; i++) {
            led_strip_set_pixel(led_strip_, i, 
                pixel_colors_[i].r, pixel_colors_[i].g, pixel_colors_[i].b);
        }
        led_strip_refresh(led_strip_);

        offset = (offset + 1) % config_.led_count;
    });
}

void Ws2812Driver::EffectTaskFunc(void* arg) {
    Ws2812Driver* driver = static_cast<Ws2812Driver*>(arg);
    
    ESP_LOGI(TAG, "Effect task started");

    while (driver->effect_running_.load()) {
        // 红色呼吸，周期2秒，重复3次
        for (int i = 0; i < 3 && driver->effect_running_.load(); i++) {
            driver->EffectBreath({255, 0, 0}, 2000);
            vTaskDelay(pdMS_TO_TICKS(2000));
        }

        if (!driver->effect_running_.load()) break;

        // 彩虹效果5秒
        driver->EffectRainbow(50);
        vTaskDelay(pdMS_TO_TICKS(5000));

        if (!driver->effect_running_.load()) break;

        // 蓝色流水灯
        driver->EffectFlow({0, 0, 255}, 100);
        vTaskDelay(pdMS_TO_TICKS(2000));

        if (!driver->effect_running_.load()) break;

        // 静态橙色
        driver->StopEffect();
        driver->SetAllColor(255, 128, 0);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    ESP_LOGI(TAG, "Effect task exiting");
    vTaskDelete(NULL);
}

void Ws2812Driver::StartEffectTask() {
    if (!initialized_) {
        ESP_LOGE(TAG, "WS2812 driver not initialized");
        return;
    }

    if (effect_task_ != nullptr) {
        ESP_LOGW(TAG, "Effect task already running");
        return;
    }

    effect_running_.store(true);

    xTaskCreate(
        EffectTaskFunc,
        "ws2812_effect",
        4096,
        this,
        5,
        &effect_task_
    );

    ESP_LOGI(TAG, "Effect task started");
}

void Ws2812Driver::StopEffectTask() {
    if (effect_task_ == nullptr) {
        return;
    }

    ESP_LOGI(TAG, "Stopping effect task");
    effect_running_.store(false);

    // 等待任务退出
    vTaskDelay(pdMS_TO_TICKS(100));
    effect_task_ = nullptr;
}

#endif // CONFIG_BOARD_TYPE_ESP32S3_BLEHD_V1

