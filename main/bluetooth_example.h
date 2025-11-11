#ifndef BLUETOOTH_EXAMPLE_H
#define BLUETOOTH_EXAMPLE_H

/**
 * @file bluetooth_example.h
 * @brief 蓝牙服务使用示例
 * 
 * 这个文件展示了如何在应用中使用蓝牙服务
 */

#include "bluetooth_service.h"
#include <esp_log.h>

#define BT_EXAMPLE_TAG "BTExample"

/**
 * @brief 初始化并启动蓝牙服务
 * 
 * 使用方法：
 * 1. 在main.cc或application.cc中调用这个函数
 * 2. 使用手机的蓝牙扫描功能查找设备
 * 3. 设备名称：ESP32-PLAUD
 * 
 * @example
 * ```cpp
 * // 在 main() 中调用
 * InitializeBluetoothService();
 * ```
 */
inline void InitializeBluetoothService() {
    auto& bt = BluetoothService::GetInstance();
    
    // 设置数据接收回调（可选）
    bt.SetDataReceivedCallback([](const std::string& data) {
        ESP_LOGI(BT_EXAMPLE_TAG, "📱 收到手机发送的数据: %s", data.c_str());
        // 在这里处理接收到的数据
    });
    
    // 初始化蓝牙服务
    std::string device_name = "ESP32-PLAUD";
    if (bt.Initialize(device_name)) {
        ESP_LOGI(BT_EXAMPLE_TAG, "========================================");
        ESP_LOGI(BT_EXAMPLE_TAG, "🎉 蓝牙服务已启动");
        ESP_LOGI(BT_EXAMPLE_TAG, "📱 设备名称: %s", bt.GetDeviceName().c_str());
        ESP_LOGI(BT_EXAMPLE_TAG, "📍 MAC地址: %s", bt.GetMacAddress().c_str());
        ESP_LOGI(BT_EXAMPLE_TAG, "========================================");
        ESP_LOGI(BT_EXAMPLE_TAG, "");
        
        // 等待一下让 NimBLE 完全初始化
        ESP_LOGI(BT_EXAMPLE_TAG, "⏳ 等待蓝牙堆栈同步...");
        vTaskDelay(pdMS_TO_TICKS(2000));  // 等待2秒
        
        ESP_LOGI(BT_EXAMPLE_TAG, "📲 如何连接:");
        ESP_LOGI(BT_EXAMPLE_TAG, "1. 打开手机蓝牙");
        ESP_LOGI(BT_EXAMPLE_TAG, "2. 使用蓝牙调试App (推荐: nRF Connect 或 LightBlue)");
        ESP_LOGI(BT_EXAMPLE_TAG, "3. 扫描并连接到 '%s'", bt.GetDeviceName().c_str());
        ESP_LOGI(BT_EXAMPLE_TAG, "4. 查找服务UUID: FFE0");
        ESP_LOGI(BT_EXAMPLE_TAG, "5. 查找特征UUID: FFE1 (可读、可写、可通知)");
        ESP_LOGI(BT_EXAMPLE_TAG, "");
        ESP_LOGI(BT_EXAMPLE_TAG, "💬 数据通信:");
        ESP_LOGI(BT_EXAMPLE_TAG, "- 向特征FFE1写入数据，设备会自动回复");
        ESP_LOGI(BT_EXAMPLE_TAG, "- 启用通知功能，可以接收设备主动推送的数据");
        ESP_LOGI(BT_EXAMPLE_TAG, "========================================");
    } else {
        ESP_LOGE(BT_EXAMPLE_TAG, "❌ 蓝牙服务初始化失败");
    }
}

#endif // BLUETOOTH_EXAMPLE_H

