/**
 * @file blehd_hardware.h
 * @brief ESP32S3 BLEHD V1 硬件驱动头文件聚合
 * 
 * @deprecated BlehdHardwareManager 已废弃，请使用 Board 接口获取硬件驱动：
 *   - Board::GetInstance().GetNfcDriver()
 *   - Board::GetInstance().GetDualServoController()
 *   - Board::GetInstance().GetWs2812Driver()
 *   - Board::GetInstance().GetPressureDriver()
 * 
 * 本文件现在仅作为头文件聚合器，包含 BLEHD V1 板子的所有外设驱动头文件。
 * 所有硬件驱动已在板子初始化时由 Esp32S3BlehdV1Board 统一创建和管理。
 * 
 * 引脚配置：
 * | 外设      | 引脚定义                           |
 * |-----------|-----------------------------------|
 * | NFC       | TX=GPIO41, RX=GPIO42              |
 * | 舵机1     | GPIO1                             |
 * | 舵机2     | GPIO2                             |
 * | RGB灯带   | GPIO38, 6个LED                    |
 * | 压感      | SDA=GPIO5, SCL=GPIO4 (共享I2C)    |
 * 
 * 推荐使用方式：
 * @code
 *     #include "board.h"
 *     
 *     // 获取 NFC 驱动
 *     auto nfc = Board::GetInstance().GetNfcDriver();
 *     if (nfc) {
 *         nfc->StartContinuousMode();
 *     }
 *     
 *     // 获取舵机控制器
 *     auto servos = Board::GetInstance().GetDualServoController();
 *     if (servos) {
 *         servos->SetAngle(SERVO_ID_1, 90);
 *     }
 *     
 *     // 获取 WS2812 灯带
 *     auto leds = Board::GetInstance().GetWs2812Driver();
 *     if (leds) {
 *         leds->SetAllColor(255, 0, 0);  // 红色
 *     }
 * @endcode
 */

#ifndef _BLEHD_HARDWARE_H_
#define _BLEHD_HARDWARE_H_

#include "sdkconfig.h"

// 仅在 ESP32S3_BLEHD_V1 板子上启用
#ifdef CONFIG_BOARD_TYPE_ESP32S3_BLEHD_V1

// 包含板子配置
#include "config.h"

// 包含所有硬件驱动
#include "nfc_driver.h"
#include "servo_driver.h"
#include "ws2812_driver.h"
// 注意：压感驱动 (cs_press_driver.h) 现在在板子文件中单独初始化

/**
 * @brief BLEHD 硬件管理器
 * 
 * @deprecated 此类已废弃！请使用 Board 接口代替：
 *   - Board::GetInstance().GetNfcDriver()
 *   - Board::GetInstance().GetDualServoController()
 *   - Board::GetInstance().GetWs2812Driver()
 *   - Board::GetInstance().GetPressureDriver()
 * 
 * 此类会创建独立的驱动实例，与 Board 创建的实例冲突！
 * 保留此类仅为向后兼容，新代码请勿使用。
 */
class [[deprecated("Use Board::GetInstance() instead")]] BlehdHardwareManager {
public:
    /**
     * @brief 获取单例实例
     */
    static BlehdHardwareManager& GetInstance() {
        static BlehdHardwareManager instance;
        return instance;
    }

    /**
     * @brief 初始化所有硬件
     * @return ESP_OK 成功
     */
    esp_err_t InitAll() {
        esp_err_t ret = ESP_OK;

        // 初始化双舵机
        if (dual_servo_.Init() != ESP_OK) {
            ESP_LOGW("BlehdHW", "Failed to init dual servo");
            ret = ESP_FAIL;
        }

        // 初始化 RGB 灯带
        if (rgb_strip_.Init() != ESP_OK) {
            ESP_LOGW("BlehdHW", "Failed to init RGB strip");
            ret = ESP_FAIL;
        }

        // 初始化 NFC
        if (nfc_.Init() != ESP_OK) {
            ESP_LOGW("BlehdHW", "Failed to init NFC");
            ret = ESP_FAIL;
        }

        // 注意：压感驱动在板子文件中单独初始化

        return ret;
    }

    /**
     * @brief 释放所有硬件资源
     */
    void DeinitAll() {
        dual_servo_.Deinit();
        rgb_strip_.Deinit();
        nfc_.Deinit();
    }

    /**
     * @brief 获取双舵机控制器
     */
    DualServoController& GetDualServo() { return dual_servo_; }

    /**
     * @brief 获取 RGB 灯带驱动
     */
    Ws2812Driver& GetRgbStrip() { return rgb_strip_; }

    /**
     * @brief 获取 NFC 驱动
     */
    NfcDriver& GetNfc() { return nfc_; }

private:
    BlehdHardwareManager()
        : dual_servo_(SERVO1_PIN, SERVO2_PIN)
        , rgb_strip_(WS2812_PIN, WS2812_LED_NUM)
        , nfc_(NFC_UART_PORT, NFC_TX_PIN, NFC_RX_PIN)
    {}

    ~BlehdHardwareManager() {
        DeinitAll();
    }

    // 禁止拷贝
    BlehdHardwareManager(const BlehdHardwareManager&) = delete;
    BlehdHardwareManager& operator=(const BlehdHardwareManager&) = delete;

    DualServoController dual_servo_;
    Ws2812Driver rgb_strip_;
    NfcDriver nfc_;
};

#endif // CONFIG_BOARD_TYPE_ESP32S3_BLEHD_V1

#endif // _BLEHD_HARDWARE_H_
