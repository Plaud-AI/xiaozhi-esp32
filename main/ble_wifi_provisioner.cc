#include "ble_wifi_provisioner.h"
#include "bluetooth_service.h"
#include "system_info.h"
#include "settings.h"

#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <esp_mac.h>
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

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "初始化 BLE WiFi Provisioner");
    ESP_LOGI(TAG, "设备名称: %s", device_name.c_str());
    ESP_LOGI(TAG, "========================================");

    // 初始化蓝牙服务
    auto& ble_service = BluetoothService::GetInstance();
    if (!ble_service.Initialize(device_name)) {
        ESP_LOGE(TAG, "❌ 蓝牙服务初始化失败");
        return false;
    }

    ESP_LOGI(TAG, "✓ 蓝牙服务初始化成功");
    ESP_LOGI(TAG, "✓ MAC地址: %s", ble_service.GetMacAddress().c_str());

    // 设置数据接收回调
    ble_service.SetDataReceivedCallback([this](const std::string& data) {
        ESP_LOGD(TAG, "========================================");
        ESP_LOGD(TAG, "收到BLE数据（长度: %d字节）", data.length());
        ESP_LOGD(TAG, "数据内容: %s", data.c_str());
        ESP_LOGD(TAG, "========================================");
        this->HandleReceivedData(data);
    });

    ESP_LOGI(TAG, "✓ 数据接收回调设置成功");

    initialized_ = true;
    ESP_LOGI(TAG, "✓ BLE WiFi Provisioner 初始化完成");

    return true;
}

bool BLEWiFiProvisioner::Start() {
    if (!initialized_) {
        ESP_LOGE(TAG, "❌ BLE WiFi Provisioner 未初始化");
        return false;
    }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "启动 BLE WiFi Provisioner");
    ESP_LOGI(TAG, "========================================");

    auto& ble_service = BluetoothService::GetInstance();
    if (!ble_service.StartAdvertising()) {
        ESP_LOGE(TAG, "❌ BLE广播启动失败");
        return false;
    }

    is_provisioning_ = true;
    ESP_LOGI(TAG, "✓ BLE广播已启动，等待手机连接...");
    ESP_LOGI(TAG, "📱 请在手机App中搜索设备: %s", ble_service.GetDeviceName().c_str());
    
    return true;
}

void BLEWiFiProvisioner::Stop() {
    ESP_LOGI(TAG, "停止 BLE WiFi Provisioner");
    
    auto& ble_service = BluetoothService::GetInstance();
    ble_service.StopAdvertising();
    
    is_provisioning_ = false;
    ESP_LOGI(TAG, "✓ BLE WiFi Provisioner 已停止");
}

