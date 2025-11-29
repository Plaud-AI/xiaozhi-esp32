#include "ble_wifi_provisioner.h"
#include "bluetooth_service.h"
#include "system_info.h"
#include "settings.h"
#include "clear_wifi_helper.h"
#include "wake_word_manager.h"
#include "application.h"

#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <esp_mac.h>
#include <esp_app_desc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cJSON.h>
#include <wifi_station.h>
#include <ssid_manager.h>

#define TAG "BLEWiFiProvisioner"

// 错误码定义（符合文档规范）
#define ERROR_JSON_PARSE_FAILED     1000
#define ERROR_PASSWORD_INCORRECT    1001
#define ERROR_SSID_NOT_FOUND        1002
#define ERROR_CONNECTION_TIMEOUT    1003
#define ERROR_DHCP_FAILED           1004
#define ERROR_WIFI_LIST_FULL        1005
#define ERROR_OUT_OF_MEMORY         2000
#define ERROR_STORAGE_WRITE_FAILED  2001
#define ERROR_UNKNOWN               3000

BLEWiFiProvisioner::BLEWiFiProvisioner() 
    : initialized_(false), is_provisioning_(false) {
    ESP_LOGI(TAG, "BLE WiFi Provisioner 构造");
}

BLEWiFiProvisioner::~BLEWiFiProvisioner() {
    ESP_LOGI(TAG, "BLE WiFi Provisioner 析构");
}

bool BLEWiFiProvisioner::Initialize(const std::string& device_name) {
    if (initialized_) {
        ESP_LOGW(TAG, "BLE WiFi Provisioner 已初始化，跳过");
        return true;
    }

    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "║ 🔧 初始化 BLE WiFi Provisioner");
    ESP_LOGI(TAG, "╠════════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "║ 📱 设备名称: %s", device_name.c_str());
    ESP_LOGI(TAG, "║ 🎯 功能: WiFi配网 + 设备配置");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════");

    // 初始化蓝牙服务
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "步骤 1/3: 初始化蓝牙服务...");
    auto& ble_service = BluetoothService::GetInstance();
    if (!ble_service.Initialize(device_name)) {
        ESP_LOGE(TAG, "❌ 蓝牙服务初始化失败");
        ESP_LOGE(TAG, "可能原因:");
        ESP_LOGE(TAG, "  - 蓝牙控制器初始化失败");
        ESP_LOGE(TAG, "  - 内存不足");
        ESP_LOGE(TAG, "  - NVS 未初始化");
        return false;
    }

    ESP_LOGI(TAG, "✅ 步骤 1/3 完成：蓝牙服务初始化成功");
    ESP_LOGI(TAG, "   设备 MAC 地址: %s", ble_service.GetMacAddress().c_str());
    ESP_LOGI(TAG, "   设备名称: %s", ble_service.GetDeviceName().c_str());

    // 设置数据接收回调
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "步骤 2/3: 设置数据接收回调...");
    ble_service.SetDataReceivedCallback([this](const std::string& data) {
        ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════");
        ESP_LOGI(TAG, "║ 📥 BLE 数据接收事件");
        ESP_LOGI(TAG, "╠════════════════════════════════════════════════════════════");
        ESP_LOGI(TAG, "║ 数据长度: %d 字节", data.length());
        ESP_LOGI(TAG, "║ 数据内容: %s", data.c_str());
        ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════");
        this->HandleReceivedData(data);
    });

    ESP_LOGI(TAG, "✅ 步骤 2/3 完成：数据接收回调设置成功");

    initialized_ = true;
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "✅ 步骤 3/3 完成：BLE WiFi Provisioner 初始化完成");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "║ ✅ 初始化成功摘要");
    ESP_LOGI(TAG, "╠════════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "║ 设备名称: %s", device_name.c_str());
    ESP_LOGI(TAG, "║ MAC 地址: %s", ble_service.GetMacAddress().c_str());
    ESP_LOGI(TAG, "║ 状态: 已初始化，未启动广播");
    ESP_LOGI(TAG, "║ 下一步: 调用 Start() 启动 BLE 广播");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "");

    return true;
}

bool BLEWiFiProvisioner::Start() {
    if (!initialized_) {
        ESP_LOGE(TAG, "");
        ESP_LOGE(TAG, "╔════════════════════════════════════════════════════════════");
        ESP_LOGE(TAG, "║ ❌ 启动失败：BLE WiFi Provisioner 未初始化");
        ESP_LOGE(TAG, "╠════════════════════════════════════════════════════════════");
        ESP_LOGE(TAG, "║ 请先调用 Initialize() 进行初始化");
        ESP_LOGE(TAG, "╚════════════════════════════════════════════════════════════");
        ESP_LOGE(TAG, "");
        return false;
    }

    // 🔧 关键修复：禁用 WiFi 省电模式以防止 BLE/WiFi 共存冲突 (rwble.c 508 assert)
    // 当 WiFi 进入睡眠 (pm_go_to_sleep) 而 BLE 需要射频时，可能会导致控制器崩溃
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) == ESP_OK) {
        ESP_LOGW(TAG, "⚠️  禁用 WiFi 省电模式以保证 BLE 稳定性...");
        esp_err_t err = esp_wifi_set_ps(WIFI_PS_NONE);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "无法禁用 WiFi 省电模式: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "✅ WiFi 省电模式已禁用");
        }
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "║ 🚀 启动 BLE WiFi Provisioner");
    ESP_LOGI(TAG, "╠════════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "║ 准备启动 BLE 广播...");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════");

    auto& ble_service = BluetoothService::GetInstance();
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "🔄 正在启动 BLE 广播...");
    ESP_LOGI(TAG, "   设备名称: %s", ble_service.GetDeviceName().c_str());
    ESP_LOGI(TAG, "   MAC 地址: %s", ble_service.GetMacAddress().c_str());
    
    if (!ble_service.StartAdvertising()) {
        ESP_LOGE(TAG, "");
        ESP_LOGE(TAG, "╔════════════════════════════════════════════════════════════");
        ESP_LOGE(TAG, "║ ❌ BLE 广播启动失败");
        ESP_LOGE(TAG, "╠════════════════════════════════════════════════════════════");
        ESP_LOGE(TAG, "║ 可能原因:");
        ESP_LOGE(TAG, "║   1. 蓝牙协议栈未就绪");
        ESP_LOGE(TAG, "║   2. 广播参数配置错误");
        ESP_LOGE(TAG, "║   3. 蓝牙资源已被占用");
        ESP_LOGE(TAG, "╚════════════════════════════════════════════════════════════");
        ESP_LOGE(TAG, "");
        return false;
    }

    is_provisioning_ = true;
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "║ ✅ BLE 广播已成功启动！");
    ESP_LOGI(TAG, "╠════════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "║ 📱 手机端操作指南:");
    ESP_LOGI(TAG, "║ ─────────────────────────────────────────────────────────");
    ESP_LOGI(TAG, "║ 1️⃣  打开手机蓝牙");
    ESP_LOGI(TAG, "║ 2️⃣  扫描 BLE 设备（不是 WiFi！）");
    ESP_LOGI(TAG, "║ 3️⃣  查找设备: %s", ble_service.GetDeviceName().c_str());
    ESP_LOGI(TAG, "║ 4️⃣  点击连接");
    ESP_LOGI(TAG, "║ ─────────────────────────────────────────────────────────");
    ESP_LOGI(TAG, "║ 设备信息:");
    ESP_LOGI(TAG, "║   • 设备名称: %s", ble_service.GetDeviceName().c_str());
    ESP_LOGI(TAG, "║   • MAC 地址: %s", ble_service.GetMacAddress().c_str());
    ESP_LOGI(TAG, "║   • 服务 UUID: 0000FFE0-...");
    ESP_LOGI(TAG, "║   • 特征 UUID: 0000FFE1-...");
    ESP_LOGI(TAG, "║ ─────────────────────────────────────────────────────────");
    ESP_LOGI(TAG, "║ ⏳ 等待手机连接中...");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "");
    
    return true;
}

void BLEWiFiProvisioner::Stop() {
    ESP_LOGI(TAG, "停止 BLE WiFi Provisioner");
    
    auto& ble_service = BluetoothService::GetInstance();
    ble_service.StopAdvertising();
    
    // 恢复 WiFi 省电模式
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) == ESP_OK) {
        ESP_LOGI(TAG, "恢复 WiFi 省电模式 (MIN_MODEM)...");
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    }

    is_provisioning_ = false;
    ESP_LOGI(TAG, "✓ BLE WiFi Provisioner 已停止");
}

