#include "servo_driver.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "ServoDriver"

// 默认配置常量
#define DEFAULT_PWM_FREQUENCY       50      // 50Hz - 标准舵机频率
#define DEFAULT_PWM_RESOLUTION      LEDC_TIMER_12_BIT
#define DEFAULT_MIN_PULSE_US        500     // 0.5ms -> 0°
#define DEFAULT_MAX_PULSE_US        2500    // 2.5ms -> 180°
#define DEFAULT_ANGLE               90      // 默认居中位置

servo_config_t ServoDriver::GetDefaultConfig(gpio_num_t gpio_pin) {
    return servo_config_t {
        .gpio_pin = gpio_pin,
        .timer = LEDC_TIMER_0,
        .channel = LEDC_CHANNEL_0,
        .frequency = DEFAULT_PWM_FREQUENCY,
        .resolution = DEFAULT_PWM_RESOLUTION,
        .min_pulse_us = DEFAULT_MIN_PULSE_US,
        .max_pulse_us = DEFAULT_MAX_PULSE_US,
        .default_angle = DEFAULT_ANGLE
    };
}

ServoDriver::ServoDriver(gpio_num_t gpio_pin)
    : config_(GetDefaultConfig(gpio_pin))
    , initialized_(false)
    , current_angle_(config_.default_angle) {
}

ServoDriver::ServoDriver(const servo_config_t& config)
    : config_(config)
    , initialized_(false)
    , current_angle_(config.default_angle) {
}

ServoDriver::~ServoDriver() {
    Deinit();
}

esp_err_t ServoDriver::Init() {
    if (initialized_) {
        ESP_LOGW(TAG, "Servo already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing servo driver:");
    ESP_LOGI(TAG, "  GPIO: %d", config_.gpio_pin);
    ESP_LOGI(TAG, "  Frequency: %lu Hz", config_.frequency);
    ESP_LOGI(TAG, "  Resolution: %d bits", config_.resolution);
    ESP_LOGI(TAG, "  Pulse range: %lu - %lu us", config_.min_pulse_us, config_.max_pulse_us);

    // 配置 LEDC 定时器
    ledc_timer_config_t timer_config = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = config_.resolution,
        .timer_num = config_.timer,
        .freq_hz = config_.frequency,
        .clk_cfg = LEDC_AUTO_CLK
    };
    
    esp_err_t ret = ledc_timer_config(&timer_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC timer: %s", esp_err_to_name(ret));
        return ret;
    }

    // 配置 LEDC 通道
    ledc_channel_config_t channel_config = {
        .gpio_num = config_.gpio_pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = config_.channel,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = config_.timer,
        .duty = 0,
        .hpoint = 0,
        .flags = {
            .output_invert = 0
        }
    };
    
    ret = ledc_channel_config(&channel_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC channel: %s", esp_err_to_name(ret));
        return ret;
    }

    initialized_ = true;
    
    // 设置到默认角度
    SetAngle(config_.default_angle);
    
    ESP_LOGI(TAG, "Servo driver initialized successfully");
    return ESP_OK;
}

void ServoDriver::Deinit() {
    if (!initialized_) {
        return;
    }

    ESP_LOGI(TAG, "Deinitializing servo driver");
    
    // 停止 PWM 输出
    ledc_stop(LEDC_LOW_SPEED_MODE, config_.channel, 0);
    
    initialized_ = false;
}

uint32_t ServoDriver::AngleToDuty(uint32_t angle) const {
    // 限制角度范围
    if (angle > 180) {
        angle = 180;
    }

    // 计算脉宽 (微秒)
    // pulse_us = min_pulse + (max_pulse - min_pulse) * (angle / 180)
    uint32_t pulse_us = config_.min_pulse_us + 
        (config_.max_pulse_us - config_.min_pulse_us) * angle / 180;

    // 计算占空比
    // 一个周期 = 1000000 / frequency 微秒
    // duty = pulse_us * max_duty / period_us
    uint32_t max_duty = (1 << config_.resolution) - 1;
    uint32_t period_us = 1000000 / config_.frequency;
    uint32_t duty = pulse_us * max_duty / period_us;

    return duty;
}

