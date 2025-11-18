#include "motor.h"

#ifdef CONFIG_ENABLE_DOLL_INTERACTION

#include <esp_log.h>

#define TAG "Motor"

Motor::Motor(const std::string& name)
    : name_(name),
      current_position_(0),
      target_position_(0),
      is_moving_(false) {
}

bool Motor::Initialize() {
    ESP_LOGI(TAG, "Initializing motor: %s (simulation mode)", name_.c_str());

    // TODO: 硬件到位后实现
    // 舵机：
    //   1. 配置 PWM 通道
    //   2. 设置 PWM 频率（50Hz）
    //   3. 设置初始位置
    //
    // 步进电机：
    //   1. 配置 GPIO 引脚
    //   2. 初始化步进驱动器
    //   3. 设置步进参数

    ESP_LOGI(TAG, "Motor %s initialized (simulation)", name_.c_str());
    return true;
}

void Motor::MoveTo(int position, int speed) {
    ESP_LOGI(TAG, "[%s] MoveTo: position=%d, speed=%d", name_.c_str(), position, speed);

    target_position_ = position;
    is_moving_ = true;

    // TODO: 硬件到位后实现
    // 舵机：
    //   1. 计算 PWM 占空比
    //   2. 设置 PWM 输出
    //   3. 可选：实现平滑插值
    //
    // 步进电机：
    //   1. 计算步数
    //   2. 启动步进任务
    //   3. 实现加减速曲线

    // 模拟：立即到达目标位置
    current_position_ = target_position_;
    is_moving_ = false;
}

void Motor::Stop() {
    ESP_LOGI(TAG, "[%s] Stop", name_.c_str());

    is_moving_ = false;

    // TODO: 硬件到位后实现
    // 舵机：保持当前 PWM 输出
    // 步进电机：停止步进脉冲
}

int Motor::GetPosition() const {
    return current_position_;
}

bool Motor::IsMoving() const {
    return is_moving_;
}

bool Motor::CheckLimit() const {
    // TODO: 硬件到位后实现
    // 1. 检查硬件限位开关
    // 2. 检查软件限位
    // 3. 返回是否触碰限位

    return false;  // 模拟：无限位触发
}

#endif // CONFIG_ENABLE_DOLL_INTERACTION

