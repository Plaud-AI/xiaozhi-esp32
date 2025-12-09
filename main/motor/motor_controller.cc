#include "motor_controller.h"

#ifdef CONFIG_ENABLE_DOLL_INTERACTION

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstdio>

#define TAG "MotorController"

// 默认 GPIO 配置（可通过 Kconfig 覆盖）
#ifndef CONFIG_SERVO_YAW_GPIO
#define CONFIG_SERVO_YAW_GPIO 2
#endif

#ifndef CONFIG_SERVO_PITCH_GPIO
#define CONFIG_SERVO_PITCH_GPIO 3
#endif

MotorController& MotorController::GetInstance() {
    static MotorController instance;
    return instance;
}

MotorController::MotorController()
    : initialized_(false),
      running_(false),
      emergency_stopped_(false),
      safety_enabled_(true) {
}

MotorController::~MotorController() {
    Stop();
}

void MotorController::Initialize() {
    if (initialized_) {
        ESP_LOGW(TAG, "Already initialized");
        return;
    }

    ESP_LOGI(TAG, "Initializing MotorController...");
    ESP_LOGI(TAG, "  Yaw GPIO: %d", CONFIG_SERVO_YAW_GPIO);
    ESP_LOGI(TAG, "  Pitch GPIO: %d", CONFIG_SERVO_PITCH_GPIO);

    InitializeMotors();
    initialized_ = true;

    ESP_LOGI(TAG, "MotorController initialized");
}

void MotorController::InitializeMotors() {
    // 创建 Yaw 舵机（水平）
    yaw_motor_ = std::make_unique<Motor>(
        "yaw",
        (gpio_num_t)CONFIG_SERVO_YAW_GPIO,
        LEDC_CHANNEL_0,
        ServoLimits::YawDefault()
    );

    // 创建 Pitch 舵机（垂直）
    pitch_motor_ = std::make_unique<Motor>(
        "pitch",
        (gpio_num_t)CONFIG_SERVO_PITCH_GPIO,
        LEDC_CHANNEL_1,
        ServoLimits::PitchDefault()
    );

    // 初始化舵机
    if (!yaw_motor_->Initialize()) {
        ESP_LOGE(TAG, "Failed to initialize Yaw motor");
    }
    
    if (!pitch_motor_->Initialize()) {
        ESP_LOGE(TAG, "Failed to initialize Pitch motor");
    }
}

void MotorController::Start() {
    if (running_) {
        ESP_LOGW(TAG, "Already running");
        return;
    }

    if (!initialized_) {
        Initialize();
    }

    ESP_LOGI(TAG, "Starting MotorController...");
    running_ = true;
    emergency_stopped_ = false;

    // 回到中位
    Home();

    ESP_LOGI(TAG, "MotorController started");
}

void MotorController::Stop() {
    if (!running_) {
        return;
    }

    ESP_LOGI(TAG, "Stopping MotorController...");
    
    StopAllMotors();
    running_ = false;

    ESP_LOGI(TAG, "MotorController stopped");
}

// ==================== 单轴控制 ====================

void MotorController::SetYawAngle(float angle) {
    if (!running_ || emergency_stopped_) {
        ESP_LOGW(TAG, "Cannot move: not running or emergency stopped");
        return;
    }

    if (yaw_motor_) {
        ESP_LOGI(TAG, "SetYawAngle: %.1f°", angle);
        yaw_motor_->MoveTo(angle);
    }
}

void MotorController::SetPitchAngle(float angle) {
    if (!running_ || emergency_stopped_) {
        ESP_LOGW(TAG, "Cannot move: not running or emergency stopped");
        return;
    }

    if (pitch_motor_) {
        ESP_LOGI(TAG, "SetPitchAngle: %.1f°", angle);
        pitch_motor_->MoveTo(angle);
    }
}

void MotorController::MoveYawRelative(float delta) {
    if (!running_ || emergency_stopped_) {
        return;
    }

    if (yaw_motor_) {
        ESP_LOGD(TAG, "MoveYawRelative: %+.1f°", delta);
        yaw_motor_->MoveRelative(delta);
    }
}

void MotorController::MovePitchRelative(float delta) {
    if (!running_ || emergency_stopped_) {
        return;
    }

    if (pitch_motor_) {
        ESP_LOGD(TAG, "MovePitchRelative: %+.1f°", delta);
        pitch_motor_->MoveRelative(delta);
    }
}

// ==================== 双轴控制 ====================