esp_err_t ServoDriver::SetAngle(uint32_t angle) {
    if (!initialized_) {
        ESP_LOGE(TAG, "Servo not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // 限制角度范围
    if (angle > 180) {
        ESP_LOGW(TAG, "Angle %lu exceeds max (180), clamping", angle);
        angle = 180;
    }

    uint32_t duty = AngleToDuty(angle);
    
    esp_err_t ret = ledc_set_duty(LEDC_LOW_SPEED_MODE, config_.channel, duty);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set duty: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = ledc_update_duty(LEDC_LOW_SPEED_MODE, config_.channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update duty: %s", esp_err_to_name(ret));
        return ret;
    }

    uint32_t previous_angle = current_angle_;
    current_angle_ = angle;
    
    ESP_LOGD(TAG, "Servo angle: %lu -> %lu (duty: %lu)", previous_angle, angle, duty);
    return ESP_OK;
}

esp_err_t ServoDriver::Move(uint32_t angle, servo_direction_t direction) {
    if (!initialized_) {
        ESP_LOGE(TAG, "Servo not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    int32_t target = (int32_t)current_angle_;
    int32_t delta = (int32_t)(angle / 2);  // 实际移动量 = angle / 2

    if (direction == SERVO_DIR_FORWARD) {
        target += delta;
    } else {
        target -= delta;
    }

    // 限幅
    if (target < 0) target = 0;
    if (target > 180) target = 180;

    ESP_LOGI(TAG, "Servo move: %lu -> %ld (delta: %s%ld)", 
             current_angle_, target, 
             direction == SERVO_DIR_FORWARD ? "+" : "-", delta);

    return SetAngle((uint32_t)target);
}

esp_err_t ServoDriver::Sweep(uint32_t min_angle, uint32_t max_angle, 
                              uint32_t step, uint32_t step_delay_ms, 
                              uint32_t cycles,
                              std::function<void(uint32_t)> progress_callback) {
    if (!initialized_) {
        ESP_LOGE(TAG, "Servo not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // 参数验证
    if (min_angle > 180) min_angle = 180;
    if (max_angle > 180) max_angle = 180;
    if (min_angle >= max_angle) {
        ESP_LOGE(TAG, "Invalid sweep range: %lu - %lu", min_angle, max_angle);
        return ESP_ERR_INVALID_ARG;
    }
    if (step == 0) step = 1;
    if (cycles == 0) cycles = 1;

    ESP_LOGI(TAG, "Servo sweep: %lu - %lu, step: %lu, delay: %lums, cycles: %lu",
             min_angle, max_angle, step, step_delay_ms, cycles);

    for (uint32_t cycle = 0; cycle < cycles; cycle++) {
        // 从当前位置到最小角度
        SetAngle(min_angle);
        if (progress_callback) progress_callback(min_angle);
        vTaskDelay(pdMS_TO_TICKS(step_delay_ms));

        // 向上扫描
        for (uint32_t angle = min_angle; angle <= max_angle; angle += step) {
            SetAngle(angle);
            if (progress_callback) progress_callback(angle);
            vTaskDelay(pdMS_TO_TICKS(step_delay_ms));
        }

        // 确保到达最大角度
        SetAngle(max_angle);
        if (progress_callback) progress_callback(max_angle);
        vTaskDelay(pdMS_TO_TICKS(step_delay_ms));

        // 向下扫描
        for (int32_t angle = max_angle; angle >= (int32_t)min_angle; angle -= step) {
            SetAngle((uint32_t)angle);
            if (progress_callback) progress_callback((uint32_t)angle);
            vTaskDelay(pdMS_TO_TICKS(step_delay_ms));
        }
    }

    // 回到中间位置
    uint32_t center = (min_angle + max_angle) / 2;
    SetAngle(center);

    ESP_LOGI(TAG, "Servo sweep completed, final angle: %lu", center);
    return ESP_OK;
}

