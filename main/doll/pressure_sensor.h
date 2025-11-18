#ifndef PRESSURE_SENSOR_H
#define PRESSURE_SENSOR_H

#ifdef CONFIG_ENABLE_DOLL_INTERACTION

/**
 * @brief 压力传感器抽象接口
 * 
 * 用于检测手办是否放置在底座上
 * 支持：电容式、电阻式（FSR）、ADC 等
 */
class PressureSensor {
public:
    PressureSensor() = default;
    virtual ~PressureSensor() = default;

    // 初始化传感器
    virtual bool Initialize();

    // 检测是否有压力（手办放置）
    virtual bool IsPressed();

    // 获取压力值（可选，用于调试和标定）
    virtual int GetPressureValue();

    // 设置触发阈值
    virtual void SetThreshold(int threshold);

private:
    int threshold_ = 500; // 默认阈值
    // TODO: 添加 ADC/GPIO 配置
};

#endif // CONFIG_ENABLE_DOLL_INTERACTION

#endif // PRESSURE_SENSOR_H

