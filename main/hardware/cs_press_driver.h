/**
 * @file cs_press_driver.h
 * @brief CHIPSEA CSU18M6X 压感传感器驱动 (C++ 封装)
 * 
 * 基于参考工程 cs_press_m6x_driver 对齐，支持：
 * - 双通道传感器数据读取
 * - 固件更新
 * - 校准功能
 * - Debug 模式
 * 
 * 引脚配置（对齐参考工程）:
 * - SDA: GPIO 5
 * - SCL: GPIO 4
 * - RST: GPIO 10 (可选)
 * 
 * @note 支持 I2C 总线共享
 */

#ifndef _CS_PRESS_DRIVER_H_
#define _CS_PRESS_DRIVER_H_

#include "sdkconfig.h"

// 仅在 ESP32S3_BLEHD_V1 板子上启用
#ifdef CONFIG_BOARD_TYPE_ESP32S3_BLEHD_V1

#include <driver/i2c_master.h>
#include <driver/gpio.h>
#include <esp_err.h>
#include <functional>
#include <cstdint>

/* ============ 配置宏定义 ============ */
#define CS_PRESS_I2C_ADDR           0x72        // 7-bit I2C 地址
#define CS_PRESS_I2C_FREQ_HZ        100000      // 100kHz

/* ============ 引脚定义（对齐参考工程）============ */
#define CS_PRESS_I2C_SDA_PIN        GPIO_NUM_5
#define CS_PRESS_I2C_SCL_PIN        GPIO_NUM_4
#define CS_PRESS_RST_PIN            GPIO_NUM_NC  // 不使用硬件复位，用软复位

/* ============ 寄存器定义 ============ */
#define DEBUG_MODE_REG              0x60
#define DEBUG_READY_REG             0x61
#define DEBUG_DATA_REG              0x62

#define FW_STATUS_REG               0x00
#define AP_RESRT_REG                0x01
#define AP_DEVICE_ID_REG            0x02
#define AP_MANUFACTURER_ID_REG      0x03
#define AP_MODULE_ID_REG            0x04
#define AP_VERSION_REG              0x05
#define AP_WAKEUP_REG               0x06
#define AP_SLEEP_REG                0x07
#define AP_CALIBRATION_REG          0x1c
#define AP_WATCH_MODE_REG           0x1d
#define AP_KEYIO_OUT_REG            0x1e
#define AP_RW_TEST_REG              0x1f
#define AP_FORCEDATA_REG            0x20

#define BOOT_CMD_REG                0x21

/* ============ Debug 模式命令 ============ */
#define AP_R_NOISE_DEBUG_MODE           0x11
#define AP_W_CAL_FACTOR_DEBUG_MODE      0x30
#define AP_R_CAL_FACTOR_DEBUG_MODE      0x31
#define AP_W_PRESS_LEVEL_DEBUG_MODE     0x32
#define AP_R_PRESS_LEVEL_DEBUG_MODE     0x33
#define AP_CALIBRATION_DEBUG_MODE       0x34
#define AP_CALIBRATION_LOG_DEBUG_MODE   0x35
#define AP_R_SENSOR_DATA_DEBUG_MODE     0xf4

#define AP_SOFT_RESRT_CMD               0xcc

/* ============ 固件相关常量 ============ */
#define FW_ADDR_CODE_LENGTH         0x06
#define FW_ADDR_VERSION             0x02
#define FW_ONE_BLOCK_LENGTH         32
#define BOOT_CMD_LENGTH             3
#define CALIBRATION_SUCCESS_FLAG    0xf0

#define CS_MANUFACTURER_ID_LENGTH   2
#define CS_MODULE_ID_LENGTH         2
#define CS_FW_VERSION_LENGTH        2

/**
 * @brief 固件信息结构体
 */
struct CsFwInfo {
    uint16_t manufacturer_id;
    uint16_t module_id;
    uint16_t fw_version;
};

/**
 * @brief 双通道传感器数据结构体
 */
struct CsSensorData {
    int16_t rawdata[2];           // 两个通道的原始 ADC 值
    int16_t arith_rawdata[2];     // 两个通道的算术处理值
    int16_t energy_data[2];       // 能量数据
    int16_t diff_data[2];         // 差分数据
};

/**
 * @brief 校准结果结构体
 */
struct CsCalibrationResult {
    uint8_t calibration_progress;
    uint16_t calibration_factor;
    int16_t press_adc_1st;
    int16_t press_adc_2nd;
    int16_t press_adc_3rd;
};

