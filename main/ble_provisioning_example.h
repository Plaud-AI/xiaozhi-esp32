#ifndef BLE_PROVISIONING_EXAMPLE_H
#define BLE_PROVISIONING_EXAMPLE_H

#include "ble_wifi_provisioner.h"
#include "application.h"
#include "settings.h"
#include <esp_log.h>
#include <esp_system.h>

/**
 * @brief BLE WiFi配网示例
 * 
 * 展示如何在主程序中集成BLE WiFi配网功能
 * 
 * 使用方法:
 * 1. 在 wifi_board.cc 的 EnterWifiConfigMode() 中调用
 * 2. 或者在设备检测到未配置WiFi时自动启动
 * 
 * 示例代码:
 * 
 * ```cpp
 * #include "ble_provisioning_example.h"
 * 
 * void WifiBoard::EnterWifiConfigMode() {
 *     // 初始化并启动BLE WiFi配网
 *     InitializeBLEWiFiProvisioning();
 *     
 *     // 等待配网完成（配网成功后会自动重启）
 *     while (true) {
 *         vTaskDelay(pdMS_TO_TICKS(10000));
 *     }
 * }
 * ```
 */

static const char* BLE_PROV_TAG = "BLEProvisioningExample";

/**
 * @brief 初始化BLE WiFi配网服务
 * 
 * 这个函数会:
 * 1. 初始化BLE WiFi Provisioner
 * 2. 设置配网成功/失败回调
 * 3. 启动BLE广播
 * 4. 等待手机连接并配网
 */
inline void InitializeBLEWiFiProvisioning() {
    ESP_LOGI(BLE_PROV_TAG, "");
    ESP_LOGI(BLE_PROV_TAG, "╔════════════════════════════════════════╗");
    ESP_LOGI(BLE_PROV_TAG, "║   启动 BLE WiFi 配网服务              ║");
    ESP_LOGI(BLE_PROV_TAG, "╚════════════════════════════════════════╝");
    ESP_LOGI(BLE_PROV_TAG, "");

    // 获取BLE WiFi Provisioner实例
    auto& provisioner = BLEWiFiProvisioner::GetInstance();

    // 设置配网成功回调
    provisioner.SetProvisionSuccessCallback([](const std::string& ssid, const std::string& password) {
        ESP_LOGI(BLE_PROV_TAG, "");
        ESP_LOGI(BLE_PROV_TAG, "╔════════════════════════════════════════╗");
        ESP_LOGI(BLE_PROV_TAG, "║   ✅ WiFi配网成功！                    ║");
        ESP_LOGI(BLE_PROV_TAG, "╚════════════════════════════════════════╝");
        ESP_LOGI(BLE_PROV_TAG, "SSID: %s", ssid.c_str());
        ESP_LOGI(BLE_PROV_TAG, "设备将在2秒后重启...");
        ESP_LOGI(BLE_PROV_TAG, "");
        
        // 可以在这里添加额外的处理
        // 例如：显示成功提示、播放成功音效等
        auto& application = Application::GetInstance();
        application.Alert("WiFi配网成功", ("已连接到: " + ssid).c_str(), "check-circle");
    });

    // 设置配网失败回调
    provisioner.SetProvisionFailureCallback([](const std::string& error_message) {
        ESP_LOGE(BLE_PROV_TAG, "");
        ESP_LOGE(BLE_PROV_TAG, "╔════════════════════════════════════════╗");
        ESP_LOGE(BLE_PROV_TAG, "║   ❌ WiFi配网失败                      ║");
        ESP_LOGE(BLE_PROV_TAG, "╚════════════════════════════════════════╝");
        ESP_LOGE(BLE_PROV_TAG, "错误: %s", error_message.c_str());
        ESP_LOGE(BLE_PROV_TAG, "请在手机App中重试");
        ESP_LOGE(BLE_PROV_TAG, "");
        
        // 可以在这里添加额外的处理
        // 例如：显示错误提示、播放错误音效等
        auto& application = Application::GetInstance();
        application.Alert("WiFi配网失败", error_message.c_str(), "exclamation-triangle");
    });

    // 初始化BLE WiFi配网服务
    // 设备名称可以自定义，例如包含设备MAC地址后4位
    std::string device_name = "XiaoZhi-AI";
    if (!provisioner.Initialize(device_name)) {
        ESP_LOGE(BLE_PROV_TAG, "❌ BLE WiFi配网服务初始化失败");
        return;
    }

    // 启动BLE广播
    if (!provisioner.Start()) {
        ESP_LOGE(BLE_PROV_TAG, "❌ BLE广播启动失败");
        return;
    }

    ESP_LOGI(BLE_PROV_TAG, "");
    ESP_LOGI(BLE_PROV_TAG, "╔════════════════════════════════════════╗");
    ESP_LOGI(BLE_PROV_TAG, "║   📱 等待手机连接...                  ║");
    ESP_LOGI(BLE_PROV_TAG, "╚════════════════════════════════════════╝");
    ESP_LOGI(BLE_PROV_TAG, "");
    ESP_LOGI(BLE_PROV_TAG, "请使用手机App执行以下步骤:");
    ESP_LOGI(BLE_PROV_TAG, "1. 打开蓝牙");
    ESP_LOGI(BLE_PROV_TAG, "2. 扫描设备");
    ESP_LOGI(BLE_PROV_TAG, "3. 连接到: %s", device_name.c_str());
    ESP_LOGI(BLE_PROV_TAG, "4. 选择WiFi并输入密码");
    ESP_LOGI(BLE_PROV_TAG, "5. 等待配网完成");
    ESP_LOGI(BLE_PROV_TAG, "");

    // 显示在屏幕上（如果有显示屏）
    auto& application = Application::GetInstance();
    std::string hint = "请使用手机App连接设备\n";
    hint += "设备名称: " + device_name + "\n\n";
    hint += "配网步骤:\n";
    hint += "1. 打开App并扫描设备\n";
    hint += "2. 选择WiFi并输入密码\n";
    hint += "3. 等待配网完成";
    
    application.Alert("BLE WiFi配网模式", hint.c_str(), "bluetooth");
}

