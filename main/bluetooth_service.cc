#include "bluetooth_service.h"
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_bt.h>  // Added for esp_bt_controller_get_status
#include <nvs_flash.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <host/ble_hs.h>
#include <host/ble_uuid.h>
#include <host/util/util.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>

#define TAG "BluetoothService"

// 自定义服务UUID和特征UUID
// 服务UUID: 0000ffe0-0000-1000-8000-00805f9b34fb
static const ble_uuid128_t XIAOZHI_SERVICE_UUID = 
    BLE_UUID128_INIT(0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
                     0x00, 0x10, 0x00, 0x00, 0xe0, 0xff, 0x00, 0x00);

// 特征UUID: 0000ffe1-0000-1000-8000-00805f9b34fb
static const ble_uuid128_t XIAOZHI_CHAR_UUID = 
    BLE_UUID128_INIT(0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
                     0x00, 0x10, 0x00, 0x00, 0xe1, 0xff, 0x00, 0x00);

static uint16_t g_char_val_handle;
static BluetoothService* g_instance = nullptr;

BluetoothService::BluetoothService() 
    : initialized_(false), connected_(false), conn_handle_(0), mtu_(23), receive_buffer_("") {
    g_instance = this;
    ESP_LOGI(TAG, "BluetoothService构造，默认MTU: %d", mtu_);
}

BluetoothService::~BluetoothService() {
    g_instance = nullptr;
}

std::string BluetoothService::GetMacAddress() const {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BT);
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return std::string(mac_str);
}

int BluetoothService::gatt_svr_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                                          struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (!g_instance) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    switch (ctxt->op) {
        case BLE_GATT_ACCESS_OP_READ_CHR:
            ESP_LOGI(TAG, "客户端读取特征");
            return 0;

        case BLE_GATT_ACCESS_OP_WRITE_CHR:
            if (ctxt->om) {
                uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
                char *data = (char *)malloc(om_len + 1);
                if (data) {
                    os_mbuf_copydata(ctxt->om, 0, om_len, data);
                    data[om_len] = '\0';
                    
                    ESP_LOGI(TAG, "");
                    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════");
                    ESP_LOGI(TAG, "║ 📥 BLE 数据写入事件");
                    ESP_LOGI(TAG, "╠════════════════════════════════════════════════════════════");
                    ESP_LOGI(TAG, "║ 数据长度: %d 字节", om_len);
                    
                    // 打印数据内容（显示前50个字符，避免过长）
                    int preview_len = (om_len > 50) ? 50 : om_len;
                    ESP_LOGI(TAG, "║ 内容预览: %.*s%s", preview_len, data, 
                             (om_len > 50) ? "..." : "");
                    
                    // 显示前16个字节的十六进制表示（用于调试）
                    if (om_len > 0) {
                        char hex_buf[64];
                        int hex_len = (om_len > 16) ? 16 : om_len;
                        int hex_pos = 0;
                        for (int i = 0; i < hex_len && hex_pos < 60; i++) {
                            hex_pos += snprintf(hex_buf + hex_pos, sizeof(hex_buf) - hex_pos, 
                                              "%02X ", (unsigned char)data[i]);
                        }
                        ESP_LOGI(TAG, "║ 十六进制: %s%s", hex_buf, (om_len > 16) ? "..." : "");
                    }
                    
                    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════");
                    ESP_LOGI(TAG, "");
                    
                    // 处理接收到的数据片段（支持分包重组）
                    if (g_instance) {
                        g_instance->ProcessReceivedData(std::string(data, om_len));
                    }
                    
                    free(data);
                }
            }
            return 0;

        default:
            return BLE_ATT_ERR_UNLIKELY;
    }
}

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &XIAOZHI_SERVICE_UUID.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &XIAOZHI_CHAR_UUID.u,
                .access_cb = BluetoothService::gatt_svr_chr_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &g_char_val_handle,
            },
            {
                0, /* 结束标记 */
            }
        },
    },
    {
        0, /* 结束标记 */
    },
};

