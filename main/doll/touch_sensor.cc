#include "touch_sensor.h"

#ifdef CONFIG_ENABLE_DOLL_INTERACTION

#include <esp_log.h>

#define TAG "TouchSensor"

bool TouchSensor::Initialize() {
    ESP_LOGI(TAG, "Initializing Touch Sensor (simulation mode)");

    // TODO: 硬件到位后实现
    // 1. 配置电容触摸 GPIO（ESP32 内置触摸）
    // 2. 或配置 I2C 触摸芯片（CAP1188）
    // 3. 设置触摸阈值
    // 4. 启用中断

    ESP_LOGI(TAG, "Touch Sensor initialized (simulation)");
    return true;
}

TouchPosition TouchSensor::GetTouchPosition() {
    // TODO: 硬件到位后实现
    // 1. 读取触摸传感器状态
    // 2. 判断哪个位置被触摸
    // 3. 返回对应的位置枚举

    // 模拟模式：返回无触摸
    return TouchPosition::kTouchNone;
}

bool TouchSensor::IsTouched() {
    return GetTouchPosition() != TouchPosition::kTouchNone;
}

#endif // CONFIG_ENABLE_DOLL_INTERACTION

