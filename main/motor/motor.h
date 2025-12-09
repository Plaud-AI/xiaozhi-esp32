#ifndef MOTOR_H
#define MOTOR_H

#include <string>
#include <memory>

#ifdef CONFIG_ENABLE_DOLL_INTERACTION

#include <driver/gpio.h>
#include <driver/ledc.h>
#include "hardware/servo_driver.h"

/**
 * @brief 舵机限位配置
 */
struct ServoLimits {
    float min_angle;     // 最小角度
    float max_angle;     // 最大角度
    float center_angle;  // 中位角度

    ServoLimits(float min = 0, float max = 180, float center = 90)
        : min_angle(min), max_angle(max), center_angle(center) {}

    // 预设配置（使用 Kconfig 值或默认值）
    static ServoLimits YawDefault() {
        // Yaw 舵机限位：默认 20° ~ 160°，中位 90°
#ifdef CONFIG_SERVO_YAW_MIN_ANGLE
        float min = (float)CONFIG_SERVO_YAW_MIN_ANGLE;
#else
        float min = 20.0f;
#endif
#ifdef CONFIG_SERVO_YAW_MAX_ANGLE
        float max = (float)CONFIG_SERVO_YAW_MAX_ANGLE;
#else
        float max = 160.0f;
#endif
        return ServoLimits(min, max, 90.0f);
    }

    static ServoLimits PitchDefault() {
        // Pitch 舵机限位：默认 80° ~ 100°（±10° 范围），中位 90°
#ifdef CONFIG_SERVO_PITCH_MIN_ANGLE
        float min = (float)CONFIG_SERVO_PITCH_MIN_ANGLE;
#else
        float min = 80.0f;
#endif
#ifdef CONFIG_SERVO_PITCH_MAX_ANGLE
        float max = (float)CONFIG_SERVO_PITCH_MAX_ANGLE;
#else
        float max = 100.0f;
#endif
        return ServoLimits(min, max, 90.0f);
    }
};

/**
 * @brief 电机抽象类（实际用舵机实现）
 * 
 * 封装单个舵机的控制逻辑，包括：
 * - 初始化和反初始化
 * - 角度设置（绝对和相对）
 * - 软件限位保护
 * - 平滑运动
 * - 模拟模式（屏蔽实际硬件动作）
 */
class Motor {
public:
    /**
     * @brief 构造函数
     * @param name 电机名称（用于日志）
     * @param gpio_pin GPIO 引脚
     * @param channel LEDC 通道
     * @param limits 限位配置
     * @param simulation_mode 是否启用模拟模式（不实际驱动舵机）
     */
    Motor(const std::string& name, gpio_num_t gpio_pin, 
          ledc_channel_t channel, const ServoLimits& limits,
          bool simulation_mode = false);
    
    ~Motor();

    // ==================== 初始化 ====================
    
    /**
     * @brief 初始化电机
     * @return true 成功，false 失败
     */
    bool Initialize();
    
    /**
     * @brief 反初始化
     */
    void Deinitialize();
    
    bool IsInitialized() const { return initialized_; }

    // ==================== 运动控制 ====================
    
    /**
     * @brief 移动到指定角度（绝对）
     * @param angle 目标角度（会自动限幅）
     */
    void MoveTo(float angle);
    
    /**
     * @brief 平滑移动到指定角度
     * @param angle 目标角度
     * @param duration_ms 移动时间（毫秒）
     */
    void MoveToSmooth(float angle, uint32_t duration_ms);
    
    /**
     * @brief 相对移动
     * @param delta_angle 相对角度（正=正向，负=反向）
     */
    void MoveRelative(float delta_angle);
    
    /**
     * @brief 回到中位
     */
    void Home();
    
    /**
     * @brief 停止运动
     */
    void Stop();

    // ==================== 状态查询 ====================
    
    float GetPosition() const { return current_angle_; }
    float GetTargetPosition() const { return target_angle_; }
    float GetCenterAngle() const { return limits_.center_angle; }
    bool IsMoving() const { return is_moving_; }
    const ServoLimits& GetLimits() const { return limits_; }

    // ==================== 配置 ====================
    
    void SetLimits(const ServoLimits& limits) { limits_ = limits; }
    const std::string& GetName() const { return name_; }
    
    /**
     * @brief 设置模拟模式
     * @param enable true=模拟模式（不驱动硬件），false=正常模式
     */
    void SetSimulationMode(bool enable) { simulation_mode_ = enable; }
    bool IsSimulationMode() const { return simulation_mode_; }

private:
    /**
     * @brief 角度限幅
     */
    float ClampAngle(float angle) const;

    std::string name_;
    gpio_num_t gpio_pin_;
    ledc_channel_t channel_;
    ServoLimits limits_;
    
    float current_angle_;
    float target_angle_;
    bool is_moving_;
    bool initialized_;
    bool simulation_mode_;   // 模拟模式：true=不实际驱动硬件
    
    std::unique_ptr<ServoDriver> servo_driver_;
};

#endif // CONFIG_ENABLE_DOLL_INTERACTION

#endif // MOTOR_H