int BluetoothService::gap_event_handler(struct ble_gap_event *event, void *arg) {
    if (!g_instance) {
        return 0;
    }

    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════");
            ESP_LOGI(TAG, "║ 📱 BLE 连接事件");
            ESP_LOGI(TAG, "╠════════════════════════════════════════════════════════════");
            ESP_LOGI(TAG, "║ 连接状态码: %d", event->connect.status);
            
            if (event->connect.status == 0) {
                g_instance->connected_ = true;
                g_instance->conn_handle_ = event->connect.conn_handle;
                
                ESP_LOGI(TAG, "║ 结果: ✅ 连接成功");
                ESP_LOGI(TAG, "║ ─────────────────────────────────────────────────────────");
                ESP_LOGI(TAG, "║ • 连接句柄: %d", event->connect.conn_handle);
                ESP_LOGI(TAG, "║ • 设备名称: %s", g_instance->device_name_.c_str());
                ESP_LOGI(TAG, "║ • MAC 地址: %s", g_instance->GetMacAddress().c_str());
                ESP_LOGI(TAG, "║ • 当前 MTU: %d 字节", g_instance->mtu_);
                ESP_LOGI(TAG, "║");
                ESP_LOGI(TAG, "║ 🎉 手机已成功连接！现在可以:");
                ESP_LOGI(TAG, "║   - 扫描 WiFi 网络");
                ESP_LOGI(TAG, "║   - 配置 WiFi 连接");
                ESP_LOGI(TAG, "║   - 查看设备信息");
                ESP_LOGI(TAG, "║   - 设置唤醒词");
                ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════");
                ESP_LOGI(TAG, "");
            } else {
                ESP_LOGE(TAG, "║ 结果: ❌ 连接失败");
                ESP_LOGE(TAG, "║ 错误码: %d", event->connect.status);
                ESP_LOGE(TAG, "║");
                ESP_LOGE(TAG, "║ 🔄 自动重新启动广播...");
                ESP_LOGE(TAG, "╚════════════════════════════════════════════════════════════");
                ESP_LOGE(TAG, "");
                // 连接失败，重新开始广播
                g_instance->StartAdvertising();
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT: {
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════");
            ESP_LOGI(TAG, "║ 📱 BLE 断开连接事件");
            ESP_LOGI(TAG, "╠════════════════════════════════════════════════════════════");
            ESP_LOGI(TAG, "║ 连接句柄: %d", g_instance->conn_handle_);
            ESP_LOGI(TAG, "║ 断开原因码: %d", event->disconnect.reason);
            ESP_LOGI(TAG, "║");
            
            // 断开原因解释
            const char* reason_str = "未知原因";
            switch (event->disconnect.reason) {
                case 0x08: reason_str = "连接超时"; break;
                case 0x13: reason_str = "用户主动断开"; break;
                case 0x16: reason_str = "主机终止连接"; break;
                case 0x3D: reason_str = "连接参数不可接受"; break;
                default: break;
            }
            ESP_LOGI(TAG, "║ 原因说明: %s", reason_str);
            ESP_LOGI(TAG, "║");
            ESP_LOGI(TAG, "║ 🔄 清理操作:");
            ESP_LOGI(TAG, "║   • 清空接收缓冲区");
            ESP_LOGI(TAG, "║   • 重置连接状态");
            ESP_LOGI(TAG, "║   • 重新启动广播");
            ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════");
            ESP_LOGI(TAG, "");
            
            g_instance->connected_ = false;
            g_instance->conn_handle_ = 0;
            // 清空接收缓冲区
            g_instance->receive_buffer_.clear();
            // 重新开始广播
            g_instance->StartAdvertising();
            break;
        }

        case BLE_GAP_EVENT_ADV_COMPLETE:
            ESP_LOGD(TAG, "📡 广播周期完成，自动重启广播");
            g_instance->StartAdvertising();
            break;

        case BLE_GAP_EVENT_SUBSCRIBE: {
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════");
            ESP_LOGI(TAG, "║ 🔔 BLE 特征订阅事件");
            ESP_LOGI(TAG, "╠════════════════════════════════════════════════════════════");
            ESP_LOGI(TAG, "║ 连接句柄: %d", event->subscribe.conn_handle);
            ESP_LOGI(TAG, "║ 属性句柄: %d", event->subscribe.attr_handle);
            ESP_LOGI(TAG, "║");
            ESP_LOGI(TAG, "║ ℹ️  手机已订阅通知，可以:");
            ESP_LOGI(TAG, "║   - 接收设备主动推送的数据");
            ESP_LOGI(TAG, "║   - 接收命令执行结果");
            ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════");
            ESP_LOGI(TAG, "");
            break;
        }

        case BLE_GAP_EVENT_MTU: {
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════");
            ESP_LOGI(TAG, "║ 📏 BLE MTU 更新事件");
            ESP_LOGI(TAG, "╠════════════════════════════════════════════════════════════");
            ESP_LOGI(TAG, "║ 连接句柄: %d", event->mtu.conn_handle);
            ESP_LOGI(TAG, "║ 旧 MTU: %d 字节", g_instance->mtu_);
            ESP_LOGI(TAG, "║ 新 MTU: %d 字节", event->mtu.value);
            
            g_instance->mtu_ = event->mtu.value;
            
            ESP_LOGI(TAG, "║");
            ESP_LOGI(TAG, "║ ℹ️  MTU 说明:");
            ESP_LOGI(TAG, "║   • 每次最多传输: %d 字节", g_instance->mtu_ - 3);
            ESP_LOGI(TAG, "║   • 大数据包会自动分片传输");
            ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════");
            ESP_LOGI(TAG, "");
            break;
        }

        default:
            ESP_LOGD(TAG, "🔔 BLE 事件: type=%d", event->type);
            break;
    }

    return 0;
}

