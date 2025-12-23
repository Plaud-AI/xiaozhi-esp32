#ifndef _NFC_DRIVER_H_
#define _NFC_DRIVER_H_

#include <driver/uart.h>
#include <driver/gpio.h>
#include <esp_err.h>
#include <functional>
#include <vector>
#include <string>
#include <atomic>

/**
 * @brief NFC 卡片信息结构体
 */
typedef struct {
    std::vector<uint8_t> card_id;   // 卡片 ID 字节数组
    std::string card_id_hex;         // 卡片 ID 十六进制字符串
    uint8_t id_length;               // ID 长度
    uint32_t timestamp;              // 检测时间戳 (毫秒)
} nfc_card_info_t;

/**
 * @brief NFC 配置结构体
 */
typedef struct {
    uart_port_t uart_port;          // UART 端口号
    gpio_num_t tx_pin;              // TX 引脚
    gpio_num_t rx_pin;              // RX 引脚
    uint32_t baudrate;              // 波特率
    uint32_t rx_buffer_size;        // 接收缓冲区大小
    uint32_t tx_buffer_size;        // 发送缓冲区大小
} nfc_config_t;

/**
 * @brief NFC 事件类型
 */
typedef enum {
    NFC_EVENT_CARD_DETECTED,        // 检测到卡片
    NFC_EVENT_CARD_REMOVED,         // 卡片移除
    NFC_EVENT_ERROR                 // 错误事件
} nfc_event_type_t;

/**
 * @brief NFC 驱动类
 * 
 * 基于 UART 的 NFC 模块驱动，支持：
 * - 单次轮询读卡
 * - 连续读卡模式
 * - 卡片事件回调
 * 
 * 协议说明：
 * - 轮询命令: 43 4D 74 02 03 00 00 00 00 00
 * - 无卡响应: 43 4D 74 03 03 00 01 00 30 31
 * - 有卡响应: 43 4D 74 03 0B 00 01 00 00 04 00 08 04 A9 07 F8 03 5C
 *                                              ↑ 卡号从 byte[13] 开始
 */
class NfcDriver {
public:
    /**
     * @brief 卡片检测回调函数类型
     */
    using CardCallback = std::function<void(nfc_event_type_t event, const nfc_card_info_t& card_info)>;

    /**
     * @brief 使用默认配置构造
     * @param uart_port UART 端口
     * @param tx_pin TX 引脚
     * @param rx_pin RX 引脚
     */
    NfcDriver(uart_port_t uart_port, gpio_num_t tx_pin, gpio_num_t rx_pin);

    /**
     * @brief 使用完整配置构造
     * @param config NFC 配置
     */
    explicit NfcDriver(const nfc_config_t& config);

    ~NfcDriver();

    /**
     * @brief 初始化 NFC 模块
     * @return ESP_OK 成功
     */
    esp_err_t Init();

    /**
     * @brief 释放资源
     */
    void Deinit();

    /**
     * @brief 发送轮询命令
     * @return ESP_OK 成功
     */
    esp_err_t SendPollingCommand();

    /**
     * @brief 单次读卡
     * @param card_info [out] 卡片信息
     * @param timeout_ms 超时时间 (毫秒)
     * @return true 检测到卡片, false 无卡或超时
     */
    bool ReadCard(nfc_card_info_t& card_info, uint32_t timeout_ms = 100);

    /**
     * @brief 单次轮询并读卡
     * @param card_info [out] 卡片信息
     * @param timeout_ms 超时时间 (毫秒)
     * @return true 检测到卡片, false 无卡或超时
     */
    bool PollAndRead(nfc_card_info_t& card_info, uint32_t timeout_ms = 100);

    /**
     * @brief 启动连续读卡模式
     * @param interval_ms 轮询间隔 (毫秒)
     * @return ESP_OK 成功
     */
    esp_err_t StartContinuousMode(uint32_t interval_ms = 200);

    /**
     * @brief 停止连续读卡模式
     */
    void StopContinuousMode();

    /**
     * @brief 检查连续模式是否运行中
     */
    bool IsContinuousModeRunning() const { return continuous_running_.load(); }

    /**
     * @brief 设置卡片回调
     * @param callback 回调函数
     */
    void SetCardCallback(CardCallback callback) { card_callback_ = callback; }

    /**
     * @brief 检查是否已初始化
     */
    bool IsInitialized() const { return initialized_; }

    /**
     * @brief 获取 UART 端口
     */
    uart_port_t GetUartPort() const { return config_.uart_port; }

    /**
     * @brief 获取 TX 引脚
     */
    gpio_num_t GetTxPin() const { return config_.tx_pin; }

    /**
     * @brief 获取 RX 引脚
     */
    gpio_num_t GetRxPin() const { return config_.rx_pin; }

    /**
     * @brief 获取波特率
     */
    uint32_t GetBaudrate() const { return config_.baudrate; }

    /**
     * @brief 获取连续模式轮询间隔
     */
    uint32_t GetContinuousInterval() const { return continuous_interval_ms_; }

private:
    nfc_config_t config_;               // NFC 配置
    bool initialized_;                  // 初始化标志
    CardCallback card_callback_;        // 卡片回调
    
    // 连续模式相关
    std::atomic<bool> continuous_running_;  // 连续模式运行标志
    uint32_t continuous_interval_ms_;       // 轮询间隔
    TaskHandle_t continuous_task_;          // 连续模式任务句柄

    // 上一次检测到的卡片 ID（用于去重）
    std::string last_card_id_;
    uint32_t last_card_time_;

    /**
     * @brief 解析响应数据
     * @param data 接收到的数据
     * @param length 数据长度
     * @param card_info [out] 卡片信息
     * @return true 成功解析出卡片, false 无卡或解析失败
     */
    bool ParseResponse(const uint8_t* data, int length, nfc_card_info_t& card_info);

    /**
     * @brief 字节数组转十六进制字符串
     */
    static std::string BytesToHexString(const uint8_t* data, uint8_t length);

    /**
     * @brief 获取默认配置
     */
    static nfc_config_t GetDefaultConfig(uart_port_t uart_port, gpio_num_t tx_pin, gpio_num_t rx_pin);

    /**
     * @brief 连续模式任务函数
     */
    static void ContinuousTaskFunc(void* arg);
};

#endif // _NFC_DRIVER_H_

