#ifndef CLEAR_WIFI_HELPER_H
#define CLEAR_WIFI_HELPER_H

#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <ssid_manager.h>
#include <settings.h>

#define CLEAR_WIFI_TAG "ClearWiFi"

/**
 * @brief 清除所有WiFi配置并重启设备
 * 
 * 这个函数会：
 * 1. 清除所有保存的WiFi凭证
 * 2. 清除WiFi相关的NVS设置
 * 3. 延迟1秒后重启设备
 */
inline void ClearAllWiFiConfig() {
    ESP_LOGI(CLEAR_WIFI_TAG, "========================================");
    ESP_LOGI(CLEAR_WIFI_TAG, "开始清除WiFi配置");
    ESP_LOGI(CLEAR_WIFI_TAG, "========================================");
    
    // 清除所有WiFi凭证
    auto& ssid_manager = SsidManager::GetInstance();
    ssid_manager.Clear();
    ESP_LOGI(CLEAR_WIFI_TAG, "✓ 已清除所有WiFi凭证");
    
    // 清除WiFi相关设置
    {
        Settings settings("wifi", true);
        settings.EraseAll();
        ESP_LOGI(CLEAR_WIFI_TAG, "✓ 已清除WiFi设置");
    }
    
    ESP_LOGI(CLEAR_WIFI_TAG, "========================================");
    ESP_LOGI(CLEAR_WIFI_TAG, "WiFi配置已清除，设备将在1秒后重启");
    ESP_LOGI(CLEAR_WIFI_TAG, "========================================");
    
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

/**
 * @brief 通过BLE命令清除WiFi配置
 * 
 * 使用方法：从手机发送以下JSON命令
 * {"cmd":"clear_wifi"}
 * 
 * 响应：
 * {"cmd":"clear_wifi","status":"success","message":"WiFi配置已清除，设备即将重启"}
 */
inline std::string HandleClearWiFiCommand() {
    ESP_LOGI(CLEAR_WIFI_TAG, "收到清除WiFi配置命令");
    
    // 构建响应
    std::string response = R"({"cmd":"clear_wifi","status":"success","message":"WiFi配置已清除，设备即将重启"})";
    
    // 在后台任务中执行清除操作（给时间发送响应）
    xTaskCreate([](void* param) {
        vTaskDelay(pdMS_TO_TICKS(500));  // 延迟500ms，确保响应已发送
        ClearAllWiFiConfig();
        vTaskDelete(NULL);
    }, "clear_wifi_task", 2048, NULL, 5, NULL);
    
    return response;
}

#endif // CLEAR_WIFI_HELPER_H

