#include "pressure_sensor.h"

#ifdef CONFIG_ENABLE_DOLL_INTERACTION

#include <esp_log.h>

#define TAG "PressureSensor"

PressureSensor::~PressureSensor() {
    // 默认析构函数
}

bool PressureSensor::Initialize() {
    ESP_LOGI(TAG, "Initializing Pressure Sensor (simulation mode)");

    // TODO: 硬件到位后实现
    // 1. 配置 ADC 通道（如果使用模拟传感器）
    // 2. 配置 GPIO（如果使用数字传感器）
    // 3. 配置 I2C（如果使用 I2C 传感器）
    // 4. 校准传感器

    ESP_LOGI(TAG, "Pressure Sensor initialized (simulation)");
    return true;
}

bool PressureSensor::IsPressed() {
    // TODO: 硬件到位后实现
    // 1. 读取传感器数值
    // 2. 与阈值比较
    // 3. 可选：添加去抖动逻辑

    // 模拟模式：返回 false
    return false;
}

int PressureSensor::GetPressureValue() {
    // TODO: 硬件到位后实现
    // 返回原始 ADC 值或处理后的压力值

    // 模拟模式：返回 0
    return 0;
}

void PressureSensor::SetThreshold(int threshold) {
    threshold_ = threshold;
    ESP_LOGI(TAG, "Threshold set to: %d", threshold);
}

#endif // CONFIG_ENABLE_DOLL_INTERACTION

