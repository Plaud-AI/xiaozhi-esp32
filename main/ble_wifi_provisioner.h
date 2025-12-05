#ifndef BLE_WIFI_PROVISIONER_H
#define BLE_WIFI_PROVISIONER_H

#include <string>
#include <functional>
#include <vector>
#include "bluetooth_service.h"

// 前向声明
struct cJSON;

/**
 * @brief BLE WiFi 配网处理器
 * 
 * 实现完整的BLE WiFi配网协议，支持以下功能：
 * 1. WiFi扫描
 * 2. WiFi配置
 * 3. 获取设备信息
 * 4. 获取已保存WiFi列表
 * 5. 删除WiFi配置
 * 
 * 符合文档规范：docs/ble-wifi-provisioning-spec.md
 */
class BLEWiFiProvisioner {
public:
    /**
     * @brief 获取单例实例
     */
    static BLEWiFiProvisioner& GetInstance() {
        static BLEWiFiProvisioner instance;
        return instance;
    }

    // 删除拷贝构造和赋值
    BLEWiFiProvisioner(const BLEWiFiProvisioner&) = delete;
    BLEWiFiProvisioner& operator=(const BLEWiFiProvisioner&) = delete;

    /**
     * @brief 初始化BLE WiFi配网服务
     * @param device_name 设备名称
     * @return true 初始化成功，false 初始化失败
     */
    bool Initialize(const std::string& device_name = "XiaoZhi-AI");

    /**
     * @brief 启动BLE WiFi配网服务
     * @return true 启动成功，false 启动失败
     */
    bool Start();

    /**
     * @brief 停止BLE WiFi配网服务
     */
    void Stop();

    /**
     * @brief 是否正在配网
     */
    bool IsProvisioning() const { return is_provisioning_; }

    /**
     * @brief 设置配网成功回调
     * @param callback 配网成功时的回调函数（ssid, password）
     */
    void SetProvisionSuccessCallback(std::function<void(const std::string&, const std::string&)> callback);

    /**
     * @brief 设置配网失败回调
     * @param callback 配网失败时的回调函数（error_message）
     */
    void SetProvisionFailureCallback(std::function<void(const std::string&)> callback);

private:
    BLEWiFiProvisioner();
    ~BLEWiFiProvisioner();

    /**
     * @brief 处理接收到的数据
     * @param data 接收到的JSON数据
     */
    void HandleReceivedData(const std::string& data);

    /**
     * @brief 处理WiFi扫描命令
     */
    void HandleScanWiFiCommand();

    /**
     * @brief 处理WiFi配置命令
     * @param ssid WiFi SSID
     * @param password WiFi密码
     * @param bssid WiFi BSSID（可选）
     */
    void HandleWiFiConfigCommand(const std::string& ssid, const std::string& password, 
                                  const std::string& bssid = "");

    /**
     * @brief 处理获取设备信息命令
     */
    void HandleGetDeviceInfoCommand();

    /**
     * @brief 处理获取已保存WiFi列表命令
     */
    void HandleGetSavedWiFiCommand();

    /**
     * @brief 处理删除WiFi配置命令
     * @param ssid 要删除的WiFi SSID
     */
    void HandleDeleteWiFiCommand(const std::string& ssid);

    /**
     * @brief 处理断开WiFi连接命令
     */
    void HandleDisconnectWiFiCommand();

    /**
     * @brief 处理设置唤醒词命令
     */
    void HandleSetWakeWordsCommand(cJSON* root);

    /**
     * @brief 处理获取唤醒词列表命令
     */
    void HandleGetWakeWordsCommand();

    /**
     * @brief 处理删除唤醒词命令
     */
    void HandleDeleteWakeWordCommand(cJSON* root);

    /**
     * @brief 处理重置唤醒词命令
     */
    void HandleResetWakeWordsCommand();

    /**
     * @brief 处理设置 OTA URL 命令
     */
    void HandleSetOtaUrlCommand(cJSON* root);

    /**
     * @brief 处理获取 OTA URL 命令
     */
    void HandleGetOtaUrlCommand();

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // 新增指令（v2.1）
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    /**
     * @brief 处理设置语音唤醒开关命令
     */
    void HandleSetWakeWordEnabledCommand(cJSON* root);

    /**
     * @brief 处理设置音量命令
     */
    void HandleSetVolumeCommand(cJSON* root);

    /**
     * @brief 处理检查固件更新命令
     */
    void HandleCheckFirmwareUpdateCommand();

    /**
     * @brief 处理重置设备命令
     */
    void HandleResetDeviceCommand();

    /**
     * @brief 处理解绑设备命令
     */
    void HandleUnbindDeviceCommand();

    /**
     * @brief 发送响应数据到手机
     * @param json_response JSON响应字符串
     * @return true 发送成功，false 发送失败
     */
    bool SendResponse(const std::string& json_response);

    /**
     * @brief 发送错误响应
     * @param cmd 命令名称
     * @param error_code 错误码
     * @param error_message 错误消息
     */
    void SendErrorResponse(const std::string& cmd, int error_code, const std::string& error_message);

    /**
     * @brief 构建WiFi扫描结果JSON
     * @return JSON字符串
     */
    std::string BuildScanResultJson();

    /**
     * @brief 构建设备信息JSON
     * @return JSON字符串
     */
    std::string BuildDeviceInfoJson();

    /**
     * @brief 构建已保存WiFi列表JSON
     * @return JSON字符串
     */
    std::string BuildSavedWiFiListJson();

    /**
     * @brief 获取加密模式名称
     * @param auth_mode ESP32 WiFi认证模式
     * @return 加密模式整数值（符合文档规范）
     */
    int GetAuthModeValue(int esp_auth_mode);

    bool initialized_;
    bool is_provisioning_;
    std::function<void(const std::string&, const std::string&)> provision_success_callback_;
    std::function<void(const std::string&)> provision_failure_callback_;
};

#endif // BLE_WIFI_PROVISIONER_H