void BLEWiFiProvisioner::HandleReceivedData(const std::string& data) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "开始处理接收到的数据");
    ESP_LOGI(TAG, "========================================");

    // 解析JSON
    cJSON* root = cJSON_Parse(data.c_str());
    if (!root) {
        ESP_LOGE(TAG, "❌ JSON解析失败");
        ESP_LOGE(TAG, "原始数据: %s", data.c_str());
        const char* error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            ESP_LOGE(TAG, "JSON错误位置: %s", error_ptr);
        }
        SendErrorResponse("unknown", ERROR_JSON_PARSE_FAILED, "JSON解析失败");
        return;
    }

    // 获取命令
    cJSON* cmd_item = cJSON_GetObjectItem(root, "cmd");
    if (!cmd_item || !cJSON_IsString(cmd_item)) {
        ESP_LOGE(TAG, "❌ 命令字段缺失或格式错误");
        cJSON_Delete(root);
        SendErrorResponse("unknown", ERROR_JSON_PARSE_FAILED, "命令字段缺失");
        return;
    }

    std::string cmd = cmd_item->valuestring;
    ESP_LOGI(TAG, "📥 收到命令: %s", cmd.c_str());

    // 根据命令类型处理
    if (cmd == "scan_wifi") {
        ESP_LOGI(TAG, "➜ 执行: WiFi扫描命令");
        HandleScanWiFiCommand();
    } 
    else if (cmd == "wifi_config") {
        ESP_LOGI(TAG, "➜ 执行: WiFi配置命令");
        
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

        if (!ssid_item || !cJSON_IsString(ssid_item) ||
            !password_item || !cJSON_IsString(password_item)) {
            ESP_LOGE(TAG, "❌ ssid或password字段缺失或格式错误");
            cJSON_Delete(root);
            SendErrorResponse(cmd, ERROR_JSON_PARSE_FAILED, "ssid或password字段缺失");
            return;
        }

        std::string ssid = ssid_item->valuestring;
        std::string password = password_item->valuestring;
        std::string bssid = "";
        
        if (bssid_item && cJSON_IsString(bssid_item)) {
            bssid = bssid_item->valuestring;
        }

        ESP_LOGI(TAG, "WiFi配置参数:");
        ESP_LOGI(TAG, "  SSID: %s", ssid.c_str());
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

    // 初始化WiFi（如果未初始化）
    esp_err_t ret = esp_wifi_set_mode(WIFI_MODE_STA);
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

    ESP_LOGI(TAG, "✓ WiFi已启动，开始扫描...");

    // 配置扫描参数
    wifi_scan_config_t scan_config = {
        .ssid = nullptr,
        .bssid = nullptr,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = {
            .active = {
                .min = 100,
                .max = 300
            }
        }
    };

    // 启动扫描
    ret = esp_wifi_scan_start(&scan_config, true);  // 阻塞扫描
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ WiFi扫描启动失败: %s", esp_err_to_name(ret));
        SendErrorResponse("scan_wifi", ERROR_UNKNOWN, "WiFi扫描失败");
        return;
    }

    ESP_LOGI(TAG, "✓ WiFi扫描完成，正在获取结果...");

    // 延迟以确保扫描结果可用
    vTaskDelay(pdMS_TO_TICKS(100));

    // 发送扫描结果
    std::string response = BuildScanResultJson();
    SendResponse(response);

    ESP_LOGI(TAG, "✓ WiFi扫描结果已发送");
    ESP_LOGI(TAG, "========================================");
}