void MotorController::SetBothAngles(float yaw, float pitch) {
    if (!running_ || emergency_stopped_) {
        return;
    }

    ESP_LOGD(TAG, "SetBothAngles: Yaw=%.1f°, Pitch=%.1f°", yaw, pitch);
    
    if (yaw_motor_) yaw_motor_->MoveTo(yaw);
    if (pitch_motor_) pitch_motor_->MoveTo(pitch);
}

void MotorController::MoveBothRelative(float yaw_delta, float pitch_delta) {
    if (!running_ || emergency_stopped_) {
        return;
    }

    ESP_LOGD(TAG, "MoveBothRelative: Yaw%+.1f°, Pitch%+.1f°", yaw_delta, pitch_delta);
    
    if (yaw_motor_) yaw_motor_->MoveRelative(yaw_delta);
    if (pitch_motor_) pitch_motor_->MoveRelative(pitch_delta);
}

// ==================== 基础动作（兼容旧接口） ====================

void MotorController::Rotate(int angle, int speed) {
    if (!running_ || emergency_stopped_) {
        return;
    }

    ESP_LOGI(TAG, "Rotate: angle=%d, speed=%d", angle, speed);
    
    // 转换为相对角度
    if (yaw_motor_) {
        float current = yaw_motor_->GetPosition();
        float target = yaw_motor_->GetCenterAngle() + (float)angle;
        
        // 使用平滑移动，速度映射到时间
        uint32_t duration_ms = (uint32_t)(std::abs(angle) * (150 - speed) / 50);
        if (duration_ms < 100) duration_ms = 100;
        
        yaw_motor_->MoveToSmooth(target, duration_ms);
    }
}

void MotorController::Nod(int angle, int speed) {
    if (!running_ || emergency_stopped_) {
        return;
    }

    ESP_LOGI(TAG, "Nod: angle=%d, speed=%d", angle, speed);
    
    if (pitch_motor_) {
        float current = pitch_motor_->GetPosition();
        float target = pitch_motor_->GetCenterAngle() + (float)angle;
        
        uint32_t duration_ms = (uint32_t)(std::abs(angle) * (150 - speed) / 50);
        if (duration_ms < 100) duration_ms = 100;
        
        pitch_motor_->MoveToSmooth(target, duration_ms);
    }
}