void BLEWiFiProvisioner::HandleReceivedData(const std::string& data) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "║ 📨 处理接收到的 BLE 数据");
    ESP_LOGI(TAG, "╠════════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "║ 数据长度: %d 字节", data.length());
    ESP_LOGI(TAG, "║ 原始数据: %s", data.c_str());
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════");

    // 解析JSON
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "🔍 步骤 1: 解析 JSON...");
    cJSON* root = cJSON_Parse(data.c_str());
    if (!root) {
        ESP_LOGE(TAG, "");
        ESP_LOGE(TAG, "╔════════════════════════════════════════════════════════════");
        ESP_LOGE(TAG, "║ ❌ JSON 解析失败");
        ESP_LOGE(TAG, "╠════════════════════════════════════════════════════════════");
        ESP_LOGE(TAG, "║ 原始数据: %s", data.c_str());
        const char* error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            ESP_LOGE(TAG, "║ 错误位置: %s", error_ptr);
        }
        ESP_LOGE(TAG, "║");
        ESP_LOGE(TAG, "║ 可能原因:");
        ESP_LOGE(TAG, "║   1. JSON 格式不正确");
        ESP_LOGE(TAG, "║   2. 数据传输不完整");
        ESP_LOGE(TAG, "║   3. 编码问题");
        ESP_LOGE(TAG, "╚════════════════════════════════════════════════════════════");
        ESP_LOGE(TAG, "");
        SendErrorResponse("unknown", ERROR_JSON_PARSE_FAILED, "JSON解析失败");
        return;
    }
    ESP_LOGI(TAG, "✅ JSON 解析成功");

    // 获取命令
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "🔍 步骤 2: 提取命令字段...");
    cJSON* cmd_item = cJSON_GetObjectItem(root, "cmd");
    if (!cmd_item || !cJSON_IsString(cmd_item)) {
        ESP_LOGE(TAG, "");
        ESP_LOGE(TAG, "╔════════════════════════════════════════════════════════════");
        ESP_LOGE(TAG, "║ ❌ 命令字段缺失或格式错误");
        ESP_LOGE(TAG, "╠════════════════════════════════════════════════════════════");
        ESP_LOGE(TAG, "║ 期望: {\"cmd\": \"命令名称\", ...}");
        ESP_LOGE(TAG, "║ 实际: %s", data.c_str());
        ESP_LOGE(TAG, "╚════════════════════════════════════════════════════════════");
        ESP_LOGE(TAG, "");
        cJSON_Delete(root);
        SendErrorResponse("unknown", ERROR_JSON_PARSE_FAILED, "命令字段缺失");
        return;
    }

    std::string cmd = cmd_item->valuestring;
    ESP_LOGI(TAG, "✅ 命令提取成功: %s", cmd.c_str());
    ESP_LOGI(TAG, "");

    // 根据命令类型处理
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "║ 🎯 分发命令处理");
    ESP_LOGI(TAG, "╠════════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "║ 命令类型: %s", cmd.c_str());
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "");
    
    if (cmd == "scan_wifi") {
        ESP_LOGI(TAG, "🔍 执行命令: WiFi 扫描");
        ESP_LOGI(TAG, "─────────────────────────────────────────────────────────");
        HandleScanWiFiCommand();
    } 
    else if (cmd == "wifi_config") {
        ESP_LOGI(TAG, "📡 执行命令: WiFi 配置");
        ESP_LOGI(TAG, "─────────────────────────────────────────────────────────");
        
        cJSON* data_item = cJSON_GetObjectItem(root, "data");
        if (!data_item || !cJSON_IsObject(data_item)) {
            ESP_LOGE(TAG, "❌ data字段缺失或格式错误");
            cJSON_Delete(root);
            SendErrorResponse(cmd, ERROR_JSON_PARSE_FAILED, "data字段缺失");
            return;
        }

        cJSON* ssid_item = cJSON_GetObjectItem(data_item, "ssid");
        cJSON* password_item = cJSON_GetObjectItem(data_item, "password");
        cJSON* bssid_item = cJSON_GetObjectItem(data_item, "bssid");

        // SSID 是必需的
        if (!ssid_item || !cJSON_IsString(ssid_item)) {
            ESP_LOGE(TAG, "❌ ssid字段缺失或格式错误");
            cJSON_Delete(root);
            SendErrorResponse(cmd, ERROR_JSON_PARSE_FAILED, "ssid字段缺失");
            return;
        }

        std::string ssid = ssid_item->valuestring;
        std::string password = "";
        std::string bssid = "";
        
        // Password 是可选的（已保存的 WiFi 可以不提供密码）
        if (password_item && cJSON_IsString(password_item)) {
            password = password_item->valuestring;
        }
        
        // 如果没有提供密码，尝试从已保存的配置中获取
        if (password.empty()) {
            ESP_LOGI(TAG, "📋 未提供密码，尝试从已保存配置中查找...");
            auto& ssid_manager = SsidManager::GetInstance();
            auto ssid_list = ssid_manager.GetSsidList();
            
            bool found = false;
            for (const auto& saved_item : ssid_list) {
                if (saved_item.ssid == ssid) {
                    password = saved_item.password;
                    if (!password.empty()) {
                        ESP_LOGI(TAG, "✅ 找到已保存的 WiFi 配置");
                        found = true;
                        break;
                    }
                }
            }
            
            if (!found) {
                ESP_LOGE(TAG, "❌ 该 WiFi 未保存，请提供密码");
                cJSON_Delete(root);
                SendErrorResponse(cmd, ERROR_JSON_PARSE_FAILED, "该 WiFi 未保存，请提供密码");
                return;
            }
        }
        
        if (bssid_item && cJSON_IsString(bssid_item)) {
            bssid = bssid_item->valuestring;
        }

        ESP_LOGI(TAG, "WiFi配置参数:");
        ESP_LOGI(TAG, "  SSID: %s", ssid.c_str());
        ESP_LOGI(TAG, "  密码来源: %s", password_item ? "用户提供" : "已保存配置");
        ESP_LOGI(TAG, "  密码长度: %d字符", password.length());
        if (!bssid.empty()) {
            ESP_LOGI(TAG, "  BSSID: %s", bssid.c_str());
        }

        HandleWiFiConfigCommand(ssid, password, bssid);
    } 
    else if (cmd == "get_device_info") {
        ESP_LOGI(TAG, "➜ 执行: 获取设备信息命令");
        HandleGetDeviceInfoCommand();
    } 
    else if (cmd == "get_saved_wifi") {
        ESP_LOGI(TAG, "➜ 执行: 获取已保存WiFi列表命令");
        HandleGetSavedWiFiCommand();
    } 
    else if (cmd == "delete_wifi") {
        ESP_LOGI(TAG, "➜ 执行: 删除WiFi配置命令");
        
        cJSON* data_item = cJSON_GetObjectItem(root, "data");
        if (!data_item || !cJSON_IsObject(data_item)) {
            ESP_LOGE(TAG, "❌ data字段缺失或格式错误");
            cJSON_Delete(root);
            SendErrorResponse(cmd, ERROR_JSON_PARSE_FAILED, "data字段缺失");
            return;
        }

        cJSON* ssid_item = cJSON_GetObjectItem(data_item, "ssid");
        if (!ssid_item || !cJSON_IsString(ssid_item)) {
            ESP_LOGE(TAG, "❌ ssid字段缺失或格式错误");
            cJSON_Delete(root);
            SendErrorResponse(cmd, ERROR_JSON_PARSE_FAILED, "ssid字段缺失");
            return;
        }

        std::string ssid = ssid_item->valuestring;
        ESP_LOGI(TAG, "要删除的WiFi SSID: %s", ssid.c_str());
        
        HandleDeleteWiFiCommand(ssid);
    }
    else if (cmd == "disconnect_wifi") {
        ESP_LOGI(TAG, "➜ 执行: 断开WiFi连接命令");
        HandleDisconnectWiFiCommand();
    }
    else if (cmd == "clear_wifi") {
        ESP_LOGI(TAG, "➜ 执行: 清除所有WiFi配置命令");
        std::string response = HandleClearWiFiCommand();
        SendResponse(response);
    }
    else if (cmd == "set_wake_words") {
        ESP_LOGI(TAG, "➜ 执行: 设置唤醒词命令");
        HandleSetWakeWordsCommand(root);
    }
    else if (cmd == "get_wake_words") {
        ESP_LOGI(TAG, "➜ 执行: 获取唤醒词列表命令");
        HandleGetWakeWordsCommand();
    }
    else if (cmd == "delete_wake_word") {
        ESP_LOGI(TAG, "➜ 执行: 删除唤醒词命令");
        HandleDeleteWakeWordCommand(root);
    }
    else if (cmd == "reset_wake_words") {
        ESP_LOGI(TAG, "➜ 执行: 重置唤醒词命令");
        HandleResetWakeWordsCommand();
    }
    else if (cmd == "set_ota_url") {
        ESP_LOGI(TAG, "➜ 执行: 设置 OTA URL 命令");
        HandleSetOtaUrlCommand(root);
    }
    else if (cmd == "get_ota_url") {
        ESP_LOGI(TAG, "➜ 执行: 获取 OTA URL 命令");
        HandleGetOtaUrlCommand();
    }
    else {
        ESP_LOGW(TAG, "⚠️  未知命令: %s", cmd.c_str());
        SendErrorResponse(cmd, ERROR_JSON_PARSE_FAILED, "未知命令");
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "========================================");
}

