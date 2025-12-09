#ifndef MOTOR_CONTROLLER_H
#define MOTOR_CONTROLLER_H

#include <memory>
#include <functional>
#include <string>

#ifdef CONFIG_ENABLE_DOLL_INTERACTION

#include "motor.h"

/**
 * @brief 双舵机电机控制器
 * 
 * 管理 Yaw（水平）和 Pitch（垂直）两个舵机：
 * - Yaw:   控制左右转动，范围 20°~160°（中位 90°）
 * - Pitch: 控制上下俯仰，范围 80°~100°（中位 90°，机械限制 ±10°）
 * 
 * 提供：
 * - 单轴控制接口
 * - 双轴协调控制
 * - 预设动作（点头、摇头等）
 * - 测试接口
 */
class MotorController {
public:
    static MotorController& GetInstance();

    // ==================== 初始化 ====================
    void Initialize();
    void Start();
    void Stop();
    bool IsRunning() const { return running_; }
    bool IsInitialized() const { return initialized_; }

    // ==================== 单轴控制（测试接口） ====================
    
    /**
     * @brief 设置 Yaw 角度（绝对值）
     * @param angle 目标角度 (20~160)
     */
    void SetYawAngle(float angle);
    
    /**
     * @brief 设置 Pitch 角度（绝对值）
     * @param angle 目标角度 (80~100)
     */
    void SetPitchAngle(float angle);
    
    /**
     * @brief 相对移动 Yaw
     * @param delta 相对角度（正=右转，负=左转）
     */
    void MoveYawRelative(float delta);
    
    /**
     * @brief 相对移动 Pitch
     * @param delta 相对角度（正=抬头，负=低头）
     */
    void MovePitchRelative(float delta);

    // ==================== 双轴协调控制 ====================
    
    /**
     * @brief 同时设置两轴角度
     * @param yaw Yaw 目标角度
     * @param pitch Pitch 目标角度
     */
    void SetBothAngles(float yaw, float pitch);
    
    /**
     * @brief 同时相对移动两轴
     * @param yaw_delta Yaw 相对角度
     * @param pitch_delta Pitch 相对角度
     */
    void MoveBothRelative(float yaw_delta, float pitch_delta);

    // ==================== 基础动作（兼容旧接口） ====================
    void Rotate(int angle, int speed = 50);      // 旋转（Yaw）
    void Nod(int angle, int speed = 50);         // 点头（Pitch）
    void Shake(int angle, int repeat = 1);       // 摇头
    
    // ==================== 控制 ====================
    void StopAllMotors();                        // 停止所有电机
    void Reset();                                // 复位到中位
    void Home();                                 // 回到中位（同 Reset）
    void EmergencyStop();                        // 紧急停止

    // ==================== 状态查询（测试接口） ====================
    bool IsBusy() const;
    bool IsEmergencyStopped() const { return emergency_stopped_; }
    
    float GetYawAngle() const;
    float GetPitchAngle() const;
    float GetYawCenter() const;
    float GetPitchCenter() const;
    
    // 获取限位信息
    void GetYawLimits(float& min, float& max) const;
    void GetPitchLimits(float& min, float& max) const;

    // ==================== 运行时配置（测试接口） ====================
    void SetYawLimits(float min_angle, float max_angle);
    void SetPitchLimits(float min_angle, float max_angle);
    void EnableSafety(bool enable) { safety_enabled_ = enable; }

    // ==================== 回调 ====================
    void SetOnMotionCompleteCallback(std::function<void()> callback);
    void SetOnErrorCallback(std::function<void(const std::string&)> callback);

    // ==================== 测试辅助 ====================
    
    /**
     * @brief 获取状态字符串（用于调试/测试面板）
     */
    std::string GetStatusString() const;
    
    /**
     * @brief 舵机扫描测试
     * @param axis 0=Yaw, 1=Pitch, 2=Both
     * @param cycles 往返次数
     */
    void TestSweep(int axis, int cycles = 1);

private:
    MotorController();
    ~MotorController();
    MotorController(const MotorController&) = delete;
    MotorController& operator=(const MotorController&) = delete;

    void InitializeMotors();

    bool initialized_;
    bool running_;
    bool emergency_stopped_;
    bool safety_enabled_;
    
    std::unique_ptr<Motor> yaw_motor_;     // 水平旋转
    std::unique_ptr<Motor> pitch_motor_;   // 垂直俯仰

    std::function<void()> on_motion_complete_;
    std::function<void(const std::string&)> on_error_;
};

#endif // CONFIG_ENABLE_DOLL_INTERACTION

#endif // MOTOR_CONTROLLER_H
