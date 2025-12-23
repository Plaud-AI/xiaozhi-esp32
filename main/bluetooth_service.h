#ifndef BLUETOOTH_SERVICE_H
#define BLUETOOTH_SERVICE_H

#include <string>
#include <functional>
#include <memory>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"

/**
 * @brief 蓝牙服务类
 * 
 * 实现BLE广播、连接和数据传输功能
 * 手机可以扫描到设备并连接
 */
class BluetoothService {
public:
    /**
     * @brief 获取单例实例
     */
    static BluetoothService& GetInstance() {
        static BluetoothService instance;
        return instance;
    }

    // 删除拷贝构造和赋值
    BluetoothService(const BluetoothService&) = delete;
    BluetoothService& operator=(const BluetoothService&) = delete;

    /**
     * @brief 初始化蓝牙服务
     * @param device_name 设备名称（会显示在手机扫描列表中）
     * @return true 初始化成功，false 初始化失败
     */
    bool Initialize(const std::string& device_name);

    /**
     * @brief 启动BLE广播
     * @return true 启动成功，false 启动失败
     */
    bool StartAdvertising();

    /**
     * @brief 停止BLE广播
     */
    void StopAdvertising();

    /**
     * @brief 完全停止 BLE 协议栈（用于 WiFi 连接成功后释放资源）
     * 
     * 按正确顺序停止:
     * 1. 停止 BLE 广播
     * 2. 停止 NimBLE Host 任务
     * 3. 取消初始化 NimBLE port
     * 
     * 注意: 调用此方法后，需要重启设备才能再次使用 BLE
     */
    void Deinitialize();

    /**
     * @brief 发送数据到已连接的客户端（自动分包）
     * @param data 要发送的数据
     * @return true 发送成功，false 发送失败
     * 
     * 注意：此函数会自动根据MTU大小分包发送，并在末尾添加换行符作为结束标记
     */
    bool SendData(const std::string& data);
    
    /**
     * @brief 获取当前MTU大小
     * @return MTU大小（字节）
     */
    uint16_t GetMTU() const { return mtu_; }

    /**
     * @brief 设置数据接收回调
     * @param callback 接收到数据时的回调函数
     */
    void SetDataReceivedCallback(std::function<void(const std::string&)> callback);

    /**
     * @brief 获取设备名称
     */
    std::string GetDeviceName() const { return device_name_; }

    /**
     * @brief 获取设备MAC地址
     */
    std::string GetMacAddress() const;

    /**
     * @brief 是否已连接
     */
    bool IsConnected() const { return connected_; }

    /**
     * @brief 是否已通过应用层认证
     */
    bool IsAuthenticated() const { return authenticated_; }

    /**
     * @brief 获取厂商标识码（用于广播过滤）
     * @return 厂商标识码，App 端用于过滤扫描结果
     */
    static uint16_t GetManufacturerId();

    /**
     * @brief 获取应用签名标识（用于广播过滤）
     * @return 应用签名标识，App 端用于验证设备
     */
    static uint32_t GetAppSignature();

    /**
     * @brief 主动断开当前连接
     * @param reason 断开原因（可选）
     */
    void Disconnect(uint8_t reason = 0x13);

    /**
     * @brief 设置是否允许自动重启广播
     * @param enabled true 允许断开后自动重启广播，false 禁止
     * 
     * 用于语音交互状态下禁止 BLE 广播自动重启
     */
    void SetAdvertisingEnabled(bool enabled) { advertising_enabled_ = enabled; }
    
    /**
     * @brief 获取是否允许自动重启广播
     */
    bool IsAdvertisingEnabled() const { return advertising_enabled_; }

    // NimBLE回调函数(需要是public的，因为要在C结构体中使用)
    static int gap_event_handler(struct ble_gap_event *event, void *arg);
    static int gatt_svr_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                   struct ble_gatt_access_ctxt *ctxt, void *arg);

private:
    BluetoothService();
    ~BluetoothService();

    std::string device_name_;
    bool initialized_;
    bool connected_;
    bool authenticated_;  // 应用层认证状态
    bool advertising_enabled_;  // 是否允许自动重启广播（语音交互时禁止）
    uint16_t conn_handle_;
    uint16_t mtu_;  // 当前MTU大小
    std::function<void(const std::string&)> data_received_callback_;
    
    // 分包接收缓冲区
    std::string receive_buffer_;  // 累积接收到的数据片段
    
    // 认证挑战码
    uint8_t auth_challenge_[32];
    
    // ═══════════════════════════════════════════════════════════════
    // 异步处理相关成员（解决 nimble_host 栈溢出问题）
    // ═══════════════════════════════════════════════════════════════
    QueueHandle_t message_queue_;      // 消息队列
    TaskHandle_t process_task_;        // 处理任务句柄
    bool process_task_running_;        // 处理任务运行标志
    
    static constexpr size_t MESSAGE_QUEUE_SIZE = 8;      // 队列容量
    static constexpr size_t MAX_MESSAGE_LENGTH = 1024;   // 单条消息最大长度
    static constexpr size_t PROCESS_TASK_STACK = 6144;   // 处理任务栈大小
    
    /**
     * @brief 处理接收到的数据片段（支持分包重组）
     * @param data 接收到的数据片段
     * 
     * 注意：此函数在 nimble_host 任务中调用，只做数据重组，
     * 完整消息会放入队列由专用任务处理
     */
    void ProcessReceivedData(const std::string& data);
    
    /**
     * @brief 将完整消息放入队列（异步处理）
     * @param message 完整的消息
     */
    void EnqueueMessage(const std::string& message);
    
    /**
     * @brief 消息处理任务入口（静态）
     */
    static void ProcessTaskEntry(void* arg);
    
    /**
     * @brief 消息处理任务主循环
     */
    void ProcessTaskLoop();
    
    /**
     * @brief 处理认证请求
     * @param data 认证数据
     * @return true 认证成功，false 认证失败
     */
    bool HandleAuthRequest(const std::string& data);
    
    /**
     * @brief 生成新的认证挑战码
     */
    void GenerateAuthChallenge();
};

#endif // BLUETOOTH_SERVICE_H

