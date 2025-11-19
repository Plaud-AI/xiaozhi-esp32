#ifndef TOUCH_SENSOR_H
#define TOUCH_SENSOR_H

#include "doll_service.h"

#ifdef CONFIG_ENABLE_DOLL_INTERACTION

/**
 * @brief 触摸传感器抽象接口
 * 
 * 用于检测用户触摸手办的不同位置
 * 支持：电容触摸、触摸按键等
 */
class TouchSensor {
public:
    TouchSensor() = default;
    virtual ~TouchSensor();

    // 初始化传感器
    virtual bool Initialize();

    // 获取当前触摸位置
    virtual TouchPosition GetTouchPosition();

    // 检测是否有触摸
    virtual bool IsTouched();

private:
    // TODO: 添加触摸传感器配置
    // 例如：多个 GPIO 或 I2C 触摸芯片（CAP1188）
};

#endif // CONFIG_ENABLE_DOLL_INTERACTION

#endif // TOUCH_SENSOR_H

