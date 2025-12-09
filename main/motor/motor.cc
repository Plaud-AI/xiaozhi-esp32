#include "motor.h"

#ifdef CONFIG_ENABLE_DOLL_INTERACTION

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cmath>

#define TAG "Motor"

Motor::Motor(const std::string& name, gpio_num_t gpio_pin, 
             ledc_channel_t channel, const ServoLimits& limits)
    : name_(name),
      gpio_pin_(gpio_pin),
      channel_(channel),
      limits_(limits),
      current_angle_(limits.center_angle),
      target_angle_(limits.center_angle),
      is_moving_(false),
      initialized_(false) {
}

Motor::~Motor() {
    Deinitialize();
}

bool Motor::Initialize() {
    if (initialized_) {
        ESP_LOGW(TAG, "[%s] Already initialized", name_.c_str());
        return true;
    }

    ESP_LOGI(TAG, "[%s] Initializing on GPIO %d, channel %d", 
             name_.c_str(), gpio_pin_, channel_);
    ESP_LOGI(TAG, "[%s] Limits: %.1f ~ %.1f, center: %.1f", 
             name_.c_str(), limits_.min_angle, limits_.max_angle, limits_.center_angle);

    // 创建舵机配置
    servo_config_t config = {
        .gpio_pin = gpio_pin_,
        .timer = LEDC_TIMER_0,  // 所有舵机共用 Timer 0
        .channel = channel_,
        .frequency = 50,        // 50Hz 标准舵机频率
        .resolution = LEDC_TIMER_12_BIT,
        .min_pulse_us = 500,    // 0.5ms -> 0°
        .max_pulse_us = 2500,   // 2.5ms -> 180°
        .default_angle = (uint32_t)limits_.center_angle
    };

    servo_driver_ = std::make_unique<ServoDriver>(config);
    
    esp_err_t ret = servo_driver_->Init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[%s] Failed to initialize servo: %s", 
                 name_.c_str(), esp_err_to_name(ret));
        servo_driver_.reset();
        return false;
    }

    current_angle_ = limits_.center_angle;
    target_angle_ = limits_.center_angle;
    initialized_ = true;

    ESP_LOGI(TAG, "[%s] Initialized successfully", name_.c_str());
    return true;
}

void Motor::Deinitialize() {
    if (!initialized_) {
        return;
    }

    ESP_LOGI(TAG, "[%s] Deinitializing", name_.c_str());
    
    if (servo_driver_) {
        servo_driver_->Deinit();
        servo_driver_.reset();
    }

    initialized_ = false;
}

float Motor::ClampAngle(float angle) const {
    if (angle < limits_.min_angle) {
        return limits_.min_angle;
    }
    if (angle > limits_.max_angle) {
        return limits_.max_angle;
    }
    return angle;
}

void Motor::MoveTo(float angle) {
    if (!initialized_ || !servo_driver_) {
        ESP_LOGW(TAG, "[%s] Not initialized", name_.c_str());
        return;
    }

    // 限幅保护
    float clamped_angle = ClampAngle(angle);
    if (clamped_angle != angle) {
        ESP_LOGW(TAG, "[%s] Angle %.1f clamped to %.1f", 
                 name_.c_str(), angle, clamped_angle);
    }

    target_angle_ = clamped_angle;
    is_moving_ = true;

    // 直接设置角度（舵机会自行移动）
    esp_err_t ret = servo_driver_->SetAngle((uint32_t)clamped_angle);
    if (ret == ESP_OK) {
        current_angle_ = clamped_angle;
        ESP_LOGD(TAG, "[%s] MoveTo: %.1f°", name_.c_str(), current_angle_);
    } else {
        ESP_LOGE(TAG, "[%s] MoveTo failed: %s", name_.c_str(), esp_err_to_name(ret));
    }

    is_moving_ = false;
}

void Motor::MoveToSmooth(float angle, uint32_t duration_ms) {
    if (!initialized_ || !servo_driver_) {
        ESP_LOGW(TAG, "[%s] Not initialized", name_.c_str());
        return;
    }

    float clamped_angle = ClampAngle(angle);
    float start_angle = current_angle_;
    float delta = clamped_angle - start_angle;
    
    if (std::abs(delta) < 0.5f) {
        // 角度差太小，直接到位
        MoveTo(clamped_angle);
        return;
    }

    target_angle_ = clamped_angle;
    is_moving_ = true;

    // 计算步进参数
    const uint32_t step_interval_ms = 20;  // 每 20ms 更新一次
    uint32_t steps = duration_ms / step_interval_ms;
    if (steps < 1) steps = 1;

    ESP_LOGD(TAG, "[%s] MoveToSmooth: %.1f -> %.1f in %lu ms (%lu steps)", 
             name_.c_str(), start_angle, clamped_angle, duration_ms, steps);

    // 平滑移动
    for (uint32_t i = 0; i <= steps; i++) {
        float progress = (float)i / steps;
        float current = start_angle + delta * progress;
        servo_driver_->SetAngle((uint32_t)current);
        current_angle_ = current;
        
        if (i < steps) {
            vTaskDelay(pdMS_TO_TICKS(step_interval_ms));
        }
    }

    // 确保到达目标
    servo_driver_->SetAngle((uint32_t)clamped_angle);
    current_angle_ = clamped_angle;
    is_moving_ = false;
}

void Motor::MoveRelative(float delta_angle) {
    float target = current_angle_ + delta_angle;
    MoveTo(target);
}

void Motor::Home() {
    ESP_LOGI(TAG, "[%s] Homing to center: %.1f°", name_.c_str(), limits_.center_angle);
    MoveTo(limits_.center_angle);
}

void Motor::Stop() {
    // 舵机本身没有"停止"概念，保持当前位置
    is_moving_ = false;
    target_angle_ = current_angle_;
    ESP_LOGD(TAG, "[%s] Stop at %.1f°", name_.c_str(), current_angle_);
}

#endif // CONFIG_ENABLE_DOLL_INTERACTION