void BLEWiFiProvisioner::HandleWiFiConfigCommand(const std::string& ssid, 
                                                  const std::string& password,
                                                  const std::string& bssid) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "开始WiFi配置");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "目标SSID: %s", ssid.c_str());
    ESP_LOGI(TAG, "密码长度: %d", password.length());

    // 验证密码长度
    if (!password.empty() && (password.length() < 8 || password.length() > 63)) {
        ESP_LOGE(TAG, "❌ 密码长度不符合要求（8-63字符）");
        SendErrorResponse("wifi_config", ERROR_PASSWORD_INCORRECT, 
                         "密码长度必须为8-63字符");
        return;
    }

    // 保存WiFi凭证
    ESP_LOGI(TAG, "保存WiFi凭证到NVS...");
    auto& ssid_manager = SsidManager::GetInstance();
    
    // 添加WiFi凭证
    ssid_manager.AddSsid(ssid, password);
    ESP_LOGI(TAG, "✓ WiFi凭证已保存");

    // 尝试连接WiFi
    ESP_LOGI(TAG, "正在连接到WiFi: %s", ssid.c_str());
    
    auto& wifi_station = WifiStation::GetInstance();
    wifi_station.Stop();  // 先停止当前连接
    
    vTaskDelay(pdMS_TO_TICKS(500));  // 等待停止完成
    
    wifi_station.Start();  // 启动WiFi Station
    
    // 等待连接（超时30秒）
    bool connected = wifi_station.WaitForConnected(30 * 1000);
    
    if (connected) {
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "✅ WiFi连接成功！");
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "SSID: %s", wifi_station.GetSsid().c_str());
        ESP_LOGI(TAG, "IP地址: %s", wifi_station.GetIpAddress().c_str());
        ESP_LOGI(TAG, "RSSI: %d dBm", wifi_station.GetRssi());
        ESP_LOGI(TAG, "信道: %d", wifi_station.GetChannel());
        ESP_LOGI(TAG, "========================================");

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
        if (provision_success_callback_) {
            provision_success_callback_(ssid, password);
        }

        // 延迟后重启设备
        ESP_LOGI(TAG, "将在2秒后重启设备...");
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
        
    } else {
        ESP_LOGE(TAG, "========================================");
        ESP_LOGE(TAG, "❌ WiFi连接失败");
        ESP_LOGE(TAG, "========================================");
        ESP_LOGE(TAG, "可能的原因:");
        ESP_LOGE(TAG, "  1. 密码错误");
        ESP_LOGE(TAG, "  2. WiFi信号太弱");
        ESP_LOGE(TAG, "  3. SSID不存在");
        ESP_LOGE(TAG, "  4. 路由器拒绝连接");
        ESP_LOGE(TAG, "========================================");

        // 删除刚保存的凭证
        ssid_manager.RemoveSsid(ssid);
        ESP_LOGW(TAG, "已删除无效的WiFi凭证");

        SendErrorResponse("wifi_config", ERROR_CONNECTION_TIMEOUT, 
                         "WiFi连接超时，请检查密码和信号强度");

        // 调用失败回调
        if (provision_failure_callback_) {
            provision_failure_callback_("连接超时");
        }
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

void BLEWiFiProvisioner::HandleDeleteWiFiCommand(const std::string& ssid) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "删除WiFi配置");
    ESP_LOGI(TAG, "SSID: %s", ssid.c_str());
    ESP_LOGI(TAG, "========================================");

    auto& ssid_manager = SsidManager::GetInstance();
    ssid_manager.RemoveSsid(ssid);

    ESP_LOGI(TAG, "✓ WiFi配置已删除");

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

    cJSON* networks = cJSON_CreateArray();
    for (int i = 0; i < ap_count; i++) {
        ESP_LOGI(TAG, "网络 %d:", i + 1);
        ESP_LOGI(TAG, "  SSID: %s", ap_records[i].ssid);
        ESP_LOGI(TAG, "  RSSI: %d dBm", ap_records[i].rssi);
        ESP_LOGI(TAG, "  信道: %d", ap_records[i].primary);
        ESP_LOGI(TAG, "  加密: %d", ap_records[i].authmode);

        cJSON* network = cJSON_CreateObject();
        cJSON_AddStringToObject(network, "ssid", (char*)ap_records[i].ssid);
        cJSON_AddNumberToObject(network, "rssi", ap_records[i].rssi);
        cJSON_AddNumberToObject(network, "channel", ap_records[i].primary);
        cJSON_AddNumberToObject(network, "auth_mode", GetAuthModeValue(ap_records[i].authmode));
        
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

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "cmd", "get_device_info");
    cJSON_AddStringToObject(root, "status", "success");

    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "device_name", ble_service.GetDeviceName().c_str());
    cJSON_AddStringToObject(data, "firmware_version", SystemInfo::GetFirmwareVersion().c_str());
    cJSON_AddStringToObject(data, "hardware_version", SystemInfo::GetChipModel().c_str());
    cJSON_AddStringToObject(data, "mac_address", ble_service.GetMacAddress().c_str());
    cJSON_AddNumberToObject(data, "free_heap", esp_get_free_heap_size());
    
    uint32_t chip_id = 0;
    esp_efuse_mac_get_default((uint8_t*)&chip_id);
    char chip_id_str[16];
    snprintf(chip_id_str, sizeof(chip_id_str), "0x%08X", chip_id);
    cJSON_AddStringToObject(data, "chip_id", chip_id_str);

    cJSON_AddItemToObject(root, "data", data);

    char* json_str = cJSON_PrintUnformatted(root);
    std::string result;
    if (json_str) {
        result = json_str;
        free(json_str);
        
        ESP_LOGI(TAG, "设备信息:");
        ESP_LOGI(TAG, "  名称: %s", ble_service.GetDeviceName().c_str());
        ESP_LOGI(TAG, "  固件版本: %s", SystemInfo::GetFirmwareVersion().c_str());
        ESP_LOGI(TAG, "  硬件版本: %s", SystemInfo::GetChipModel().c_str());
        ESP_LOGI(TAG, "  MAC地址: %s", ble_service.GetMacAddress().c_str());
        ESP_LOGI(TAG, "  空闲堆: %u bytes", esp_get_free_heap_size());
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

