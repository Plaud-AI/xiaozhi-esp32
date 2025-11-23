#ifndef MOTOR_H
#define MOTOR_H

#include <string>

#ifdef CONFIG_ENABLE_DOLL_INTERACTION

/**
 * @brief 单个电机抽象接口
 * 
 * 支持舵机（Servo）和步进电机（Stepper）
 * 当前为模拟实现，硬件到位后可继承此类实现具体驱动
 */
class Motor {
public:
    explicit Motor(const std::string& name);
    virtual ~Motor() = default;

    // 初始化电机
    virtual bool Initialize();

    // 移动到目标位置
    // @param position 目标位置（角度或步数）
    // @param speed 速度（0-100）
    virtual void MoveTo(int position, int speed);

    // 停止电机
    virtual void Stop();

    // 获取当前位置
    virtual int GetPosition() const;

    // 检测是否正在运动
    virtual bool IsMoving() const;

    // 检查限位
    virtual bool CheckLimit() const;

    // 获取电机名称
    const std::string& GetName() const { return name_; }

protected:
    std::string name_;
    int current_position_;
    int target_position_;
    bool is_moving_;

    // TODO: 添加具体电机类型的成员
    // 例如：PWM 通道、GPIO 引脚等
};

// TODO: 硬件到位后可以创建子类
// class ServoMotor : public Motor {};
// class StepperMotor : public Motor {};

#endif // CONFIG_ENABLE_DOLL_INTERACTION

#endif // MOTOR_H