/**
 * @brief 检查是否需要进入BLE配网模式
 * 
 * 在以下情况下返回true:
 * 1. 没有保存任何WiFi配置
 * 2. 用户手动触发（例如长按按钮）
 * 3. WiFi连接多次失败
 * 
 * @return true 需要进入配网模式，false 不需要
 */
inline bool ShouldEnterBLEProvisioningMode() {
    // 检查是否有保存的WiFi配置
    auto& ssid_manager = SsidManager::GetInstance();
    auto ssid_list = ssid_manager.GetSsidList();
    
    if (ssid_list.empty()) {
        ESP_LOGI(BLE_PROV_TAG, "未找到WiFi配置，进入BLE配网模式");
        return true;
    }

    // 检查是否设置了强制配网标志
    Settings settings("wifi", false);
    int force_ble_provisioning = settings.GetInt("force_ble_provisioning", 0);
    if (force_ble_provisioning == 1) {
        ESP_LOGI(BLE_PROV_TAG, "检测到强制BLE配网标志，进入BLE配网模式");
        
        // 清除标志
        Settings rw_settings("wifi", true);
        rw_settings.SetInt("force_ble_provisioning", 0);
        
        return true;
    }

    return false;
}

/**
 * @brief 触发BLE配网模式
 * 
 * 可以在用户长按按钮、MCP命令等场景下调用
 * 设置标志后重启设备，设备启动时会进入BLE配网模式
 */
inline void TriggerBLEProvisioningMode() {
    ESP_LOGI(BLE_PROV_TAG, "触发BLE配网模式");
    
    // 设置强制BLE配网标志
    Settings settings("wifi", true);
    settings.SetInt("force_ble_provisioning", 1);
    
    ESP_LOGI(BLE_PROV_TAG, "将在1秒后重启进入BLE配网模式...");
    
    // 显示提示
    auto& application = Application::GetInstance();
    application.Alert("进入BLE配网模式", "设备即将重启...", "bluetooth");
    
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

/**
 * @brief 完整的使用示例
 * 
 * 展示如何在WifiBoard中集成BLE WiFi配网
 */
inline void BLEProvisioningUsageExample() {
    ESP_LOGI(BLE_PROV_TAG, "===========================================");
    ESP_LOGI(BLE_PROV_TAG, "BLE WiFi配网使用示例");
    ESP_LOGI(BLE_PROV_TAG, "===========================================");
    ESP_LOGI(BLE_PROV_TAG, "");
    ESP_LOGI(BLE_PROV_TAG, "方法1: 在WifiBoard::StartNetwork()中检查");
    ESP_LOGI(BLE_PROV_TAG, "```cpp");
    ESP_LOGI(BLE_PROV_TAG, "void WifiBoard::StartNetwork() {");
    ESP_LOGI(BLE_PROV_TAG, "    if (ShouldEnterBLEProvisioningMode()) {");
    ESP_LOGI(BLE_PROV_TAG, "        InitializeBLEWiFiProvisioning();");
    ESP_LOGI(BLE_PROV_TAG, "        while (true) {");
    ESP_LOGI(BLE_PROV_TAG, "            vTaskDelay(pdMS_TO_TICKS(10000));");
    ESP_LOGI(BLE_PROV_TAG, "        }");
    ESP_LOGI(BLE_PROV_TAG, "    }");
    ESP_LOGI(BLE_PROV_TAG, "    // 正常WiFi连接流程...");
    ESP_LOGI(BLE_PROV_TAG, "}");
    ESP_LOGI(BLE_PROV_TAG, "```");
    ESP_LOGI(BLE_PROV_TAG, "");
    ESP_LOGI(BLE_PROV_TAG, "方法2: 通过MCP命令触发");
    ESP_LOGI(BLE_PROV_TAG, "```cpp");
    ESP_LOGI(BLE_PROV_TAG, "// 在MCP命令处理中添加");
    ESP_LOGI(BLE_PROV_TAG, "if (tool_name == \"reset_wifi_config\") {");
    ESP_LOGI(BLE_PROV_TAG, "    TriggerBLEProvisioningMode();");
    ESP_LOGI(BLE_PROV_TAG, "}");
    ESP_LOGI(BLE_PROV_TAG, "```");
    ESP_LOGI(BLE_PROV_TAG, "");
    ESP_LOGI(BLE_PROV_TAG, "方法3: 通过按钮触发");
    ESP_LOGI(BLE_PROV_TAG, "```cpp");
    ESP_LOGI(BLE_PROV_TAG, "// 在按钮长按事件中");
    ESP_LOGI(BLE_PROV_TAG, "if (button_long_pressed) {");
    ESP_LOGI(BLE_PROV_TAG, "    TriggerBLEProvisioningMode();");
    ESP_LOGI(BLE_PROV_TAG, "}");
    ESP_LOGI(BLE_PROV_TAG, "```");
    ESP_LOGI(BLE_PROV_TAG, "");
    ESP_LOGI(BLE_PROV_TAG, "===========================================");
}

#endif // BLE_PROVISIONING_EXAMPLE_H