/**
 * @brief 校准日志结构体
 */
struct CsCalibrationLog {
    uint16_t calibration_factor;
    int16_t press_adc_1st;
    int16_t press_adc_2nd;
    int16_t press_adc_3rd;
};

/**
 * @brief CHIPSEA 压感传感器驱动类
 */
class CsPressDriver {
public:
    /**
     * @brief 传感器数据回调函数类型
     */
    using SensorDataCallback = std::function<void(const CsSensorData& data)>;

    /**
     * @brief 构造函数（使用外部 I2C 总线）
     * @param i2c_bus 外部 I2C 总线句柄
     * @param i2c_addr I2C 从机地址（默认 0x72）
     * @param rst_pin 复位引脚（GPIO_NUM_NC 表示使用软复位）
     */
    CsPressDriver(i2c_master_bus_handle_t i2c_bus, 
                  uint8_t i2c_addr = CS_PRESS_I2C_ADDR,
                  gpio_num_t rst_pin = CS_PRESS_RST_PIN);

    /**
     * @brief 构造函数（创建独立 I2C 总线）
     * @param i2c_port I2C 端口号
     * @param sda_pin SDA 引脚
     * @param scl_pin SCL 引脚
     * @param rst_pin 复位引脚（GPIO_NUM_NC 表示使用软复位）
     */
    CsPressDriver(i2c_port_t i2c_port,
                  gpio_num_t sda_pin = CS_PRESS_I2C_SDA_PIN,
                  gpio_num_t scl_pin = CS_PRESS_I2C_SCL_PIN,
                  gpio_num_t rst_pin = CS_PRESS_RST_PIN);

    ~CsPressDriver();

    /* ========== 初始化和复位 ========== */

    /**
     * @brief 初始化传感器（包含固件更新）
     * @return ESP_OK 成功
     */
    esp_err_t Init();

    /**
     * @brief 仅初始化（不更新固件）
     * @return ESP_OK 成功
     */
    esp_err_t InitWithoutFwUpdate();

    /**
     * @brief 反初始化
     */
    void Deinit();

    /**
     * @brief 复位 IC
     * @return ESP_OK 成功
     */
    esp_err_t ResetIc();

    /* ========== 设备状态控制 ========== */

    /**
     * @brief 唤醒设备
     * @return ESP_OK 成功
     */
    esp_err_t WakeUp();

    /**
     * @brief 设备休眠
     * @return ESP_OK 成功
     */
    esp_err_t Sleep();

    /**
     * @brief I2C 读写测试
     * @param test_data 测试数据
     * @return ESP_OK 成功
     */
    esp_err_t I2cRwTest(uint8_t test_data);

    /* ========== 固件信息 ========== */

    /**
     * @brief 读取固件信息
     * @param fw_info [out] 固件信息
     * @return ESP_OK 成功
     */
    esp_err_t ReadFwInfo(CsFwInfo& fw_info);

    /**
     * @brief 打印固件信息
     */
    void PrintFwInfo();

    /**
     * @brief 检查固件状态
     * @return 0: boot 模式, 1: AP 模式, -1: 错误
     */
    int CheckFwStatus();

    /* ========== 传感器数据读取 ========== */

    /**
     * @brief 初始化传感器数据读取
     * @return ESP_OK 成功
     */
    esp_err_t ReadSensorDataInit();

    /**
     * @brief 读取双通道传感器数据
     * @param sensor_data [out] 传感器数据
     * @return ESP_OK 成功
     */
    esp_err_t ReadSensorData(CsSensorData& sensor_data);

    /**
     * @brief 读取简单压力值
     * @param pressure [out] 压力值
     * @return ESP_OK 成功
     */
    esp_err_t ReadPressure(uint16_t& pressure);

    /**
     * @brief 读取平均压力值
     * @param pressure [out] 压力值
     * @return ESP_OK 成功
     */
    esp_err_t ReadPressureAvg(uint16_t& pressure);

    /* ========== 校准功能 ========== */

    /**
     * @brief 启用校准
     * @return ESP_OK 成功
     */
    esp_err_t CalibrationEnable();

    /**
     * @brief 禁用校准
     * @return ESP_OK 成功
     */
    esp_err_t CalibrationDisable();

    /**
     * @brief 检查校准结果
     * @param result [out] 校准结果
     * @return 0: 进行中, 1: 完成, -1: 错误
     */
    int CalibrationCheck(CsCalibrationResult& result);