void BLEWiFiProvisioner::HandleScanWiFiCommand() {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "开始WiFi扫描");
    ESP_LOGI(TAG, "========================================");

    // 检查WiFi是否已经运行
    wifi_mode_t mode;
    esp_err_t ret = esp_wifi_get_mode(&mode);
    
    bool wifi_already_running = (ret == ESP_OK && mode != WIFI_MODE_NULL);
    bool need_new_scan = true;
    
    if (wifi_already_running) {
        ESP_LOGI(TAG, "✓ WiFi已在运行，检查是否有缓存的扫描结果...");
        
        // 优先检查是否有缓存的扫描结果（来自Soft AP的周期性扫描）
        uint16_t cached_ap_count = 0;
        ret = esp_wifi_scan_get_ap_num(&cached_ap_count);
        
        if (ret == ESP_OK && cached_ap_count > 0) {
            ESP_LOGI(TAG, "✅ 发现缓存的WiFi扫描结果（%d 个网络），直接使用", cached_ap_count);
            ESP_LOGI(TAG, "✅ 这些结果来自Soft AP的周期性扫描（每30秒刷新）");
            need_new_scan = false;  // 使用缓存，不需要新扫描
        } else {
            ESP_LOGW(TAG, "⚠️  缓存中没有扫描结果，将启动新扫描（仅扫描当前信道，结果可能不完整）");
            need_new_scan = true;
            
            // 记录缓存为空的原因
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "获取缓存扫描结果失败: %s", esp_err_to_name(ret));
            } else {
                ESP_LOGW(TAG, "缓存为空的可能原因：周期性扫描尚未完成或缓存已过期");
            }
        }
    } else {
        // 初始化WiFi
        ret = esp_wifi_set_mode(WIFI_MODE_STA);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "❌ 设置WiFi STA模式失败: %s", esp_err_to_name(ret));
            SendErrorResponse("scan_wifi", ERROR_UNKNOWN, "WiFi初始化失败");
            return;
        }

        ret = esp_wifi_start();
        if (ret != ESP_OK && ret != ESP_ERR_WIFI_STATE) {
            ESP_LOGE(TAG, "❌ 启动WiFi失败: %s", esp_err_to_name(ret));
            SendErrorResponse("scan_wifi", ERROR_UNKNOWN, "WiFi启动失败");
            return;
        }
        ESP_LOGI(TAG, "✓ WiFi已启动");
    }

    if (need_new_scan) {
        ESP_LOGW(TAG, "⚠️  开始新的WiFi扫描（BLE+WiFi共存模式下，扫描范围受限）...");
        
        // 配置扫描参数
        // 注意：在BLE+WiFi共存模式下，WiFi扫描会受到限制
        // 通常只能扫描当前信道的网络，导致结果不完整
        wifi_scan_config_t scan_config = {
            .ssid = nullptr,
            .bssid = nullptr,
            .channel = 0,  // 尝试扫描所有信道（共存模式下可能被限制）
            .show_hidden = false,
            .scan_type = WIFI_SCAN_TYPE_ACTIVE,
            .scan_time = {
                .active = {
                    .min = 0,  // 使用默认值，适配BLE+WiFi共存
                    .max = 0   // 使用默认值，适配BLE+WiFi共存
                }
            }
        };

        // 启动扫描（非阻塞）
        ret = esp_wifi_scan_start(&scan_config, false);
        if (ret != ESP_OK && ret != ESP_ERR_WIFI_STATE) {
            ESP_LOGE(TAG, "❌ WiFi扫描启动失败: %s", esp_err_to_name(ret));
            SendErrorResponse("scan_wifi", ERROR_UNKNOWN, "WiFi扫描失败");
            return;
        }
        
        // 等待扫描完成（最多等待10秒）
        for (int i = 0; i < 100; i++) {
            uint16_t ap_count = 0;
            ret = esp_wifi_scan_get_ap_num(&ap_count);
            if (ret == ESP_OK && ap_count > 0) {
                ESP_LOGW(TAG, "⚠️  BLE模式扫描完成（仅扫描到 %d 个网络，可能不完整）", ap_count);
                ESP_LOGW(TAG, "💡 建议：使用Soft AP Web配网可获取完整列表");
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    ESP_LOGI(TAG, "✓ 正在获取扫描结果...");

    // 发送扫描结果
    std::string response = BuildScanResultJson();
    
    // 检查响应大小，大数据包可能导致BLE发送失败
    if (response.length() > 3000) {
        ESP_LOGW(TAG, "⚠️  响应数据较大（%d 字节），BLE分包发送可能不稳定", response.length());
    }
    
    bool send_success = SendResponse(response);
    
    if (send_success) {
        ESP_LOGI(TAG, "✅ WiFi扫描结果已成功发送");
    } else {
        ESP_LOGE(TAG, "❌ WiFi扫描结果发送失败（可能是BLE连接不稳定或数据包过大）");
    }
    ESP_LOGI(TAG, "========================================");
}

void BLEWiFiProvisioner::HandleWiFiConfigCommand(const std::string& ssid, 
                                                  const std::string& password,
                                                  const std::string& bssid) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "║ 📡 WiFi 配置流程开始");
    ESP_LOGI(TAG, "╠════════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "║ 目标 SSID: %s", ssid.c_str());
    ESP_LOGI(TAG, "║ 密码长度: %d 字符", password.length());
    if (!bssid.empty()) {
        ESP_LOGI(TAG, "║ BSSID: %s", bssid.c_str());
    }
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "");

    // 验证密码长度
    ESP_LOGI(TAG, "🔍 步骤 1: 验证参数...");
    if (!password.empty() && (password.length() < 8 || password.length() > 63)) {
        ESP_LOGE(TAG, "");
        ESP_LOGE(TAG, "╔════════════════════════════════════════════════════════════");
        ESP_LOGE(TAG, "║ ❌ 密码长度验证失败");
        ESP_LOGE(TAG, "╠════════════════════════════════════════════════════════════");
        ESP_LOGE(TAG, "║ 要求: 8-63 字符");
        ESP_LOGE(TAG, "║ 实际: %d 字符", password.length());
        ESP_LOGE(TAG, "╚════════════════════════════════════════════════════════════");
        ESP_LOGE(TAG, "");
        SendErrorResponse("wifi_config", ERROR_PASSWORD_INCORRECT, 
                         "密码长度必须为8-63字符");
        return;
    }
    ESP_LOGI(TAG, "✅ 参数验证通过");

    // 检查是否已连接到WiFi
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "🔍 步骤 2: 检查当前 WiFi 连接状态...");
    auto& wifi_station = WifiStation::GetInstance();
    if (wifi_station.IsConnected()) {
        std::string current_ssid = wifi_station.GetSsid();
        ESP_LOGI(TAG, "ℹ️  当前已连接到: %s", current_ssid.c_str());
        
        if (current_ssid == ssid) {
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════");
            ESP_LOGI(TAG, "║ ℹ️  已连接到目标 WiFi");
            ESP_LOGI(TAG, "╠════════════════════════════════════════════════════════════");
            ESP_LOGI(TAG, "║ SSID: %s", ssid.c_str());
            ESP_LOGI(TAG, "║ IP: %s", wifi_station.GetIpAddress().c_str());
            ESP_LOGI(TAG, "║ RSSI: %d dBm", wifi_station.GetRssi());
            ESP_LOGI(TAG, "║");
            ESP_LOGI(TAG, "║ 无需重新连接，直接返回成功");
            ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════");
            ESP_LOGI(TAG, "");
            
            // 构建响应（已连接）
            cJSON* root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "cmd", "wifi_config");
            cJSON_AddStringToObject(root, "status", "success");
            cJSON_AddStringToObject(root, "message", "已经连接到该WiFi");
            
            cJSON* data = cJSON_CreateObject();
            cJSON_AddStringToObject(data, "ssid", wifi_station.GetSsid().c_str());
            cJSON_AddStringToObject(data, "ip", wifi_station.GetIpAddress().c_str());
            cJSON_AddNumberToObject(data, "rssi", wifi_station.GetRssi());
            cJSON_AddItemToObject(root, "data", data);

            char* json_str = cJSON_PrintUnformatted(root);
            if (json_str) {
                SendResponse(std::string(json_str));
                free(json_str);
            }
            cJSON_Delete(root);
            return;
        } else {
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════");
            ESP_LOGI(TAG, "║ ⚠️  需要切换到新 WiFi");
            ESP_LOGI(TAG, "╠════════════════════════════════════════════════════════════");
            ESP_LOGI(TAG, "║ 当前 WiFi: %s", current_ssid.c_str());
            ESP_LOGI(TAG, "║ 目标 WiFi: %s", ssid.c_str());
            ESP_LOGI(TAG, "║");
            ESP_LOGI(TAG, "║ 🔄 正在断开当前连接...");
            ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════");
            ESP_LOGI(TAG, "");
        }
    } else {
        ESP_LOGI(TAG, "ℹ️  当前未连接任何 WiFi");
    }

    // 保存WiFi凭证
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "🔍 步骤 3: 保存 WiFi 凭证到 NVS...");
    auto& ssid_manager = SsidManager::GetInstance();
    
    // 添加WiFi凭证
    ssid_manager.AddSsid(ssid, password);
    ESP_LOGI(TAG, "✅ WiFi 凭证已保存到 NVS");
    ESP_LOGI(TAG, "   SSID: %s", ssid.c_str());

    // 尝试连接WiFi
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "║ 🔍 步骤 4: 尝试连接到 WiFi");
    ESP_LOGI(TAG, "╠════════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "║ 目标 SSID: %s", ssid.c_str());
    ESP_LOGI(TAG, "║ 超时时间: 30 秒");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "");
    
    // 检查当前 WiFi 模式
    wifi_mode_t current_mode = WIFI_MODE_NULL;
    esp_err_t mode_err = esp_wifi_get_mode(&current_mode);
    bool is_ap_mode = (mode_err == ESP_OK && 
                       (current_mode == WIFI_MODE_AP || current_mode == WIFI_MODE_APSTA));
    
    ESP_LOGI(TAG, "📡 当前 WiFi 模式: %d", current_mode);
    ESP_LOGI(TAG, "");
    
    // ⚠️ 重要优化：BLE 配网时直接连接，不扫描！
    // 用户已经在手机上选择了 WiFi，无需再扫描匹配
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "║ 🎯 BLE 配网优化：直接连接模式");
    ESP_LOGI(TAG, "╠════════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "║ • 用户已指定 WiFi，无需扫描");
    ESP_LOGI(TAG, "║ • 避免与 WiFiPeriodicScan 任务冲突");
    ESP_LOGI(TAG, "║ • 连接速度更快，成功率更高");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "");
    
    // 确保 WiFi Station 已初始化
    if (!is_ap_mode) {
        ESP_LOGI(TAG, "🔄 停止当前 WiFi Station...");
        wifi_station.Stop();
        vTaskDelay(pdMS_TO_TICKS(500));
        
        ESP_LOGI(TAG, "🚀 启动 WiFi Station...");
        wifi_station.Start();
        vTaskDelay(pdMS_TO_TICKS(500));
    } else {
        ESP_LOGI(TAG, "ℹ️  WiFi 已在 APSTA 模式运行");
    }
    
    // 直接连接到指定的 WiFi（不扫描）
    ESP_LOGI(TAG, "");
    bool connected = wifi_station.ConnectDirectly(ssid, password, 30 * 1000);
    
    if (connected) {
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════");
        ESP_LOGI(TAG, "║ ✅✅✅ WiFi 连接成功！");
        ESP_LOGI(TAG, "╠════════════════════════════════════════════════════════════");
        ESP_LOGI(TAG, "║ 连接信息:");
        ESP_LOGI(TAG, "║ ─────────────────────────────────────────────────────────");
        ESP_LOGI(TAG, "║ • SSID: %s", wifi_station.GetSsid().c_str());
        ESP_LOGI(TAG, "║ • IP 地址: %s", wifi_station.GetIpAddress().c_str());
        ESP_LOGI(TAG, "║ • RSSI: %d dBm", wifi_station.GetRssi());
        ESP_LOGI(TAG, "║ • 信道: %d", wifi_station.GetChannel());
        ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════");
        ESP_LOGI(TAG, "");

        // 构建成功响应
        cJSON* root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "cmd", "wifi_config");
        cJSON_AddStringToObject(root, "status", "success");
        cJSON_AddStringToObject(root, "message", "WiFi配置成功，设备即将重启");
        
        cJSON* data = cJSON_CreateObject();
        cJSON_AddStringToObject(data, "ssid", wifi_station.GetSsid().c_str());
        cJSON_AddStringToObject(data, "ip", wifi_station.GetIpAddress().c_str());
        cJSON_AddNumberToObject(data, "rssi", wifi_station.GetRssi());
        cJSON_AddItemToObject(root, "data", data);

        char* json_str = cJSON_PrintUnformatted(root);
        if (json_str) {
            SendResponse(std::string(json_str));
            free(json_str);
        }
        cJSON_Delete(root);

        // 调用成功回调
        ESP_LOGI(TAG, "🔔 调用配网成功回调...");
        if (provision_success_callback_) {
            provision_success_callback_(ssid, password);
            ESP_LOGI(TAG, "✅ 配网成功回调已执行");
        } else {
            ESP_LOGI(TAG, "ℹ️  未设置配网成功回调");
        }

        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════");
        ESP_LOGI(TAG, "║ 📝 注意事项");
        ESP_LOGI(TAG, "╠════════════════════════════════════════════════════════════");
        ESP_LOGI(TAG, "║ • 配网模式：设备将重启以应用新配置");
        ESP_LOGI(TAG, "║ • 正常模式：配置已保存，重启后生效");
        ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════");
        ESP_LOGI(TAG, "");
        
    } else {
        ESP_LOGE(TAG, "");
        ESP_LOGE(TAG, "╔════════════════════════════════════════════════════════════");
        ESP_LOGE(TAG, "║ ❌❌❌ WiFi 连接失败");
        ESP_LOGE(TAG, "╠════════════════════════════════════════════════════════════");
        ESP_LOGE(TAG, "║ 目标 SSID: %s", ssid.c_str());
        ESP_LOGE(TAG, "║");
        ESP_LOGE(TAG, "║ 可能的原因:");
        ESP_LOGE(TAG, "║ ─────────────────────────────────────────────────────────");
        ESP_LOGE(TAG, "║ 1️⃣  密码错误");
        ESP_LOGE(TAG, "║     • 检查密码是否正确");
        ESP_LOGE(TAG, "║     • 注意区分大小写");
        ESP_LOGE(TAG, "║");
        ESP_LOGE(TAG, "║ 2️⃣  WiFi 信号太弱");
        ESP_LOGE(TAG, "║     • 将设备靠近路由器");
        ESP_LOGE(TAG, "║     • 检查路由器是否工作正常");
        ESP_LOGE(TAG, "║");
        ESP_LOGE(TAG, "║ 3️⃣  SSID 不存在或隐藏");
        ESP_LOGE(TAG, "║     • 确认 WiFi 名称正确");
        ESP_LOGE(TAG, "║     • 确认路由器已开启");
        ESP_LOGE(TAG, "║");
        ESP_LOGE(TAG, "║ 4️⃣  路由器拒绝连接");
        ESP_LOGE(TAG, "║     • 检查路由器 MAC 地址过滤");
        ESP_LOGE(TAG, "║     • 检查路由器设备连接数限制");
        ESP_LOGE(TAG, "╚════════════════════════════════════════════════════════════");
        ESP_LOGE(TAG, "");

        // 删除刚保存的凭证
        ESP_LOGW(TAG, "🗑️  清理操作：删除无效的 WiFi 凭证...");
        auto ssid_list = ssid_manager.GetSsidList();
        bool removed = false;
        for (size_t i = 0; i < ssid_list.size(); i++) {
            if (ssid_list[i].ssid == ssid) {
                ssid_manager.RemoveSsid(i);
                ESP_LOGW(TAG, "✅ 已删除无效的 WiFi 凭证: %s", ssid.c_str());
                removed = true;
                break;
            }
        }
        if (!removed) {
            ESP_LOGW(TAG, "⚠️  未找到需要删除的凭证");
        }

        ESP_LOGE(TAG, "");
        ESP_LOGE(TAG, "📤 发送错误响应到手机...");
        SendErrorResponse("wifi_config", ERROR_CONNECTION_TIMEOUT, 
                         "WiFi连接超时，请检查密码和信号强度");

        // 调用失败回调
        ESP_LOGE(TAG, "");
        ESP_LOGE(TAG, "🔔 调用配网失败回调...");
        if (provision_failure_callback_) {
            provision_failure_callback_("连接超时");
            ESP_LOGE(TAG, "✅ 配网失败回调已执行");
        } else {
            ESP_LOGE(TAG, "ℹ️  未设置配网失败回调");
        }
        
        ESP_LOGE(TAG, "");
        ESP_LOGE(TAG, "╔════════════════════════════════════════════════════════════");
        ESP_LOGE(TAG, "║ WiFi 配置流程结束（失败）");
        ESP_LOGE(TAG, "╚════════════════════════════════════════════════════════════");
        ESP_LOGE(TAG, "");
    }
}