static void ble_host_task(void *param) {
    ESP_LOGI(TAG, "BLE主机任务已启动");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void ble_on_sync(void) {
    ESP_LOGI(TAG, "BLE堆栈已同步");
    
    // 确保使用随机地址
    uint8_t own_addr_type;
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "设置地址类型失败: %d", rc);
        return;
    }
    
    ESP_LOGI(TAG, "地址类型: %d", own_addr_type);

    // ⚠️ 注意：不在这里自动开始广播
    // 广播应由应用层（如 BLEWiFiProvisioner）在确保环境安全（如WiFi PS已禁用）后显式启动
    // 之前在 ble_on_sync 中自动启动会导致 WiFi/BLE 共存冲突 (rwble.c 508 assert)
}

static void ble_on_reset(int reason) {
    ESP_LOGE(TAG, "BLE堆栈重置，原因: %d", reason);
}

bool BluetoothService::Initialize(const std::string& device_name) {
    if (initialized_) {
        ESP_LOGW(TAG, "蓝牙服务已初始化");
        return true;
    }

    device_name_ = device_name;
    
    ESP_LOGI(TAG, "初始化蓝牙服务: %s", device_name_.c_str());

    // 检查 NimBLE 是否已在 main.cc 中初始化
    // 如果 BLE 堆栈已经运行，ble_hs_is_enabled() 会返回 true
    // 如果尚未初始化，我们需要初始化它
    static bool nimble_port_initialized = false;
    
    if (!nimble_port_initialized) {
        // Check if controller is already enabled (e.g. initialized in main.cc)
        if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) {
             ESP_LOGI(TAG, "Bluetooth controller already enabled (by main.cc)");
             nimble_port_initialized = true;
        } else {
        ESP_LOGI(TAG, "初始化 NimBLE 端口...");
        int ret = nimble_port_init();
        if (ret != ESP_OK) {
            // 如果返回 ESP_ERR_INVALID_STATE，说明已经初始化过了
            if (ret == ESP_ERR_INVALID_STATE) {
                ESP_LOGI(TAG, "NimBLE 端口已在启动时初始化");
                nimble_port_initialized = true;
            } else {
                ESP_LOGE(TAG, "NimBLE端口初始化失败: %d", ret);
                return false;
            }
        } else {
            ESP_LOGI(TAG, "✅ NimBLE 端口初始化成功");
            nimble_port_initialized = true;
            }
        }
    } else {
        ESP_LOGI(TAG, "✓ NimBLE 端口已初始化，跳过");
    }

    // 初始化GAP和GATT服务
    ble_svc_gap_init();
    ble_svc_gatt_init();

    // 设置设备名称
    int ret = ble_svc_gap_device_name_set(device_name_.c_str());
    if (ret != 0) {
        ESP_LOGE(TAG, "设置设备名称失败: %d", ret);
        return false;
    }

    // 注册GATT服务
    ret = ble_gatts_count_cfg(gatt_svr_svcs);
    if (ret != 0) {
        ESP_LOGE(TAG, "GATT服务配置计数失败: %d", ret);
        return false;
    }

    ret = ble_gatts_add_svcs(gatt_svr_svcs);
    if (ret != 0) {
        ESP_LOGE(TAG, "添加GATT服务失败: %d", ret);
        return false;
    }

    // 设置同步和重置回调
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.reset_cb = ble_on_reset;

    // 在启动BLE任务之前设置初始化标志，因为BLE任务可能很快同步并调用StartAdvertising()
    initialized_ = true;
    
    // 启动BLE主机任务
    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "蓝牙服务初始化成功");
    ESP_LOGI(TAG, "设备MAC地址: %s", GetMacAddress().c_str());

    return true;
}

