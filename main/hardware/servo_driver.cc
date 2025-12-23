/**
 * @file servo_driver.cc
 * @brief 舵机驱动实现 - 支持单舵机和双舵机模式
 */

#include "servo_driver.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "ServoDriver"

// ============================================================================
// ServoDriver 单舵机类实现
// ============================================================================

servo_config_t ServoDriver::GetDefaultConfig(gpio_num_t gpio_pin) {
    return servo_config_t {
        .gpio_pin = gpio_pin,
        .timer = SERVO_PWM_TIMER,
        .channel = SERVO1_CHANNEL,
        .frequency = SERVO_PWM_FREQUENCY,
        .resolution = SERVO_PWM_RESOLUTION,
        .min_pulse_us = SERVO_DEFAULT_MIN_PULSE_US,
        .max_pulse_us = SERVO_DEFAULT_MAX_PULSE_US,
        .default_angle = SERVO_DEFAULT_ANGLE
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


// ============================================================================
// DualServoController 双舵机控制器实现（仅 ESP32S3_BLEHD_V1）
// ============================================================================

#if SERVO_DUAL_MODE_ENABLED

#define DUAL_TAG "DualServo"

DualServoController::DualServoController()
    : servo1_pin_(SERVO1_GPIO_PIN)
    , servo2_pin_(SERVO2_GPIO_PIN)
    , initialized_(false)
    , timer_initialized_(false)
    , demo_running_(false)
    , demo_task_(nullptr) {
    servo_angles_[SERVO_ID_1] = SERVO_DEFAULT_ANGLE;
    servo_angles_[SERVO_ID_2] = SERVO_DEFAULT_ANGLE;
}

DualServoController::DualServoController(gpio_num_t servo1_pin, gpio_num_t servo2_pin)
    : servo1_pin_(servo1_pin)
    , servo2_pin_(servo2_pin)
    , initialized_(false)
    , timer_initialized_(false)
    , demo_running_(false)
    , demo_task_(nullptr) {
    servo_angles_[SERVO_ID_1] = SERVO_DEFAULT_ANGLE;
    servo_angles_[SERVO_ID_2] = SERVO_DEFAULT_ANGLE;
}

DualServoController::~DualServoController() {
    Deinit();
}

esp_err_t DualServoController::Init() {
    if (initialized_) {
        ESP_LOGW(DUAL_TAG, "Dual servo controller already initialized");
        return ESP_OK;
    }

    ESP_LOGI(DUAL_TAG, "Initializing dual servo controller:");
    ESP_LOGI(DUAL_TAG, "  Servo1 GPIO: %d", servo1_pin_);
    ESP_LOGI(DUAL_TAG, "  Servo2 GPIO: %d", servo2_pin_);

    // 配置 LEDC 定时器（两个通道共用一个定时器）
    ledc_timer_config_t timer_config = {
        .speed_mode = SERVO_PWM_MODE,
        .duty_resolution = SERVO_PWM_RESOLUTION,
        .timer_num = SERVO_PWM_TIMER,
        .freq_hz = SERVO_PWM_FREQUENCY,
        .clk_cfg = LEDC_AUTO_CLK
    };
    
    esp_err_t ret = ledc_timer_config(&timer_config);
    if (ret != ESP_OK) {
        ESP_LOGE(DUAL_TAG, "Failed to configure LEDC timer: %s", esp_err_to_name(ret));
        return ret;
    }
    timer_initialized_ = true;

    // 配置舵机1通道
    ledc_channel_config_t channel1_config = {
        .gpio_num = servo1_pin_,
        .speed_mode = SERVO_PWM_MODE,
        .channel = SERVO1_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = SERVO_PWM_TIMER,
        .duty = 0,
        .hpoint = 0,
        .flags = {
            .output_invert = 0
        }
    };
    ret = ledc_channel_config(&channel1_config);
    if (ret != ESP_OK) {
        ESP_LOGE(DUAL_TAG, "Failed to configure servo1 channel: %s", esp_err_to_name(ret));
        return ret;
    }

    // 配置舵机2通道
    ledc_channel_config_t channel2_config = {
        .gpio_num = servo2_pin_,
        .speed_mode = SERVO_PWM_MODE,
        .channel = SERVO2_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = SERVO_PWM_TIMER,
        .duty = 0,
        .hpoint = 0,
        .flags = {
            .output_invert = 0
        }
    };
    ret = ledc_channel_config(&channel2_config);
    if (ret != ESP_OK) {
        ESP_LOGE(DUAL_TAG, "Failed to configure servo2 channel: %s", esp_err_to_name(ret));
        return ret;
    }

    initialized_ = true;

    // 设置两个舵机到默认角度
    SetBothAngles(SERVO_DEFAULT_ANGLE, SERVO_DEFAULT_ANGLE);

    ESP_LOGI(DUAL_TAG, "Dual servo controller initialized successfully");
    return ESP_OK;
}

void DualServoController::Deinit() {
    if (!initialized_) {
        return;
    }

    ESP_LOGI(DUAL_TAG, "Deinitializing dual servo controller");

    // 停止演示任务
    StopDemoTask();

    // 停止两个舵机的 PWM 输出
    ledc_stop(SERVO_PWM_MODE, SERVO1_CHANNEL, 0);
    ledc_stop(SERVO_PWM_MODE, SERVO2_CHANNEL, 0);

    initialized_ = false;
    timer_initialized_ = false;
}

uint32_t DualServoController::AngleToDuty(uint32_t angle) const {
    // 限制角度范围
    if (angle > 180) {
        angle = 180;
    }

    // 12位分辨率下：0.5ms对应102，2.5ms对应512
    const float duty_min = 102.0f;  // 0.5ms -> 0°
    const float duty_max = 512.0f;  // 2.5ms -> 180°

    float duty = duty_min + (duty_max - duty_min) * ((float)angle / 180.0f);
    return (uint32_t)duty;
}

esp_err_t DualServoController::UpdateDuty(servo_id_t servo_id, uint32_t angle) {
    if (servo_id >= SERVO_ID_MAX) {
        ESP_LOGE(DUAL_TAG, "Invalid servo ID: %d", servo_id);
        return ESP_ERR_INVALID_ARG;
    }

    ledc_channel_t channel = (servo_id == SERVO_ID_1) ? SERVO1_CHANNEL : SERVO2_CHANNEL;
    uint32_t duty = AngleToDuty(angle);

    esp_err_t ret = ledc_set_duty(SERVO_PWM_MODE, channel, duty);
    if (ret != ESP_OK) {
        return ret;
    }

    return ledc_update_duty(SERVO_PWM_MODE, channel);
}

esp_err_t DualServoController::SetAngle(servo_id_t servo_id, uint32_t angle) {
    if (!initialized_) {
        ESP_LOGE(DUAL_TAG, "Dual servo controller not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (servo_id >= SERVO_ID_MAX) {
        ESP_LOGE(DUAL_TAG, "Invalid servo ID: %d", servo_id);
        return ESP_ERR_INVALID_ARG;
    }

    // 限制角度范围
    if (angle > 180) {
        angle = 180;
    }

    esp_err_t ret = UpdateDuty(servo_id, angle);
    if (ret == ESP_OK) {
        servo_angles_[servo_id] = angle;
        ESP_LOGD(DUAL_TAG, "Servo%d angle set to %lu", servo_id + 1, angle);
    }

    return ret;
}

uint32_t DualServoController::GetAngle(servo_id_t servo_id) const {
    if (servo_id >= SERVO_ID_MAX) {
        return 0;
    }
    return servo_angles_[servo_id];
}

esp_err_t DualServoController::SetBothAngles(uint32_t angle1, uint32_t angle2) {
    esp_err_t ret1 = SetAngle(SERVO_ID_1, angle1);
    esp_err_t ret2 = SetAngle(SERVO_ID_2, angle2);

    return (ret1 == ESP_OK && ret2 == ESP_OK) ? ESP_OK : ESP_FAIL;
}

esp_err_t DualServoController::Move(servo_id_t servo_id, uint32_t angle, servo_direction_t direction) {
    if (!initialized_) {
        ESP_LOGE(DUAL_TAG, "Dual servo controller not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (servo_id >= SERVO_ID_MAX) {
        ESP_LOGE(DUAL_TAG, "Invalid servo ID: %d", servo_id);
        return ESP_ERR_INVALID_ARG;
    }

    int32_t target = (int32_t)servo_angles_[servo_id];
    int32_t delta = (int32_t)(angle / 2);  // 实际移动量 = angle / 2

    if (direction == SERVO_DIR_FORWARD) {
        target += delta;
    } else {
        target -= delta;
    }

    // 限幅
    if (target < 0) target = 0;
    if (target > 180) target = 180;

    return SetAngle(servo_id, (uint32_t)target);
}

gpio_num_t DualServoController::GetServoPin(servo_id_t servo_id) const {
    if (servo_id == SERVO_ID_1) {
        return servo1_pin_;
    } else if (servo_id == SERVO_ID_2) {
        return servo2_pin_;
    }
    return GPIO_NUM_NC;
}

void DualServoController::DemoTaskFunc(void* arg) {
    DualServoController* controller = static_cast<DualServoController*>(arg);
    
    ESP_LOGI(DUAL_TAG, "Demo task started");

    while (controller->demo_running_) {
        // 舵机1 前进，舵机2 后退
        controller->Move(SERVO_ID_1, 90, SERVO_DIR_FORWARD);
        controller->Move(SERVO_ID_2, 90, SERVO_DIR_REVERSE);
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (!controller->demo_running_) break;

        // 反向
        controller->Move(SERVO_ID_1, 90, SERVO_DIR_REVERSE);
        controller->Move(SERVO_ID_2, 90, SERVO_DIR_FORWARD);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGI(DUAL_TAG, "Demo task exiting");
    vTaskDelete(NULL);
}

void DualServoController::StartDemoTask() {
    if (!initialized_) {
        ESP_LOGE(DUAL_TAG, "Dual servo controller not initialized");
        return;
    }

    if (demo_running_) {
        ESP_LOGW(DUAL_TAG, "Demo task already running");
        return;
    }

    demo_running_ = true;

    xTaskCreate(
        DemoTaskFunc,
        "dual_servo_demo",
        4096,
        this,
        tskIDLE_PRIORITY + 1,
        &demo_task_
    );

    ESP_LOGI(DUAL_TAG, "Demo task started");
}

void DualServoController::StopDemoTask() {
    if (!demo_running_) {
        return;
    }

    ESP_LOGI(DUAL_TAG, "Stopping demo task");
    demo_running_ = false;

    // 等待任务退出
    if (demo_task_ != nullptr) {
        vTaskDelay(pdMS_TO_TICKS(1100));
        demo_task_ = nullptr;
    }
}

#endif // SERVO_DUAL_MODE_ENABLED