void BLEWiFiProvisioner::HandleGetDeviceInfoCommand() {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "获取设备信息");
    ESP_LOGI(TAG, "========================================");

    std::string response = BuildDeviceInfoJson();
    SendResponse(response);

    ESP_LOGI(TAG, "✓ 设备信息已发送");
    ESP_LOGI(TAG, "========================================");
}

void BLEWiFiProvisioner::HandleGetSavedWiFiCommand() {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "获取已保存WiFi列表");
    ESP_LOGI(TAG, "========================================");

    std::string response = BuildSavedWiFiListJson();
    SendResponse(response);

    ESP_LOGI(TAG, "✓ 已保存WiFi列表已发送");
    ESP_LOGI(TAG, "========================================");
}

void BLEWiFiProvisioner::HandleDisconnectWiFiCommand() {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "║ 🔌 断开 WiFi 连接并删除配置");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "");

    auto& wifi_station = WifiStation::GetInstance();
    auto& ssid_manager = SsidManager::GetInstance();
    
    // 检查是否已连接
    ESP_LOGI(TAG, "🔍 步骤 1: 检查当前连接状态...");
    if (!wifi_station.IsConnected()) {
        ESP_LOGW(TAG, "");
        ESP_LOGW(TAG, "╔════════════════════════════════════════════════════════════");
        ESP_LOGW(TAG, "║ ⚠️  当前未连接到任何 WiFi");
        ESP_LOGW(TAG, "╠════════════════════════════════════════════════════════════");
        ESP_LOGW(TAG, "║ 无需断开连接");
        ESP_LOGW(TAG, "║ WiFi 配置仍然保留");
        ESP_LOGW(TAG, "╚════════════════════════════════════════════════════════════");
        ESP_LOGW(TAG, "");
        
        cJSON* root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "cmd", "disconnect_wifi");
        cJSON_AddStringToObject(root, "status", "success");
        cJSON_AddStringToObject(root, "message", "当前未连接WiFi");
        
        char* json_str = cJSON_PrintUnformatted(root);
        if (json_str) {
            SendResponse(std::string(json_str));
            free(json_str);
        }
        cJSON_Delete(root);
        
        return;
    }
    
    // ⚠️ 关键：在 Stop() 之前保存所有需要的信息！
    std::string current_ssid = wifi_station.GetSsid();
    ESP_LOGI(TAG, "✅ 当前连接的 WiFi: %s", current_ssid.c_str());
    
    // 步骤 2：断开 WiFi 连接
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "🔍 步骤 2: 断开 WiFi 连接...");
    ESP_LOGI(TAG, "   目标 SSID: %s", current_ssid.c_str());
    
    // 停止WiFi Station
    wifi_station.Stop();
    
    // 等待断开完成
    vTaskDelay(pdMS_TO_TICKS(500));
    
    ESP_LOGI(TAG, "✅ WiFi 连接已断开");
    
    // 步骤 2.5：重新启动 WiFi Station（但不连接）
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "🔍 步骤 2.5: 重新启动 WiFi（保持就绪状态）...");
    ESP_LOGI(TAG, "   目的: 保持 WiFi 驱动就绪，支持后续扫描和配网");
    
    // 重新启动 WiFi Station（但没有保存的配置，所以不会自动连接）
    wifi_station.Start();
    
    // 等待启动完成
    vTaskDelay(pdMS_TO_TICKS(500));
    
    ESP_LOGI(TAG, "✅ WiFi 已重新启动（未连接状态）");
    ESP_LOGI(TAG, "   • WiFi 驱动: 就绪");
    ESP_LOGI(TAG, "   • 连接状态: 未连接");
    ESP_LOGI(TAG, "   • 可用功能: scan_wifi, wifi_config");
    
    // 步骤 3：删除该 WiFi 的保存配置
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "🔍 步骤 3: 删除 WiFi 配置...");
    ESP_LOGI(TAG, "   目标 SSID: %s", current_ssid.c_str());
    
    auto ssid_list = ssid_manager.GetSsidList();
    bool found = false;
    for (size_t i = 0; i < ssid_list.size(); i++) {
        if (ssid_list[i].ssid == current_ssid) {
            ssid_manager.RemoveSsid(i);
            ESP_LOGI(TAG, "✅ WiFi 配置已从 NVS 删除");
            ESP_LOGI(TAG, "   删除的 SSID: %s", current_ssid.c_str());
            found = true;
            break;
        }
    }
    
    if (!found) {
        ESP_LOGW(TAG, "⚠️  未在 NVS 中找到该 WiFi 配置");
        ESP_LOGW(TAG, "   （可能已被手动删除）");
    }
    
    // 步骤 4：发送成功响应
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "🔍 步骤 4: 发送响应到手机...");
    
    // 构建成功响应
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "cmd", "disconnect_wifi");
    cJSON_AddStringToObject(root, "status", "success");
    cJSON_AddStringToObject(root, "message", "WiFi已断开，可继续配置新网络");
    
    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "previous_ssid", current_ssid.c_str());
    cJSON_AddBoolToObject(data, "config_removed", found);
    cJSON_AddBoolToObject(data, "wifi_ready", true);  // WiFi 驱动就绪
    cJSON_AddBoolToObject(data, "need_reboot", false);  // 不需要重启
    cJSON_AddItemToObject(root, "data", data);

    char* json_str = cJSON_PrintUnformatted(root);
    if (json_str) {
        bool send_ok = SendResponse(std::string(json_str));
        if (send_ok) {
            ESP_LOGI(TAG, "✅ 响应发送成功");
        } else {
            ESP_LOGE(TAG, "❌ 响应发送失败");
        }
        free(json_str);
    }
    cJSON_Delete(root);

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "║ ✅ 断开 WiFi 完成");
    ESP_LOGI(TAG, "╠════════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "║ • 连接已断开: %s", current_ssid.c_str());
    ESP_LOGI(TAG, "║ • 配置已删除: %s", found ? "是" : "否");
    ESP_LOGI(TAG, "║ • WiFi 状态: 就绪（未连接）");
    ESP_LOGI(TAG, "║");
    ESP_LOGI(TAG, "║ 📌 说明:");
    ESP_LOGI(TAG, "║   • WiFi 连接已断开");
    ESP_LOGI(TAG, "║   • 保存的配置已删除");
    ESP_LOGI(TAG, "║   • WiFi 驱动保持就绪");
    ESP_LOGI(TAG, "║   • 可以继续扫描和配置新 WiFi");
    ESP_LOGI(TAG, "║   • 无需重启设备");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════");
    ESP_LOGI(TAG, "");
}

