#include <esp_log.h>
#include <esp_err.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <esp_event.h>
#include <esp_bt.h>
#include <nimble/nimble_port.h>

#include "application.h"
#include "system_info.h"

#define TAG "main"

extern "C" void app_main(void)
{
    // Initialize the default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize NVS flash for WiFi configuration
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS flash to fix corruption");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // ====== BLE/WiFi 共存：提前初始化 BLE 控制器 ======
    // ⚠️ 重要：BLE 控制器必须在 WiFi 之前初始化！
    // 原因：BLE 控制器需要固定地址范围的内部 SRAM（EMI 内存区域）
    //       WiFi 启动后会占用这部分内存，导致 BLE 初始化失败
    // 策略：先初始化 BLE 控制器占用所需内存，WiFi 使用剩余内存
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "🔧 BLE/WiFi 共存初始化");
    ESP_LOGI(TAG, "========================================");
    
    // 步骤1: 释放 Classic BT 内存（节省 30-40KB）
    ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ 已释放 Classic BT 内存");
    }
    
    // 步骤2: 提前初始化 NimBLE（包括 BLE 控制器）
    // 这会为 BLE 控制器分配所需的内部 SRAM
    ESP_LOGI(TAG, "🔵 初始化 BLE 控制器（WiFi 启动前）...");
    ret = nimble_port_init();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ BLE 控制器初始化成功");
    } else if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGI(TAG, "✓ BLE 控制器已初始化");
    } else {
        ESP_LOGW(TAG, "⚠️ BLE 控制器初始化失败: %s", esp_err_to_name(ret));
        ESP_LOGW(TAG, "   BLE 功能将不可用，但设备可正常工作");
    }
    ESP_LOGI(TAG, "========================================");

    // Launch the application
    auto& app = Application::GetInstance();
    app.Start();
}