    /**
     * @brief 读取校准因子
     * @param cal_factor [out] 校准因子
     * @return ESP_OK 成功
     */
    esp_err_t ReadCalibrationFactor(uint16_t& cal_factor);

    /**
     * @brief 写入校准因子
     * @param cal_factor 校准因子
     * @return ESP_OK 成功
     */
    esp_err_t WriteCalibrationFactor(uint16_t cal_factor);

    /**
     * @brief 读取压力等级
     * @param press_level [out] 压力等级
     * @return ESP_OK 成功
     */
    esp_err_t ReadPressLevel(uint16_t& press_level);

    /**
     * @brief 写入压力等级
     * @param press_level 压力等级
     * @return ESP_OK 成功
     */
    esp_err_t WritePressLevel(uint16_t press_level);

    /* ========== PGA 配置 ========== */

    /**
     * @brief 读取 PGA 增益
     * @param pga [out] PGA 值
     * @return ESP_OK 成功
     */
    esp_err_t ReadPga(uint8_t& pga);

    /**
     * @brief 写入 PGA 增益
     * @param pga PGA 值
     * @return ESP_OK 成功
     */
    esp_err_t WritePga(uint8_t pga);

    /* ========== KeyIO 控制 ========== */

    /**
     * @brief 启用 KeyIO 输出
     * @return ESP_OK 成功
     */
    esp_err_t SetKeyioOutEnable();

    /**
     * @brief 禁用 KeyIO 输出
     * @return ESP_OK 成功
     */
    esp_err_t SetKeyioOutDisable();

    /* ========== 噪声读取 ========== */

    /**
     * @brief 初始化噪声读取
     * @param period_num 周期数
     * @return ESP_OK 成功
     */
    esp_err_t ReadNoiseInit(uint16_t period_num);

    /**
     * @brief 读取噪声数据
     * @param noise_data [out] 噪声数据
     * @return ESP_OK 成功
     */
    esp_err_t ReadNoise(int16_t& noise_data);

    /* ========== 固件更新 ========== */

    /**
     * @brief 强制更新固件
     * @param fw_array 固件数组
     * @return ESP_OK 成功
     */
    esp_err_t FwForceUpdate(const uint8_t* fw_array);

    /**
     * @brief 高版本更新固件
     * @param fw_array 固件数组
     * @return ESP_OK: 成功, ESP_ERR_NOT_SUPPORTED: 不需要更新
     */
    esp_err_t FwHighVersionUpdate(const uint8_t* fw_array);

    /* ========== 状态查询 ========== */

    /**
     * @brief 检查是否已初始化
     */
    bool IsInitialized() const { return initialized_; }

    /**
     * @brief 设置传感器数据回调
     */
    void SetSensorDataCallback(SensorDataCallback callback) { callback_ = callback; }

    /**
     * @brief 获取默认固件数组
     */
    static const uint8_t* GetDefaultFwArray();

    /**
     * @brief 获取默认固件大小
     */
    static size_t GetDefaultFwSize();

private:
    i2c_master_bus_handle_t i2c_bus_;
    i2c_master_dev_handle_t i2c_dev_;
    gpio_num_t rst_pin_;
    uint8_t i2c_addr_;
    bool initialized_;
    bool owns_i2c_bus_;  // 是否拥有 I2C 总线（需要自行释放）
    SensorDataCallback callback_;

    /* ========== 底层 I2C 操作 ========== */
    esp_err_t I2cWrite(uint8_t reg_addr, const uint8_t* data, size_t length);
    esp_err_t I2cRead(uint8_t reg_addr, uint8_t* data, size_t length);

    /* ========== 辅助函数 ========== */
    void DelayMs(uint32_t ms);
    void PowerUp();
    void PowerDown();
    esp_err_t SoftReset();
    esp_err_t WakeupI2c();
    esp_err_t CleanDebugMode();
    esp_err_t SetDebugMode(uint8_t mode_num);
    esp_err_t SetDebugReady(uint8_t ready_num);
    uint8_t GetDebugReady();
    esp_err_t WriteDebugData(const uint8_t* data, uint8_t length);
    esp_err_t ReadDebugData(uint8_t* data, uint8_t length);
};

#endif // CONFIG_BOARD_TYPE_ESP32S3_BLEHD_V1

#endif // _CS_PRESS_DRIVER_H_