void BLEWiFiProvisioner::HandleDeleteWiFiCommand(const std::string& ssid) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "删除WiFi配置");
    ESP_LOGI(TAG, "SSID: %s", ssid.c_str());
    ESP_LOGI(TAG, "========================================");

    auto& ssid_manager = SsidManager::GetInstance();
    auto ssid_list = ssid_manager.GetSsidList();
    
    bool found = false;
    for (size_t i = 0; i < ssid_list.size(); i++) {
        if (ssid_list[i].ssid == ssid) {
            ssid_manager.RemoveSsid(i);
            ESP_LOGI(TAG, "✓ WiFi配置已删除");
            found = true;
            break;
        }
    }
    
    if (!found) {
        ESP_LOGW(TAG, "⚠️  未找到指定的WiFi配置: %s", ssid.c_str());
    }

    // 构建成功响应
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "cmd", "delete_wifi");
    cJSON_AddStringToObject(root, "status", "success");
    cJSON_AddStringToObject(root, "message", "WiFi配置已删除");

    char* json_str = cJSON_PrintUnformatted(root);
    if (json_str) {
        SendResponse(std::string(json_str));
        free(json_str);
    }
    cJSON_Delete(root);

    ESP_LOGI(TAG, "========================================");
}

bool BLEWiFiProvisioner::SendResponse(const std::string& json_response) {
    ESP_LOGD(TAG, "准备发送响应（长度: %d字节）", json_response.length());
    ESP_LOGD(TAG, "响应内容: %s", json_response.c_str());

    auto& ble_service = BluetoothService::GetInstance();
    bool success = ble_service.SendData(json_response);

    if (success) {
        ESP_LOGD(TAG, "✓ 响应发送成功");
    } else {
        ESP_LOGE(TAG, "❌ 响应发送失败");
    }

    return success;
}

void BLEWiFiProvisioner::SendErrorResponse(const std::string& cmd, 
                                            int error_code, 
                                            const std::string& error_message) {
    ESP_LOGE(TAG, "========================================");
    ESP_LOGE(TAG, "发送错误响应");
    ESP_LOGE(TAG, "命令: %s", cmd.c_str());
    ESP_LOGE(TAG, "错误码: %d", error_code);
    ESP_LOGE(TAG, "错误消息: %s", error_message.c_str());
    ESP_LOGE(TAG, "========================================");

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "cmd", cmd.c_str());
    cJSON_AddStringToObject(root, "status", "error");
    cJSON_AddNumberToObject(root, "error_code", error_code);
    cJSON_AddStringToObject(root, "message", error_message.c_str());

    char* json_str = cJSON_PrintUnformatted(root);
    if (json_str) {
        SendResponse(std::string(json_str));
        free(json_str);
    }
    cJSON_Delete(root);
}

