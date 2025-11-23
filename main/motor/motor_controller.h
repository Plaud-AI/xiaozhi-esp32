#ifndef MOTOR_CONTROLLER_H
#define MOTOR_CONTROLLER_H

#include <vector>
#include <memory>
#include <functional>

#ifdef CONFIG_ENABLE_DOLL_INTERACTION

class Motor;

/**
 * @brief 电机控制器 - 统一管理所有电机
 * 
 * 职责：
 * - 管理多个电机实例
 * - 提供高层动作接口（旋转、点头、摇晃）
 * - 实现安全保护（限位、堵转）
 * - 动作队列管理
 */
class MotorController {
public:
    static MotorController& GetInstance();

    // 初始化
    void Initialize();
    void Start();
    void Stop();
    bool IsRunning() const { return running_; }

    // 基础动作（角度单位：度，速度单位：0-100）
    void Rotate(int angle, int speed = 50);      // 旋转（左右）
    void Nod(int angle, int speed = 50);         // 点头（上下）
    void Shake(int angle, int repeat = 1);       // 摇晃
    
    // 控制
    void StopAllMotors();                        // 停止所有电机
    void Reset();                                // 复位到初始位置
    void EmergencyStop();                        // 紧急停止

    // 状态查询
    bool IsBusy() const;                         // 是否有电机正在运动
    bool IsEmergencyStopped() const;             // 是否处于紧急停止状态

    // 安全控制
    void EnableSafety(bool enable);              // 启用/禁用安全保护
    void SetRotateLimit(int min_angle, int max_angle);  // 设置旋转限位
    void SetNodLimit(int min_angle, int max_angle);     // 设置点头限位

    // 回调
    void SetOnMotionCompleteCallback(std::function<void()> callback);
    void SetOnErrorCallback(std::function<void(const std::string&)> callback);

private:
    MotorController();
    ~MotorController();
    MotorController(const MotorController&) = delete;
    MotorController& operator=(const MotorController&) = delete;

    void InitializeMotors();
    void CheckSafety();

    bool running_;
    bool emergency_stopped_;
    bool safety_enabled_;
    
    std::unique_ptr<Motor> rotate_motor_;        // 旋转电机
    std::unique_ptr<Motor> nod_motor_;           // 点头电机
    std::unique_ptr<Motor> shake_motor_;         // 摇晃电机（可选）

    std::function<void()> on_motion_complete_;
    std::function<void(const std::string&)> on_error_;
};

#endif // CONFIG_ENABLE_DOLL_INTERACTION

#endif // MOTOR_CONTROLLER_H

