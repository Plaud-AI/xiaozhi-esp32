#include "motor_controller.h"

#ifdef CONFIG_ENABLE_DOLL_INTERACTION

#include <esp_log.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "motor.h"

#define TAG "MotorController"

MotorController& MotorController::GetInstance() {
    static MotorController instance;
    return instance;
}

MotorController::MotorController()
    : running_(false),
      emergency_stopped_(false),
      safety_enabled_(true) {
}

MotorController::~MotorController() {
    Stop();
}

void MotorController::Initialize() {
    ESP_LOGI(TAG, "Initializing MotorController...");

    InitializeMotors();

    ESP_LOGI(TAG, "MotorController initialized (simulation mode)");
}

void MotorController::Start() {
    if (running_) {
        ESP_LOGW(TAG, "MotorController already running");
        return;
    }

    ESP_LOGI(TAG, "Starting MotorController...");
    running_ = true;
    emergency_stopped_ = false;

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

void MotorController::Rotate(int angle, int speed) {
    if (!running_ || emergency_stopped_) {
        ESP_LOGW(TAG, "Cannot rotate: controller not running or emergency stopped");
        return;
    }

    ESP_LOGI(TAG, "Rotate: angle=%d, speed=%d", angle, speed);

    if (rotate_motor_) {
        // TODO: 硬件到位后实现
        // 1. 检查限位
        // 2. 计算目标位置
        // 3. 启动电机
        rotate_motor_->MoveTo(angle, speed);
    }
}

void MotorController::Nod(int angle, int speed) {
    if (!running_ || emergency_stopped_) {
        ESP_LOGW(TAG, "Cannot nod: controller not running or emergency stopped");
        return;
    }

    ESP_LOGI(TAG, "Nod: angle=%d, speed=%d", angle, speed);

    if (nod_motor_) {
        // TODO: 硬件到位后实现
        nod_motor_->MoveTo(angle, speed);
    }
}

void MotorController::Shake(int angle, int repeat) {
    if (!running_ || emergency_stopped_) {
        ESP_LOGW(TAG, "Cannot shake: controller not running or emergency stopped");
        return;
    }

    ESP_LOGI(TAG, "Shake: angle=%d, repeat=%d", angle, repeat);

    // TODO: 硬件到位后实现
    // 实现左右摇晃动作序列
    for (int i = 0; i < repeat; i++) {
        Rotate(angle, 80);
        vTaskDelay(pdMS_TO_TICKS(200));
        Rotate(-angle, 80);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    Rotate(0, 50);  // 回中
}

void MotorController::StopAllMotors() {
    ESP_LOGI(TAG, "Stopping all motors");

    if (rotate_motor_) rotate_motor_->Stop();
    if (nod_motor_) nod_motor_->Stop();
    if (shake_motor_) shake_motor_->Stop();
}

void MotorController::Reset() {
    ESP_LOGI(TAG, "Resetting to initial position");

    // TODO: 硬件到位后实现
    // 将所有电机移动到初始位置（0度）
    if (rotate_motor_) rotate_motor_->MoveTo(0, 30);
    if (nod_motor_) nod_motor_->MoveTo(0, 30);
}

void MotorController::EmergencyStop() {
    ESP_LOGW(TAG, "Emergency stop triggered!");
    
    emergency_stopped_ = true;
    StopAllMotors();

    if (on_error_) {
        on_error_("Emergency stop");
    }
}

bool MotorController::IsBusy() const {
    // TODO: 硬件到位后实现
    // 检查所有电机是否在运动
    bool busy = false;
    if (rotate_motor_) busy |= rotate_motor_->IsMoving();
    if (nod_motor_) busy |= nod_motor_->IsMoving();
    if (shake_motor_) busy |= shake_motor_->IsMoving();
    return busy;
}

bool MotorController::IsEmergencyStopped() const {
    return emergency_stopped_;
}

void MotorController::EnableSafety(bool enable) {
    safety_enabled_ = enable;
    ESP_LOGI(TAG, "Safety %s", enable ? "enabled" : "disabled");
}

void MotorController::SetRotateLimit(int min_angle, int max_angle) {
    ESP_LOGI(TAG, "Set rotate limit: [%d, %d]", min_angle, max_angle);
    // TODO: 硬件到位后实现
    // 保存限位值，在 MoveTo 时检查
}

void MotorController::SetNodLimit(int min_angle, int max_angle) {
    ESP_LOGI(TAG, "Set nod limit: [%d, %d]", min_angle, max_angle);
    // TODO: 硬件到位后实现
}

void MotorController::SetOnMotionCompleteCallback(std::function<void()> callback) {
    on_motion_complete_ = callback;
}

void MotorController::SetOnErrorCallback(std::function<void(const std::string&)> callback) {
    on_error_ = callback;
}

void MotorController::InitializeMotors() {
    // TODO: 硬件到位后实现
    // 根据 Board 配置创建对应类型的电机实例
    
    ESP_LOGI(TAG, "Creating motor instances (simulation mode)");
    
    // 创建模拟电机
    rotate_motor_ = std::make_unique<Motor>("rotate");
    nod_motor_ = std::make_unique<Motor>("nod");
    // shake_motor_ 可选，可能与 rotate_motor_ 共用

    rotate_motor_->Initialize();
    nod_motor_->Initialize();
}

void MotorController::CheckSafety() {
    if (!safety_enabled_) {
        return;
    }

    // TODO: 硬件到位后实现
    // 1. 检查限位开关
    // 2. 检查电流（堵转检测）
    // 3. 检查温度（如有传感器）
    // 4. 如有异常，触发 EmergencyStop()
}

#endif // CONFIG_ENABLE_DOLL_INTERACTION