std::string BLEWiFiProvisioner::BuildScanResultJson() {
    ESP_LOGI(TAG, "构建WiFi扫描结果JSON");

    // 获取当前连接的WiFi信息
    std::string connected_ssid = "";
    std::string connected_bssid = "";
    auto& wifi_station = WifiStation::GetInstance();
    if (wifi_station.IsConnected()) {
        connected_ssid = wifi_station.GetSsid();
        ESP_LOGI(TAG, "当前已连接到WiFi: %s", connected_ssid.c_str());
    }

    // 获取扫描到的AP数量
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    ESP_LOGI(TAG, "扫描到 %d 个WiFi网络", ap_count);

    // 获取扫描结果
    wifi_ap_record_t* ap_records = nullptr;
    if (ap_count > 0) {
        ap_records = (wifi_ap_record_t*)malloc(sizeof(wifi_ap_record_t) * ap_count);
        if (ap_records) {
            esp_wifi_scan_get_ap_records(&ap_count, ap_records);
        } else {
            ESP_LOGE(TAG, "内存分配失败");
            ap_count = 0;
        }
    }

    // 构建JSON
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "cmd", "scan_wifi");
    cJSON_AddStringToObject(root, "status", "success");

    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "count", ap_count);
    
    // 添加当前连接信息
    if (!connected_ssid.empty()) {
        cJSON_AddStringToObject(data, "connected_ssid", connected_ssid.c_str());
        cJSON_AddNumberToObject(data, "connected_rssi", wifi_station.GetRssi());
        cJSON_AddStringToObject(data, "connected_ip", wifi_station.GetIpAddress().c_str());
    }

    cJSON* networks = cJSON_CreateArray();
    for (int i = 0; i < ap_count; i++) {
        std::string current_ssid((char*)ap_records[i].ssid);
        bool is_connected = (!connected_ssid.empty() && current_ssid == connected_ssid);
        
        ESP_LOGI(TAG, "网络 %d:%s", i + 1, is_connected ? " [已连接]" : "");
        ESP_LOGI(TAG, "  SSID: %s", ap_records[i].ssid);
        ESP_LOGI(TAG, "  RSSI: %d dBm", ap_records[i].rssi);
        ESP_LOGI(TAG, "  信道: %d", ap_records[i].primary);
        ESP_LOGI(TAG, "  加密: %d", ap_records[i].authmode);

        cJSON* network = cJSON_CreateObject();
        cJSON_AddStringToObject(network, "ssid", (char*)ap_records[i].ssid);
        cJSON_AddNumberToObject(network, "rssi", ap_records[i].rssi);
        cJSON_AddNumberToObject(network, "channel", ap_records[i].primary);
        cJSON_AddNumberToObject(network, "auth_mode", GetAuthModeValue(ap_records[i].authmode));
        
        // 标记是否为当前连接的WiFi
        cJSON_AddBoolToObject(network, "connected", is_connected);
        
        char bssid_str[18];
        snprintf(bssid_str, sizeof(bssid_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                ap_records[i].bssid[0], ap_records[i].bssid[1], ap_records[i].bssid[2],
                ap_records[i].bssid[3], ap_records[i].bssid[4], ap_records[i].bssid[5]);
        cJSON_AddStringToObject(network, "bssid", bssid_str);

        cJSON_AddItemToArray(networks, network);
    }

    cJSON_AddItemToObject(data, "networks", networks);
    cJSON_AddItemToObject(root, "data", data);

    char* json_str = cJSON_PrintUnformatted(root);
    std::string result;
    if (json_str) {
        result = json_str;
        free(json_str);
    }
    cJSON_Delete(root);

    if (ap_records) {
        free(ap_records);
    }

    ESP_LOGI(TAG, "✓ JSON构建完成（长度: %d字节）", result.length());
    return result;
}

std::string BLEWiFiProvisioner::BuildDeviceInfoJson() {
    ESP_LOGI(TAG, "构建设备信息JSON");

    auto& ble_service = BluetoothService::GetInstance();
    auto app_desc = esp_app_get_description();

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "cmd", "get_device_info");
    cJSON_AddStringToObject(root, "status", "success");

    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "device_name", ble_service.GetDeviceName().c_str());
    cJSON_AddStringToObject(data, "firmware_version", app_desc->version);
    cJSON_AddStringToObject(data, "hardware_version", SystemInfo::GetChipModelName().c_str());
    cJSON_AddStringToObject(data, "mac_address", ble_service.GetMacAddress().c_str());
    cJSON_AddNumberToObject(data, "free_heap", esp_get_free_heap_size());
    
    uint32_t chip_id = 0;
    esp_efuse_mac_get_default((uint8_t*)&chip_id);
    char chip_id_str[16];
    snprintf(chip_id_str, sizeof(chip_id_str), "0x%08lX", (unsigned long)chip_id);
    cJSON_AddStringToObject(data, "chip_id", chip_id_str);

    cJSON_AddItemToObject(root, "data", data);

    char* json_str = cJSON_PrintUnformatted(root);
    std::string result;
    if (json_str) {
        result = json_str;
        free(json_str);
        
        ESP_LOGI(TAG, "设备信息:");
        ESP_LOGI(TAG, "  名称: %s", ble_service.GetDeviceName().c_str());
        ESP_LOGI(TAG, "  固件版本: %s", app_desc->version);
        ESP_LOGI(TAG, "  硬件版本: %s", SystemInfo::GetChipModelName().c_str());
        ESP_LOGI(TAG, "  MAC地址: %s", ble_service.GetMacAddress().c_str());
        ESP_LOGI(TAG, "  空闲堆: %lu bytes", (unsigned long)esp_get_free_heap_size());
    }
    cJSON_Delete(root);

    ESP_LOGI(TAG, "✓ 设备信息JSON构建完成");
    return result;
}

std::string BLEWiFiProvisioner::BuildSavedWiFiListJson() {
    ESP_LOGI(TAG, "构建已保存WiFi列表JSON");

    auto& ssid_manager = SsidManager::GetInstance();
    auto ssid_list = ssid_manager.GetSsidList();

    ESP_LOGI(TAG, "已保存 %d 个WiFi配置", ssid_list.size());

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "cmd", "get_saved_wifi");
    cJSON_AddStringToObject(root, "status", "success");

    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "count", ssid_list.size());

    cJSON* networks = cJSON_CreateArray();
    for (size_t i = 0; i < ssid_list.size(); i++) {
        ESP_LOGI(TAG, "已保存WiFi %d: %s", i + 1, ssid_list[i].ssid.c_str());

        cJSON* network = cJSON_CreateObject();
        cJSON_AddStringToObject(network, "ssid", ssid_list[i].ssid.c_str());
        cJSON_AddBoolToObject(network, "is_default", i == 0);  // 第一个为默认
        cJSON_AddStringToObject(network, "last_connected", "未知");  // 可以后续扩展
        cJSON_AddItemToArray(networks, network);
    }

    cJSON_AddItemToObject(data, "networks", networks);
    cJSON_AddItemToObject(root, "data", data);

    char* json_str = cJSON_PrintUnformatted(root);
    std::string result;
    if (json_str) {
        result = json_str;
        free(json_str);
    }
    cJSON_Delete(root);

    ESP_LOGI(TAG, "✓ 已保存WiFi列表JSON构建完成");
    return result;
}

int BLEWiFiProvisioner::GetAuthModeValue(int esp_auth_mode) {
    // 将ESP32的认证模式转换为文档规范中的值
    switch (esp_auth_mode) {
        case WIFI_AUTH_OPEN:
            return 0;  // OPEN
        case WIFI_AUTH_WEP:
            return 1;  // WEP
        case WIFI_AUTH_WPA_PSK:
            return 2;  // WPA_PSK
        case WIFI_AUTH_WPA2_PSK:
            return 3;  // WPA2_PSK
        case WIFI_AUTH_WPA_WPA2_PSK:
            return 4;  // WPA_WPA2_PSK
        case WIFI_AUTH_WPA2_ENTERPRISE:
            return 5;  // WPA2_ENTERPRISE
        case WIFI_AUTH_WPA3_PSK:
            return 6;  // WPA3_PSK
        default:
            return 0;  // 未知，默认为OPEN
    }
}

void BLEWiFiProvisioner::SetProvisionSuccessCallback(
    std::function<void(const std::string&, const std::string&)> callback) {
    provision_success_callback_ = callback;
    ESP_LOGI(TAG, "✓ 配网成功回调已设置");
}

void BLEWiFiProvisioner::SetProvisionFailureCallback(
    std::function<void(const std::string&)> callback) {
    provision_failure_callback_ = callback;
    ESP_LOGI(TAG, "✓ 配网失败回调已设置");
}

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// 唤醒词管理命令处理
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