void MotorController::Shake(int angle, int repeat) {
    if (!running_ || emergency_stopped_) {
        return;
    }

    ESP_LOGI(TAG, "Shake: angle=%d, repeat=%d", angle, repeat);

    if (!yaw_motor_) return;

    float center = yaw_motor_->GetCenterAngle();
    
    for (int i = 0; i < repeat && !emergency_stopped_; i++) {
        // 左转
        yaw_motor_->MoveToSmooth(center - (float)angle, 150);
        vTaskDelay(pdMS_TO_TICKS(50));
        
        // 右转
        yaw_motor_->MoveToSmooth(center + (float)angle, 150);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    // 回中
    yaw_motor_->MoveToSmooth(center, 200);
}

// ==================== 控制 ====================

void MotorController::StopAllMotors() {
    ESP_LOGI(TAG, "Stopping all motors");

    if (yaw_motor_) yaw_motor_->Stop();
    if (pitch_motor_) pitch_motor_->Stop();
}

void MotorController::Reset() {
    Home();
}

void MotorController::Home() {
    ESP_LOGI(TAG, "Homing all motors");

    if (yaw_motor_) yaw_motor_->Home();
    if (pitch_motor_) pitch_motor_->Home();
}

void MotorController::EmergencyStop() {
    ESP_LOGW(TAG, "EMERGENCY STOP!");
    
    emergency_stopped_ = true;
    StopAllMotors();

    if (on_error_) {
        on_error_("Emergency stop triggered");
    }
}

// ==================== 状态查询 ====================

bool MotorController::IsBusy() const {
    bool busy = false;
    if (yaw_motor_) busy |= yaw_motor_->IsMoving();
    if (pitch_motor_) busy |= pitch_motor_->IsMoving();
    return busy;
}

float MotorController::GetYawAngle() const {
    return yaw_motor_ ? yaw_motor_->GetPosition() : 90.0f;
}

float MotorController::GetPitchAngle() const {
    return pitch_motor_ ? pitch_motor_->GetPosition() : 90.0f;
}

float MotorController::GetYawCenter() const {
    return yaw_motor_ ? yaw_motor_->GetCenterAngle() : 90.0f;
}

float MotorController::GetPitchCenter() const {
    return pitch_motor_ ? pitch_motor_->GetCenterAngle() : 90.0f;
}

void MotorController::GetYawLimits(float& min, float& max) const {
    if (yaw_motor_) {
        const auto& limits = yaw_motor_->GetLimits();
        min = limits.min_angle;
        max = limits.max_angle;
    } else {
        min = 20.0f;
        max = 160.0f;
    }
}

void MotorController::GetPitchLimits(float& min, float& max) const {
    if (pitch_motor_) {
        const auto& limits = pitch_motor_->GetLimits();
        min = limits.min_angle;
        max = limits.max_angle;
    } else {
        min = 80.0f;
        max = 100.0f;
    }
}

// ==================== 运行时配置 ====================

void MotorController::SetYawLimits(float min_angle, float max_angle) {
    ESP_LOGI(TAG, "Set Yaw limits: [%.1f, %.1f]", min_angle, max_angle);
    if (yaw_motor_) {
        ServoLimits limits = yaw_motor_->GetLimits();
        limits.min_angle = min_angle;
        limits.max_angle = max_angle;
        yaw_motor_->SetLimits(limits);
    }
}

void MotorController::SetPitchLimits(float min_angle, float max_angle) {
    ESP_LOGI(TAG, "Set Pitch limits: [%.1f, %.1f]", min_angle, max_angle);
    if (pitch_motor_) {
        ServoLimits limits = pitch_motor_->GetLimits();
        limits.min_angle = min_angle;
        limits.max_angle = max_angle;
        pitch_motor_->SetLimits(limits);
    }
}

// ==================== 回调 ====================

void MotorController::SetOnMotionCompleteCallback(std::function<void()> callback) {
    on_motion_complete_ = callback;
}

void MotorController::SetOnErrorCallback(std::function<void(const std::string&)> callback) {
    on_error_ = callback;
}

// ==================== 测试辅助 ====================

std::string MotorController::GetStatusString() const {
    char buffer[256];
    
    float yaw_min, yaw_max, pitch_min, pitch_max;
    GetYawLimits(yaw_min, yaw_max);
    GetPitchLimits(pitch_min, pitch_max);
    
    snprintf(buffer, sizeof(buffer),
        "MotorController Status:\n"
        "  Running: %s\n"
        "  Emergency: %s\n"
        "  Yaw: %.1f° (limits: %.1f~%.1f)\n"
        "  Pitch: %.1f° (limits: %.1f~%.1f)\n"
        "  Busy: %s",
        running_ ? "yes" : "no",
        emergency_stopped_ ? "YES" : "no",
        GetYawAngle(), yaw_min, yaw_max,
        GetPitchAngle(), pitch_min, pitch_max,
        IsBusy() ? "yes" : "no"
    );
    
    return std::string(buffer);
}

void MotorController::TestSweep(int axis, int cycles) {
    ESP_LOGI(TAG, "TestSweep: axis=%d, cycles=%d", axis, cycles);

    if (!running_) {
        ESP_LOGW(TAG, "Not running, cannot sweep");
        return;
    }

    for (int c = 0; c < cycles && !emergency_stopped_; c++) {
        if (axis == 0 || axis == 2) {
            // Yaw 扫描
            if (yaw_motor_) {
                float yaw_min, yaw_max;
                GetYawLimits(yaw_min, yaw_max);
                float center = yaw_motor_->GetCenterAngle();
                
                yaw_motor_->MoveToSmooth(yaw_min + 10, 500);
                vTaskDelay(pdMS_TO_TICKS(300));
                yaw_motor_->MoveToSmooth(yaw_max - 10, 500);
                vTaskDelay(pdMS_TO_TICKS(300));
                yaw_motor_->MoveToSmooth(center, 300);
            }
        }
        
        if (axis == 1 || axis == 2) {
            // Pitch 扫描
            if (pitch_motor_) {
                float pitch_min, pitch_max;
                GetPitchLimits(pitch_min, pitch_max);
                float center = pitch_motor_->GetCenterAngle();
                
                pitch_motor_->MoveToSmooth(pitch_min + 2, 300);
                vTaskDelay(pdMS_TO_TICKS(200));
                pitch_motor_->MoveToSmooth(pitch_max - 2, 300);
                vTaskDelay(pdMS_TO_TICKS(200));
                pitch_motor_->MoveToSmooth(center, 200);
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    ESP_LOGI(TAG, "TestSweep completed");
}

#endif // CONFIG_ENABLE_DOLL_INTERACTION