bool BluetoothService::StartAdvertising() {
    if (!initialized_) {
        ESP_LOGE(TAG, "蓝牙服务未初始化");
        return false;
    }

    // 检查是否已经在广播
    if (ble_gap_adv_active()) {
        ESP_LOGW(TAG, "BLE广播已在运行，跳过重复启动");
        return true;  // 返回成功，因为广播确实在运行
    }

    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    const char *name;

    memset(&fields, 0, sizeof(fields));
    memset(&adv_params, 0, sizeof(adv_params));

    // 设置广播标志
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    // 设置设备名称
    name = device_name_.c_str();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    // 设置广播数据
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "设置广播数据失败: %d", rc);
        return false;
    }

    // 设置广播参数
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    // 增加广播间隔以避免 WiFi/BLE 共存冲突 (rwble.c 508 assert)
    // 0xA0 * 0.625ms = 100ms
    adv_params.itvl_min = 0xA0;  
    adv_params.itvl_max = 0xA0;

    // 开始广播
    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                          &adv_params, gap_event_handler, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "启动广播失败: %d", rc);
        return false;
    }

    ESP_LOGI(TAG, "BLE广播已启动");
    return true;
}

void BluetoothService::StopAdvertising() {
    if (!initialized_) {
        return;
    }

    // 检查是否正在广播，避免重复停止导致控制器崩溃 (rwble.c 508 assert)
    if (!ble_gap_adv_active()) {
        ESP_LOGD(TAG, "BLE广播未在运行，跳过停止操作");
        return;
    }

    ble_gap_adv_stop();
    ESP_LOGI(TAG, "BLE广播已停止");
}

bool BluetoothService::SendData(const std::string& data) {
    if (!connected_) {
        ESP_LOGW(TAG, "未连接，无法发送数据");
        return false;
    }

    // 添加换行符作为数据结束标记
    std::string data_with_end = data + "\n";
    size_t total_length = data_with_end.length();
    
    // BLE ATT协议开销是3字节，所以实际可用MTU是 (mtu - 3)
    size_t max_chunk_size = (mtu_ > 3) ? (mtu_ - 3) : 20;
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "准备发送数据");
    ESP_LOGI(TAG, "数据长度: %d 字节 (含结束符)", total_length);
    ESP_LOGI(TAG, "当前MTU: %d 字节", mtu_);
    ESP_LOGI(TAG, "分包大小: %d 字节", max_chunk_size);
    
    // 如果数据小于等于单个包大小，直接发送
    if (total_length <= max_chunk_size) {
        ESP_LOGI(TAG, "数据适合单包传输，直接发送");
        
        struct os_mbuf *om = ble_hs_mbuf_from_flat(data_with_end.c_str(), total_length);
        if (!om) {
            ESP_LOGE(TAG, "❌ 分配mbuf失败");
            return false;
        }

        int rc = ble_gatts_notify_custom(conn_handle_, g_char_val_handle, om);
        if (rc != 0) {
            ESP_LOGE(TAG, "❌ 发送通知失败: %d", rc);
            return false;
        }

        ESP_LOGI(TAG, "✓ 数据发送成功");
        ESP_LOGI(TAG, "========================================");
        return true;
    }
    
    // 需要分包发送
    size_t chunks_count = (total_length + max_chunk_size - 1) / max_chunk_size;
    ESP_LOGI(TAG, "数据需要分 %d 个包发送", chunks_count);
    
    size_t offset = 0;
    size_t chunk_index = 0;
    
    while (offset < total_length) {
        size_t chunk_size = std::min(max_chunk_size, total_length - offset);
        chunk_index++;
        
        ESP_LOGD(TAG, "发送第 %d/%d 包，大小: %d 字节", 
                 chunk_index, chunks_count, chunk_size);
        
        struct os_mbuf *om = ble_hs_mbuf_from_flat(
            data_with_end.c_str() + offset, 
            chunk_size
        );
        
        if (!om) {
            ESP_LOGE(TAG, "❌ 分配mbuf失败 (第 %d 包)", chunk_index);
            return false;
        }

        int rc = ble_gatts_notify_custom(conn_handle_, g_char_val_handle, om);
        if (rc != 0) {
            ESP_LOGE(TAG, "❌ 发送第 %d 包失败: %d", chunk_index, rc);
            return false;
        }
        
        offset += chunk_size;
        
        // 在发送包之间添加小延迟，避免数据拥塞
        if (offset < total_length) {
            vTaskDelay(pdMS_TO_TICKS(10));  // 10ms延迟
        }
    }
    
    ESP_LOGI(TAG, "✓ 所有数据包发送成功 (%d 包)", chunks_count);
    ESP_LOGI(TAG, "========================================");
    return true;
}