void BLEWiFiProvisioner::HandleSetWakeWordsCommand(cJSON* root) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "🎯 开始设置唤醒词");
    ESP_LOGI(TAG, "========================================");
    
    // 打印原始JSON（用于调试）
    char* json_str = cJSON_PrintUnformatted(root);
    if (json_str) {
        ESP_LOGI(TAG, "收到的完整JSON: %s", json_str);
        free(json_str);
    }
    
    cJSON* data_item = cJSON_GetObjectItem(root, "data");
    if (!data_item || !cJSON_IsObject(data_item)) {
        ESP_LOGE(TAG, "❌ data 字段缺失或格式错误");
        SendErrorResponse("set_wake_words", ERROR_JSON_PARSE_FAILED, "data字段缺失");
        return;
    }
    ESP_LOGI(TAG, "✓ 找到 data 字段");
    
    // 解析唤醒词列表
    cJSON* words_array = cJSON_GetObjectItem(data_item, "words");
    if (!words_array || !cJSON_IsArray(words_array)) {
        ESP_LOGE(TAG, "❌ words 字段缺失或格式错误");
        SendErrorResponse("set_wake_words", ERROR_JSON_PARSE_FAILED, "words字段缺失");
        return;
    }
    int words_count = cJSON_GetArraySize(words_array);
    ESP_LOGI(TAG, "✓ 找到 words 数组，包含 %d 个唤醒词", words_count);
    
    // 读取阈值（可选，默认使用 DEFAULT_WAKE_WORD_THRESHOLD）
    float threshold = DEFAULT_WAKE_WORD_THRESHOLD;
    cJSON* threshold_item = cJSON_GetObjectItem(data_item, "threshold");
    if (threshold_item && cJSON_IsNumber(threshold_item)) {
        threshold = threshold_item->valuedouble;
        ESP_LOGI(TAG, "✓ 使用自定义阈值: %.3f", threshold);
    } else {
        ESP_LOGI(TAG, "ℹ️  使用默认阈值: %.3f", threshold);
    }
    
    // 读取替换标志（可选，默认 true）
    bool replace = true;
    cJSON* replace_item = cJSON_GetObjectItem(data_item, "replace");
    if (replace_item && cJSON_IsBool(replace_item)) {
        replace = cJSON_IsTrue(replace_item);
    }
    ESP_LOGI(TAG, "ℹ️  替换模式: %s", replace ? "是（清空现有唤醒词）" : "否（追加到现有唤醒词）");
    
    // 解析每个唤醒词
    ESP_LOGI(TAG, "----------------------------------------");
    ESP_LOGI(TAG, "开始解析唤醒词列表...");
    std::vector<WakeWordConfig> wake_words;
    cJSON* word_item = nullptr;
    int word_index = 0;
    cJSON_ArrayForEach(word_item, words_array) {
        word_index++;
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "📝 解析第 %d 个唤醒词...", word_index);
        
        if (!cJSON_IsObject(word_item)) {
            ESP_LOGW(TAG, "⚠️  跳过：非对象类型");
            continue;
        }
        
        cJSON* text = cJSON_GetObjectItem(word_item, "text");
        cJSON* display = cJSON_GetObjectItem(word_item, "display");
        cJSON* phonemes = cJSON_GetObjectItem(word_item, "phonemes");
        
        if (!text || !cJSON_IsString(text)) {
            ESP_LOGW(TAG, "⚠️  跳过：缺少 text 字段");
            continue;
        }
        ESP_LOGI(TAG, "   text: %s", text->valuestring);
        
        WakeWordConfig config;
        config.text = text->valuestring;
        config.display = (display && cJSON_IsString(display)) ? 
                         display->valuestring : config.text;
        
        ESP_LOGI(TAG, "   display: %s", config.display.c_str());
        
        // 解析音素数组（可选字段）
        bool phoneme_format_error = false;
        if (phonemes && cJSON_IsArray(phonemes)) {
            int phonemes_count = cJSON_GetArraySize(phonemes);
            ESP_LOGI(TAG, "   phonemes 数量: %d", phonemes_count);
            
            cJSON* phoneme_item = nullptr;
            int phoneme_index = 0;
            cJSON_ArrayForEach(phoneme_item, phonemes) {
                if (cJSON_IsString(phoneme_item)) {
                    std::string phoneme_str = phoneme_item->valuestring;
                    
                    // ⚠️ 关键验证：MultiNet 要求音素至少 3 个字符，否则会崩溃！
                    // 原因：MultiNet 内部 FST 构建时会访问字符串的第3个字符
                    if (phoneme_str.length() < 3) {
                        ESP_LOGE(TAG, "      ❌ 音素 '%s' 太短（%d 字符 < 3），不符合 MultiNet 要求", 
                                phoneme_str.c_str(), phoneme_str.length());
                        ESP_LOGE(TAG, "         MultiNet 会在 fst_minimize 时崩溃！");
                        phoneme_format_error = true;
                        break;
                    }
                    
                    // 验证音素不能全是空格
                    bool all_spaces = true;
                    for (char c : phoneme_str) {
                        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                            all_spaces = false;
                            break;
                        }
                    }
                    if (all_spaces) {
                        ESP_LOGE(TAG, "      ❌ 音素全是空白字符，不符合 MultiNet 要求");
                        phoneme_format_error = true;
                        break;
                    }
                    
                    phoneme_index++;
                    config.phonemes.push_back(phoneme_str);
                    ESP_LOGI(TAG, "      [%d] %s (长度: %d)", phoneme_index, phoneme_str.c_str(), phoneme_str.length());
                } else {
                    ESP_LOGW(TAG, "      [%d] 跳过非字符串音素", phoneme_index + 1);
                }
            }
        } else {
            ESP_LOGI(TAG, "   ℹ️  未提供 phonemes 字段");
        }
        
        // 如果音素格式错误，跳过该唤醒词
        if (phoneme_format_error) {
            ESP_LOGE(TAG, "❌ 音素格式错误，跳过该唤醒词 '%s'", config.text.c_str());
            ESP_LOGE(TAG, "   ⚠️ 重要：音素长度必须 >= 3 字符，否则 MultiNet 会崩溃");
            ESP_LOGE(TAG, "   ✅ 正确示例：'HI PANDA', 'HEY PLAUD', 'HAI PAN DA'");
            ESP_LOGE(TAG, "   ❌ 错误示例：'hi', 'ok', '你' (太短)");
            continue;
        }
        
        // ✅ 策略：text 永远作为第一个默认音素，phonemes 中的其他音素作为额外变体
        // 先检查 text 是否已经在 phonemes 中
        bool text_exists = false;
        for (const auto& p : config.phonemes) {
            if (p == config.text) {
                text_exists = true;
                break;
            }
        }
        
        // 如果 text 不在 phonemes 中，将其作为第一个音素插入
        if (!text_exists) {
            config.phonemes.insert(config.phonemes.begin(), config.text);
            ESP_LOGI(TAG, "   💡 将 text 作为默认音素添加到最前面: '%s'", config.text.c_str());
        } else {
            ESP_LOGI(TAG, "   ℹ️  text 已存在于 phonemes 中，无需重复添加");
        }
        
        ESP_LOGI(TAG, "   📊 最终音素列表 (共 %d 个):", config.phonemes.size());
        for (size_t i = 0; i < config.phonemes.size(); i++) {
            ESP_LOGI(TAG, "      [%d] %s%s", i + 1, config.phonemes[i].c_str(),
                    (config.phonemes[i] == config.text) ? " (默认)" : "");
        }
        
        wake_words.push_back(config);
        ESP_LOGI(TAG, "✅ 成功解析唤醒词: %s (%d 个音素变体)", 
                 config.text.c_str(), config.phonemes.size());
    }
    
    ESP_LOGI(TAG, "----------------------------------------");
    ESP_LOGI(TAG, "✓ 唤醒词解析完成，共成功解析 %d 个", wake_words.size());
    
    // 应用配置
    if (wake_words.empty()) {
        ESP_LOGE(TAG, "❌ 没有有效的唤醒词");
        SendErrorResponse("set_wake_words", -2, 
            "音素格式错误：所有音素长度必须 >= 3 字符（例如：'HI PANDA', 'HEY PLAUD'）");
        ESP_LOGI(TAG, "========================================");
        return;
    }
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "📦 准备保存唤醒词配置到 NVS...");
    ESP_LOGI(TAG, "   唤醒词数量: %d", wake_words.size());
    ESP_LOGI(TAG, "   阈值: %.3f", threshold);
    ESP_LOGI(TAG, "   替换模式: %s", replace ? "是" : "否");
    
    auto& manager = WakeWordManager::GetInstance();
    bool success = manager.SetWakeWords(wake_words, threshold, replace);
    
    if (success) {
        ESP_LOGI(TAG, "✅ 唤醒词配置已保存到 NVS，共 %d 个", wake_words.size());
        
        // 立即应用配置（运行时生效，无需重启！）
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "🔄 尝试立即应用唤醒词配置（运行时更新）...");
        bool applied = Application::GetInstance().ApplyWakeWordConfig();
        
        // 构建响应
        cJSON* response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "cmd", "set_wake_words");
        cJSON_AddStringToObject(response, "status", "success");
        
        cJSON* response_data = cJSON_CreateObject();
        if (applied) {
            cJSON_AddStringToObject(response_data, "message", "唤醒词配置成功并已立即生效");
            cJSON_AddBoolToObject(response_data, "runtime_applied", true);
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "✅✅✅ 唤醒词已立即生效，无需重启！");
        } else {
            cJSON_AddStringToObject(response_data, "message", "唤醒词配置成功，重启后生效");
            cJSON_AddBoolToObject(response_data, "runtime_applied", false);
            ESP_LOGW(TAG, "");
            ESP_LOGW(TAG, "⚠️  唤醒词已保存到 NVS，但运行时应用失败");
            ESP_LOGW(TAG, "⚠️  请重启设备使唤醒词生效");
        }
        cJSON_AddNumberToObject(response_data, "count", wake_words.size());
        cJSON_AddItemToObject(response, "data", response_data);
        
        char* json_str = cJSON_PrintUnformatted(response);
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "📤 发送响应给 App: %s", json_str);
        bool send_ok = SendResponse(std::string(json_str));
        if (send_ok) {
            ESP_LOGI(TAG, "✓ 响应发送成功");
        } else {
            ESP_LOGE(TAG, "❌ 响应发送失败");
        }
        free(json_str);
        cJSON_Delete(response);
        
        // 播放成功提示音（与设备 ready 时相同）
        Application::GetInstance().PlaySuccessSound();
    } else {
        ESP_LOGE(TAG, "❌ 唤醒词配置保存到 NVS 失败");
        SendErrorResponse("set_wake_words", -3, "NVS存储失败");
    }
    
    ESP_LOGI(TAG, "========================================");
}

