/**
 * @file servo_driver.h
 * @brief 舵机驱动 - 支持单舵机和双舵机模式
 * 
 * 基于 LEDC PWM 的舵机控制驱动，支持：
 * - 单舵机模式
 * - 双舵机模式（ESP32S3_BLEHD_V1 板子）
 * - 角度设置 (0-180°)
 * - 相对移动
 * - 扫描测试
 * 
 * 引脚配置（对齐参考工程）:
 * - 舵机1: GPIO 1
 * - 舵机2: GPIO 2
 * 
 * @note 双舵机模式仅在 ESP32S3_BLEHD_V1 板子上生效
 */

#ifndef _SERVO_DRIVER_H_
#define _SERVO_DRIVER_H_

#include "sdkconfig.h"
#include <driver/ledc.h>
#include <driver/gpio.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <functional>

/* ============ 引脚定义（对齐参考工程）============ */
#ifdef CONFIG_BOARD_TYPE_ESP32S3_BLEHD_V1
#define SERVO1_GPIO_PIN             GPIO_NUM_1
#define SERVO2_GPIO_PIN             GPIO_NUM_2
#define SERVO_DUAL_MODE_ENABLED     1
#else
#define SERVO_DUAL_MODE_ENABLED     0
#endif

/* ============ PWM 配置 ============ */
#define SERVO_PWM_TIMER             LEDC_TIMER_2
#define SERVO_PWM_MODE              LEDC_LOW_SPEED_MODE
#define SERVO_PWM_FREQUENCY         50          // 50Hz - 标准舵机频率
#define SERVO_PWM_RESOLUTION        LEDC_TIMER_12_BIT

// 舵机通道分配
#define SERVO1_CHANNEL              LEDC_CHANNEL_1
#define SERVO2_CHANNEL              LEDC_CHANNEL_2

// 默认脉宽配置 (微秒)
#define SERVO_DEFAULT_MIN_PULSE_US  500         // 0.5ms -> 0°
#define SERVO_DEFAULT_MAX_PULSE_US  2500        // 2.5ms -> 180°
#define SERVO_DEFAULT_ANGLE         90          // 默认居中位置

/**
 * @brief 舵机方向枚举
 */
typedef enum {
    SERVO_DIR_FORWARD = 0,  // 正方向 (+angle)
    SERVO_DIR_REVERSE       // 反方向 (-angle)
} servo_direction_t;

/**
 * @brief 舵机 ID 枚举
 */
typedef enum {
    SERVO_ID_1 = 0,         // 舵机1 - GPIO 1
    SERVO_ID_2,             // 舵机2 - GPIO 2
    SERVO_ID_MAX
} servo_id_t;

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
 * @brief 单舵机驱动类
 * 
 * 使用方法：
 * @code
 *     ServoDriver servo(GPIO_NUM_2);
 *     servo.Init();
 *     servo.SetAngle(90);
 * @endcode
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


#if SERVO_DUAL_MODE_ENABLED
/**
 * @brief 双舵机控制器类
 * 
 * 仅在 ESP32S3_BLEHD_V1 板子上可用
 * 
 * 使用方法：
 * @code
 *     DualServoController controller;
 *     controller.Init();
 *     controller.SetAngle(SERVO_ID_1, 90);
 *     controller.SetAngle(SERVO_ID_2, 45);
 * @endcode
 */
class DualServoController {
public:
    /**
     * @brief 构造函数 - 使用默认引脚配置
     */
    DualServoController();

    /**
     * @brief 构造函数 - 自定义引脚配置
     * @param servo1_pin 舵机1 GPIO 引脚
     * @param servo2_pin 舵机2 GPIO 引脚
     */
    DualServoController(gpio_num_t servo1_pin, gpio_num_t servo2_pin);

    ~DualServoController();

    /**
     * @brief 初始化双舵机
     * @return ESP_OK 成功
     */
    esp_err_t Init();

    /**
     * @brief 释放资源
     */
    void Deinit();

    /**
     * @brief 设置指定舵机的角度
     * @param servo_id 舵机 ID
     * @param angle 目标角度 (0-180)
     * @return ESP_OK 成功
     */
    esp_err_t SetAngle(servo_id_t servo_id, uint32_t angle);

    /**
     * @brief 获取指定舵机的当前角度
     * @param servo_id 舵机 ID
     * @return 当前角度 (0-180)
     */
    uint32_t GetAngle(servo_id_t servo_id) const;

    /**
     * @brief 同时设置两个舵机的角度
     * @param angle1 舵机1 角度
     * @param angle2 舵机2 角度
     * @return ESP_OK 成功
     */
    esp_err_t SetBothAngles(uint32_t angle1, uint32_t angle2);

    /**
     * @brief 相对移动指定舵机
     * @param servo_id 舵机 ID
     * @param angle 移动角度
     * @param direction 移动方向
     * @return ESP_OK 成功
     */
    esp_err_t Move(servo_id_t servo_id, uint32_t angle, servo_direction_t direction);

    /**
     * @brief 启动演示动作任务
     * 两个舵机交替正反运动
     */
    void StartDemoTask();

    /**
     * @brief 停止演示动作任务
     */
    void StopDemoTask();

    /**
     * @brief 检查是否已初始化
     */
    bool IsInitialized() const { return initialized_; }

    /**
     * @brief 检查演示任务是否运行中
     */
    bool IsDemoRunning() const { return demo_running_; }

    /**
     * @brief 获取指定舵机的 GPIO 引脚
     */
    gpio_num_t GetServoPin(servo_id_t servo_id) const;

private:
    gpio_num_t servo1_pin_;         // 舵机1 引脚
    gpio_num_t servo2_pin_;         // 舵机2 引脚
    uint32_t servo_angles_[SERVO_ID_MAX];   // 当前角度
    bool initialized_;              // 初始化标志
    bool timer_initialized_;        // 定时器是否已初始化
    bool demo_running_;             // 演示任务运行标志
    TaskHandle_t demo_task_;        // 演示任务句柄

    /**
     * @brief 更新舵机占空比
     */
    esp_err_t UpdateDuty(servo_id_t servo_id, uint32_t angle);

    /**
     * @brief 角度转占空比
     */
    uint32_t AngleToDuty(uint32_t angle) const;

    /**
     * @brief 演示任务函数
     */
    static void DemoTaskFunc(void* arg);
};

#endif // SERVO_DUAL_MODE_ENABLED

#endif // _SERVO_DRIVER_H_
