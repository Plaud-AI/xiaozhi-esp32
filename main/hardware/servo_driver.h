#ifndef _SERVO_DRIVER_H_
#define _SERVO_DRIVER_H_

#include <driver/ledc.h>
#include <driver/gpio.h>
#include <esp_err.h>
#include <functional>

/**
 * @brief 舵机方向枚举
 */
typedef enum {
    SERVO_DIR_FORWARD = 0,  // 正方向 (+angle)
    SERVO_DIR_REVERSE       // 反方向 (-angle)
} servo_direction_t;

/**
 * @brief 舵机配置结构体
 */
typedef struct {
    gpio_num_t gpio_pin;            // PWM 输出引脚
    ledc_timer_t timer;             // LEDC 定时器
    ledc_channel_t channel;         // LEDC 通道
    uint32_t frequency;             // PWM 频率 (Hz)
    ledc_timer_bit_t resolution;    // PWM 分辨率
    uint32_t min_pulse_us;          // 最小脉宽 (微秒，对应 0°)
    uint32_t max_pulse_us;          // 最大脉宽 (微秒，对应 180°)
    uint32_t default_angle;         // 默认角度
} servo_config_t;

/**
 * @brief 舵机驱动类
 * 
 * 基于 LEDC PWM 的舵机控制驱动，支持：
 * - 角度设置 (0-180°)
 * - 相对移动
 * - 扫描测试
 * 
 * 使用方法：
 *   ServoDriver servo(GPIO_NUM_2);
 *   servo.Init();
 *   servo.SetAngle(90);
 */
class ServoDriver {
public:
    /**
     * @brief 使用默认配置构造
     * @param gpio_pin PWM 输出引脚
     */
    explicit ServoDriver(gpio_num_t gpio_pin);
    
    /**
     * @brief 使用完整配置构造
     * @param config 舵机配置
     */
    explicit ServoDriver(const servo_config_t& config);
    
    ~ServoDriver();

    /**
     * @brief 初始化舵机
     * @return ESP_OK 成功, 其他值失败
     */
    esp_err_t Init();

    /**
     * @brief 释放资源
     */
    void Deinit();

    /**
     * @brief 设置舵机角度（绝对）
     * @param angle 目标角度 (0-180)
     * @return ESP_OK 成功
     */
    esp_err_t SetAngle(uint32_t angle);

    /**
     * @brief 获取当前角度
     * @return 当前角度 (0-180)
     */
    uint32_t GetAngle() const { return current_angle_; }

    /**
     * @brief 相对移动
     * @param angle 移动角度（实际移动量 = angle / 2）
     * @param direction 移动方向
     * @return ESP_OK 成功
     */
    esp_err_t Move(uint32_t angle, servo_direction_t direction);

    /**
     * @brief 扫描测试
     * @param min_angle 最小角度
     * @param max_angle 最大角度
     * @param step 每步角度
     * @param step_delay_ms 步间延时
     * @param cycles 往返次数
     * @param progress_callback 进度回调 (可选)
     * @return ESP_OK 成功
     */
    esp_err_t Sweep(uint32_t min_angle, uint32_t max_angle, 
                    uint32_t step, uint32_t step_delay_ms, 
                    uint32_t cycles,
                    std::function<void(uint32_t current_angle)> progress_callback = nullptr);

    /**
     * @brief 检查是否已初始化
     */
    bool IsInitialized() const { return initialized_; }

    /**
     * @brief 获取 GPIO 引脚
     */
    gpio_num_t GetGpioPin() const { return config_.gpio_pin; }

    /**
     * @brief 获取 PWM 频率
     */
    uint32_t GetFrequency() const { return config_.frequency; }

    /**
     * @brief 获取角度范围
     */
    void GetAngleRange(uint32_t& min, uint32_t& max) const {
        min = 0;
        max = 180;
    }

private:
    servo_config_t config_;         // 舵机配置
    bool initialized_;              // 初始化标志
    uint32_t current_angle_;        // 当前角度

    /**
     * @brief 角度转换为占空比
     * @param angle 角度 (0-180)
     * @return 占空比值
     */
    uint32_t AngleToDuty(uint32_t angle) const;

    /**
     * @brief 获取默认配置
     * @param gpio_pin GPIO 引脚
     * @return 默认配置
     */
    static servo_config_t GetDefaultConfig(gpio_num_t gpio_pin);
};

#endif // _SERVO_DRIVER_H_