void BluetoothService::SetDataReceivedCallback(std::function<void(const std::string&)> callback) {
    data_received_callback_ = callback;
}

void BluetoothService::ProcessReceivedData(const std::string& data) {
    // 将接收到的数据片段添加到缓冲区
    receive_buffer_ += data;
    
    ESP_LOGI(TAG, "累积缓冲区大小: %d 字节", receive_buffer_.length());
    
    // 添加调试：显示缓冲区的首尾字符
    if (!receive_buffer_.empty()) {
        ESP_LOGI(TAG, "缓冲区首字符: '%c' (0x%02X)", 
                 receive_buffer_[0], (unsigned char)receive_buffer_[0]);
        ESP_LOGI(TAG, "缓冲区尾字符: '%c' (0x%02X)", 
                 receive_buffer_[receive_buffer_.length()-1], 
                 (unsigned char)receive_buffer_[receive_buffer_.length()-1]);
    }
    
    // 查找换行符（消息结束标记）
    size_t newline_pos = receive_buffer_.find('\n');
    
    // 情况1：找到换行符（标准分包传输场景）
    while (newline_pos != std::string::npos) {
        // 提取完整的消息（不包含换行符）
        std::string complete_message = receive_buffer_.substr(0, newline_pos);
        
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "📦 接收到完整消息（带换行符）");
        ESP_LOGI(TAG, "消息长度: %d 字节", complete_message.length());
        ESP_LOGI(TAG, "消息内容: %s", complete_message.c_str());
        ESP_LOGI(TAG, "========================================");
        
        // 调用数据接收回调
        if (data_received_callback_) {
            data_received_callback_(complete_message);
        }
        
        // 从缓冲区中移除已处理的消息（包括换行符）
        receive_buffer_.erase(0, newline_pos + 1);
        
        // 查找下一个换行符（可能同时收到多条消息）
        newline_pos = receive_buffer_.find('\n');
    }
    
    // 情况2：没有换行符，检查是否是完整JSON消息
    // 如果收到的数据以 { 开头且以 } 结尾，可能是完整的JSON消息（无换行符）
    if (!receive_buffer_.empty()) {
        // 去除首尾空白字符
        size_t start = 0;
        size_t end = receive_buffer_.length();
        
        // 跳过开头的空白字符
        while (start < end && std::isspace((unsigned char)receive_buffer_[start])) {
            start++;
        }
        
        // 跳过结尾的空白字符
        while (end > start && std::isspace((unsigned char)receive_buffer_[end - 1])) {
            end--;
        }
        
        // 检查是否是完整的JSON（以 { 开头且以 } 结尾）
        if (end > start && 
            receive_buffer_[start] == '{' && 
            receive_buffer_[end - 1] == '}') {
            
            std::string trimmed = receive_buffer_.substr(start, end - start);
            
            ESP_LOGI(TAG, "========================================");
            ESP_LOGI(TAG, "📦 接收到完整JSON消息（无换行符）");
            ESP_LOGI(TAG, "消息长度: %d 字节", trimmed.length());
            ESP_LOGI(TAG, "消息内容: %s", trimmed.c_str());
            ESP_LOGI(TAG, "========================================");
            
            // 调用数据接收回调
            if (data_received_callback_) {
                data_received_callback_(trimmed);
            }
            
            // 清空缓冲区
            receive_buffer_.clear();
        } else {
            // 不是完整的JSON，等待更多数据
            if (end > start) {
                ESP_LOGD(TAG, "等待更多数据... (首字符: '%c', 尾字符: '%c')", 
                         receive_buffer_[start], receive_buffer_[end - 1]);
            }
        }
    }
    
    // 检查缓冲区是否过大（防止内存溢出）
    if (receive_buffer_.length() > 4096) {
        ESP_LOGW(TAG, "⚠️  接收缓冲区过大(%d 字节)，可能数据格式错误，清空缓冲区", 
                 receive_buffer_.length());
        ESP_LOGW(TAG, "缓冲区内容: %s", receive_buffer_.c_str());
        receive_buffer_.clear();
    }
}