void BLEWiFiProvisioner::HandleGetWakeWordsCommand() {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "获取唤醒词列表");
    
    auto& manager = WakeWordManager::GetInstance();
    auto wake_words = manager.GetWakeWords();
    
    ESP_LOGI(TAG, "当前唤醒词数量: %d", wake_words.size());
    
    // 构建响应
    cJSON* response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "cmd", "get_wake_words");
    cJSON_AddStringToObject(response, "status", "success");
    
    cJSON* response_data = cJSON_CreateObject();
    
    // 构建唤醒词数组
    cJSON* words_array = cJSON_CreateArray();
    for (const auto& word : wake_words) {
        cJSON* word_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(word_obj, "text", word.text.c_str());
        cJSON_AddStringToObject(word_obj, "display", word.display.c_str());
        
        cJSON* phonemes_array = cJSON_CreateArray();
        for (const auto& phoneme : word.phonemes) {
            cJSON_AddItemToArray(phonemes_array, cJSON_CreateString(phoneme.c_str()));
        }
        cJSON_AddItemToObject(word_obj, "phonemes", phonemes_array);
        
        cJSON_AddItemToArray(words_array, word_obj);
    }
    
    cJSON_AddItemToObject(response_data, "words", words_array);
    cJSON_AddNumberToObject(response_data, "threshold", manager.GetThreshold());
    cJSON_AddNumberToObject(response_data, "count", wake_words.size());
    
    cJSON_AddItemToObject(response, "data", response_data);
    
    char* json_str = cJSON_PrintUnformatted(response);
    SendResponse(std::string(json_str));
    free(json_str);
    cJSON_Delete(response);
    
    ESP_LOGI(TAG, "✓ 已发送唤醒词列表");
    ESP_LOGI(TAG, "========================================");
}

void BLEWiFiProvisioner::HandleDeleteWakeWordCommand(cJSON* root) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "删除唤醒词");
    
    cJSON* data_item = cJSON_GetObjectItem(root, "data");
    if (!data_item || !cJSON_IsObject(data_item)) {
        ESP_LOGE(TAG, "❌ data 字段缺失或格式错误");
        SendErrorResponse("delete_wake_word", ERROR_JSON_PARSE_FAILED, "data字段缺失");
        return;
    }
    
    cJSON* text_item = cJSON_GetObjectItem(data_item, "text");
    if (!text_item || !cJSON_IsString(text_item)) {
        ESP_LOGE(TAG, "❌ text 字段缺失或格式错误");
        SendErrorResponse("delete_wake_word", ERROR_JSON_PARSE_FAILED, "text字段缺失");
        return;
    }
    
    std::string text = text_item->valuestring;
    ESP_LOGI(TAG, "删除唤醒词: %s", text.c_str());
    
    auto& manager = WakeWordManager::GetInstance();
    bool success = manager.DeleteWakeWord(text);
    
    if (success) {
        ESP_LOGI(TAG, "✓ 唤醒词删除成功");
        
        cJSON* response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "cmd", "delete_wake_word");
        cJSON_AddStringToObject(response, "status", "success");
        
        cJSON* response_data = cJSON_CreateObject();
        cJSON_AddStringToObject(response_data, "message", "唤醒词删除成功");
        cJSON_AddItemToObject(response, "data", response_data);
        
        char* json_str = cJSON_PrintUnformatted(response);
        SendResponse(std::string(json_str));
        free(json_str);
        cJSON_Delete(response);
    } else {
        ESP_LOGE(TAG, "❌ 唤醒词删除失败");
        SendErrorResponse("delete_wake_word", -1, "唤醒词不存在");
    }
    
    ESP_LOGI(TAG, "========================================");
}

void BLEWiFiProvisioner::HandleResetWakeWordsCommand() {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "重置唤醒词为默认值");
    
    auto& manager = WakeWordManager::GetInstance();
    bool success = manager.ResetToDefault();
    
    if (success) {
        ESP_LOGI(TAG, "✓ 唤醒词重置成功");
        
        cJSON* response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "cmd", "reset_wake_words");
        cJSON_AddStringToObject(response, "status", "success");
        
        cJSON* response_data = cJSON_CreateObject();
        cJSON_AddStringToObject(response_data, "message", "已恢复默认唤醒词");
        cJSON_AddNumberToObject(response_data, "count", manager.GetCount());
        cJSON_AddItemToObject(response, "data", response_data);
        
        char* json_str = cJSON_PrintUnformatted(response);
        SendResponse(std::string(json_str));
        free(json_str);
        cJSON_Delete(response);
    } else {
        ESP_LOGE(TAG, "❌ 唤醒词重置失败");
        SendErrorResponse("reset_wake_words", -3, "NVS存储失败");
    }
    
    ESP_LOGI(TAG, "========================================");
}

void BLEWiFiProvisioner::HandleSetOtaUrlCommand(cJSON* root) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "设置 OTA URL");
    
    // 从 JSON 中提取 URL
    cJSON* data_item = cJSON_GetObjectItem(root, "data");
    if (!data_item || !cJSON_IsObject(data_item)) {
        ESP_LOGE(TAG, "❌ data字段缺失或格式错误");
        SendErrorResponse("set_ota_url", ERROR_JSON_PARSE_FAILED, "data字段缺失");
        ESP_LOGI(TAG, "========================================");
        return;
    }
    
    cJSON* url_item = cJSON_GetObjectItem(data_item, "url");
    if (!url_item || !cJSON_IsString(url_item)) {
        ESP_LOGE(TAG, "❌ url字段缺失或格式错误");
        SendErrorResponse("set_ota_url", ERROR_JSON_PARSE_FAILED, "url字段缺失");
        ESP_LOGI(TAG, "========================================");
        return;
    }
    
    std::string url = url_item->valuestring;
    ESP_LOGI(TAG, "新的 OTA URL: %s", url.c_str());
    
    // 基本验证：检查 URL 格式
    if (url.empty() || (url.find("http://") != 0 && url.find("https://") != 0)) {
        ESP_LOGE(TAG, "❌ 无效的 URL 格式");
        SendErrorResponse("set_ota_url", ERROR_JSON_PARSE_FAILED, "URL格式无效，必须以http://或https://开头");
        ESP_LOGI(TAG, "========================================");
        return;
    }
    
    // 保存到 NVS
    try {
        Settings settings("system", true);
        settings.SetString("ota_url", url);
        
        ESP_LOGI(TAG, "✓ OTA URL 已保存到 NVS");
        
        // 构建成功响应
        cJSON* response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "cmd", "set_ota_url");
        cJSON_AddStringToObject(response, "status", "success");
        
        cJSON* response_data = cJSON_CreateObject();
        cJSON_AddStringToObject(response_data, "message", "OTA URL设置成功");
        cJSON_AddStringToObject(response_data, "url", url.c_str());
        cJSON_AddItemToObject(response, "data", response_data);
        
        char* json_str = cJSON_PrintUnformatted(response);
        SendResponse(std::string(json_str));
        free(json_str);
        cJSON_Delete(response);
        
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "❌ 保存 OTA URL 失败: %s", e.what());
        SendErrorResponse("set_ota_url", ERROR_STORAGE_WRITE_FAILED, "NVS存储失败");
    }
    
    ESP_LOGI(TAG, "========================================");
}

void BLEWiFiProvisioner::HandleGetOtaUrlCommand() {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "获取 OTA URL");
    
    std::string custom_url;
    std::string default_url = CONFIG_OTA_URL;
    bool has_custom = false;
    
    // 尝试从 NVS 读取自定义 URL
    try {
        Settings settings("system", false);
        custom_url = settings.GetString("ota_url", "");
        if (!custom_url.empty()) {
            has_custom = true;
            ESP_LOGI(TAG, "✓ 找到自定义 OTA URL: %s", custom_url.c_str());
        } else {
            ESP_LOGI(TAG, "ℹ️  使用默认 OTA URL: %s", default_url.c_str());
        }
    } catch (const std::exception& e) {
        ESP_LOGW(TAG, "⚠️  读取自定义 OTA URL 失败: %s，使用默认值", e.what());
    }
    
    // 构建响应
    cJSON* response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "cmd", "get_ota_url");
    cJSON_AddStringToObject(response, "status", "success");
    
    cJSON* response_data = cJSON_CreateObject();
    cJSON_AddStringToObject(response_data, "default_url", default_url.c_str());
    cJSON_AddStringToObject(response_data, "current_url", has_custom ? custom_url.c_str() : default_url.c_str());
    cJSON_AddBoolToObject(response_data, "is_custom", has_custom);
    cJSON_AddItemToObject(response, "data", response_data);
    
    char* json_str = cJSON_PrintUnformatted(response);
    SendResponse(std::string(json_str));
    free(json_str);
    cJSON_Delete(response);
    
    ESP_LOGI(TAG, "========================================");
}

