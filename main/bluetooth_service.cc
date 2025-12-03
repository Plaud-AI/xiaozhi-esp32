#include "bluetooth_service.h"
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_bt.h>  // Added for esp_bt_controller_get_status
#include <esp_random.h>  // 硬件随机数生成器
#include <nvs_flash.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <host/ble_hs.h>
#include <host/ble_uuid.h>
#include <host/util/util.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>
#include <cstring>  // memcpy, memset

#define TAG "BluetoothService"

// ====== 安全认证配置 ======
// 厂商标识码（用于广播数据过滤，只有你的 App 能识别）
#define MANUFACTURER_ID         0xFFFF  // 可申请正式的蓝牙 Company ID
#define APP_SIGNATURE           0x58495A48  // "XIZH" 的十六进制，你的 App 签名标识

// 应用层认证密钥（32字节，用于 HMAC-SHA256 挑战-应答认证）
// ⚠️ 重要：生产环境请更换为你自己的密钥，并保持 App 和固件一致
static const uint8_t AUTH_SECRET_KEY[32] = {
    0x58, 0x69, 0x61, 0x6F, 0x5A, 0x68, 0x69, 0x2D,  // "XiaoZhi-"
    0x45, 0x53, 0x50, 0x33, 0x32, 0x2D, 0x53, 0x65,  // "ESP32-Se"
    0x63, 0x72, 0x65, 0x74, 0x4B, 0x65, 0x79, 0x21,  // "cretKey!"
    0x32, 0x30, 0x32, 0x35, 0x00, 0x00, 0x00, 0x00   // "2025...."
};

// 认证超时时间（毫秒）
#define AUTH_TIMEOUT_MS         5000

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
    : initialized_(false), connected_(false), authenticated_(false), 
      conn_handle_(0), mtu_(23), receive_buffer_("") {
    g_instance = this;
    memset(auth_challenge_, 0, sizeof(auth_challenge_));
    ESP_LOGI(TAG, "BluetoothService构造，默认MTU: %d", mtu_);
}

uint16_t BluetoothService::GetManufacturerId() {
    return MANUFACTURER_ID;
}

uint32_t BluetoothService::GetAppSignature() {
    return APP_SIGNATURE;
}

void BluetoothService::Disconnect(uint8_t reason) {
    if (connected_ && conn_handle_ != 0) {
        ESP_LOGI(TAG, "🔌 主动断开连接，原因: 0x%02X", reason);
        ble_gap_terminate(conn_handle_, reason);
    }
}

void BluetoothService::GenerateAuthChallenge() {
    // 使用硬件随机数生成器
    for (int i = 0; i < 32; i += 4) {
        uint32_t rand_val = esp_random();
        memcpy(&auth_challenge_[i], &rand_val, 4);
    }
    ESP_LOGI(TAG, "🎲 生成新的认证挑战码");
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
                g_instance->authenticated_ = false;  // 新连接未认证
                
                // ====== 连接独占：停止广播，防止其他设备连接 ======
                g_instance->StopAdvertising();
                
                ESP_LOGI(TAG, "║ 结果: ✅ 连接成功");
                ESP_LOGI(TAG, "║ ─────────────────────────────────────────────────────────");
                ESP_LOGI(TAG, "║ • 连接句柄: %d", event->connect.conn_handle);
                ESP_LOGI(TAG, "║ • 设备名称: %s", g_instance->device_name_.c_str());
                ESP_LOGI(TAG, "║ • MAC 地址: %s", g_instance->GetMacAddress().c_str());
                ESP_LOGI(TAG, "║ • 当前 MTU: %d 字节", g_instance->mtu_);
                ESP_LOGI(TAG, "║ • 广播状态: 已停止（连接独占）");
                ESP_LOGI(TAG, "║ • 认证状态: 等待应用层认证");
                ESP_LOGI(TAG, "║");
                ESP_LOGI(TAG, "║ 🔐 等待 App 发送认证请求...");
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
            g_instance->authenticated_ = false;  // 重置认证状态
            // 清空接收缓冲区
            g_instance->receive_buffer_.clear();
            // 重新开始广播，允许新设备连接
            g_instance->StartAdvertising();
            break;
        }

        case BLE_GAP_EVENT_ADV_COMPLETE:
            // 只有未连接时才重启广播
            if (!g_instance->connected_) {
                ESP_LOGD(TAG, "📡 广播周期完成，自动重启广播");
                g_instance->StartAdvertising();
            }
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

        // ====== BLE 配对相关事件（Just Works 模式，无 PIN）======
        case BLE_GAP_EVENT_PASSKEY_ACTION: {
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════");
            ESP_LOGI(TAG, "║ 🔐 BLE 配对请求（Just Works 模式）");
            ESP_LOGI(TAG, "╠════════════════════════════════════════════════════════════");
            ESP_LOGI(TAG, "║ 配对动作: %d", event->passkey.params.action);
            
            struct ble_sm_io pkey = {0};
            
            if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
                // 数字比较模式：自动接受
                ESP_LOGI(TAG, "║ 🔢 数字比较: %lu（自动接受）", (unsigned long)event->passkey.params.numcmp);
                pkey.action = event->passkey.params.action;
                pkey.numcmp_accept = 1;
                ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            } else if (event->passkey.params.action == BLE_SM_IOACT_NONE) {
                ESP_LOGI(TAG, "║ ✅ Just Works 配对模式（无需 PIN）");
            } else {
                // 其他模式也自动处理（设备配置为 NO_IO，不应该收到这些）
                ESP_LOGW(TAG, "║ ⚠️ 意外的配对动作: %d", event->passkey.params.action);
            }
            
            ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════");
            ESP_LOGI(TAG, "");
            break;
        }

        case BLE_GAP_EVENT_ENC_CHANGE: {
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════");
            ESP_LOGI(TAG, "║ 🔒 BLE 加密状态变更");
            ESP_LOGI(TAG, "╠════════════════════════════════════════════════════════════");
            if (event->enc_change.status == 0) {
                ESP_LOGI(TAG, "║ ✅ 加密已启用，连接安全");
            } else {
                ESP_LOGW(TAG, "║ ⚠️ 加密状态变更: %d", event->enc_change.status);
            }
            ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════");
            ESP_LOGI(TAG, "");
            break;
        }

        case BLE_GAP_EVENT_REPEAT_PAIRING: {
            // 重复配对请求：删除旧的绑定信息，允许新配对
            ESP_LOGI(TAG, "🔄 重复配对请求，删除旧绑定");
            struct ble_gap_conn_desc desc;
            ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
            ble_store_util_delete_peer(&desc.peer_id_addr);
            return BLE_GAP_REPEAT_PAIRING_RETRY;
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
    
    // ====== 配置 BLE 安全（无 PIN 模式）======
    // 使用 Just Works 配对，不需要用户输入 PIN
    // 安全验证通过应用层的挑战-应答机制实现
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;  // 无 IO 能力，使用 Just Works
    ble_hs_cfg.sm_bonding = 0;                    // 不启用绑定
    ble_hs_cfg.sm_mitm = 0;                       // 不启用 MITM（由应用层认证替代）
    ble_hs_cfg.sm_sc = 0;                         // 不强制安全连接
    
    ESP_LOGI(TAG, "🔐 BLE 安全配置:");
    ESP_LOGI(TAG, "   • 配对模式: Just Works（无 PIN）");
    ESP_LOGI(TAG, "   • 应用层认证: 挑战-应答机制");
    ESP_LOGI(TAG, "   • 连接独占: 连接后停止广播");

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
    struct ble_hs_adv_fields rsp_fields;  // 扫描响应数据
    const char *name;

    memset(&fields, 0, sizeof(fields));
    memset(&rsp_fields, 0, sizeof(rsp_fields));
    memset(&adv_params, 0, sizeof(adv_params));

    // ====== 广播数据：厂商特定数据 ======
    // 格式: [厂商ID (2字节, 小端)] [应用签名 (4字节)]
    // App 端通过检查这个数据来过滤扫描结果
    static uint8_t mfg_data[6];
    mfg_data[0] = MANUFACTURER_ID & 0xFF;         // 厂商ID低字节
    mfg_data[1] = (MANUFACTURER_ID >> 8) & 0xFF;  // 厂商ID高字节
    mfg_data[2] = APP_SIGNATURE & 0xFF;           // 签名字节0
    mfg_data[3] = (APP_SIGNATURE >> 8) & 0xFF;    // 签名字节1
    mfg_data[4] = (APP_SIGNATURE >> 16) & 0xFF;   // 签名字节2
    mfg_data[5] = (APP_SIGNATURE >> 24) & 0xFF;   // 签名字节3

    // 设置广播标志和厂商数据
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.mfg_data = mfg_data;
    fields.mfg_data_len = sizeof(mfg_data);

    // 设置广播数据（主要包含厂商数据）
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "设置广播数据失败: %d", rc);
        return false;
    }

    // ====== 扫描响应数据：设备名称 ======
    // 将名称放在扫描响应中，节省广播数据空间
    name = device_name_.c_str();
    rsp_fields.name = (uint8_t *)name;
    rsp_fields.name_len = strlen(name);
    rsp_fields.name_is_complete = 1;

    // 设置扫描响应数据
    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "设置扫描响应数据失败: %d", rc);
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
    ESP_LOGI(TAG, "   • 厂商ID: 0x%04X", MANUFACTURER_ID);
    ESP_LOGI(TAG, "   • 应用签名: 0x%08lX", (unsigned long)APP_SIGNATURE);
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

void BluetoothService::Deinitialize() {
    if (!initialized_) {
        ESP_LOGD(TAG, "BLE服务未初始化，跳过反初始化");
        return;
    }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "🔧 正在停止 BLE 协议栈...");
    ESP_LOGI(TAG, "========================================");

    // 步骤1: 停止 BLE 广播
    if (ble_gap_adv_active()) {
        ble_gap_adv_stop();
        ESP_LOGI(TAG, "✅ 步骤 1/3: BLE 广播已停止");
    } else {
        ESP_LOGI(TAG, "✅ 步骤 1/3: BLE 广播未在运行");
    }

    // 步骤2: 断开所有连接
    if (connected_) {
        ble_gap_terminate(conn_handle_, BLE_ERR_REM_USER_CONN_TERM);
        vTaskDelay(pdMS_TO_TICKS(100));  // 等待断开完成
        connected_ = false;
        ESP_LOGI(TAG, "✅ 步骤 2/3: BLE 连接已断开");
    } else {
        ESP_LOGI(TAG, "✅ 步骤 2/3: 无活动连接");
    }

    // 步骤3: 停止 NimBLE Host 任务
    // 注意: nimble_port_stop() 会通知 NimBLE Host 任务停止
    // 之后 nimble_port_deinit() 会清理资源
    int rc = nimble_port_stop();
    if (rc == 0) {
        ESP_LOGI(TAG, "✅ 步骤 3/3: NimBLE 任务停止请求已发送");
        
        // 等待 NimBLE 任务完全停止
        vTaskDelay(pdMS_TO_TICKS(100));
        
        // 取消初始化 NimBLE port
        nimble_port_deinit();
        ESP_LOGI(TAG, "✅ NimBLE port 已取消初始化");
    } else {
        ESP_LOGW(TAG, "⚠️ 步骤 3/3: NimBLE 任务停止失败 (rc=%d)", rc);
    }

    initialized_ = false;
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "✅ BLE 协议栈已完全停止");
    ESP_LOGI(TAG, "   可以安全禁用 BLE 控制器");
    ESP_LOGI(TAG, "========================================");
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

bool BluetoothService::HandleAuthRequest(const std::string& data) {
    // 简单的认证协议：
    // 请求格式: {"type":"auth","key":"<hex_encoded_key>"}
    // 成功响应: {"type":"auth_result","success":true}
    // 失败响应: {"type":"auth_result","success":false,"error":"..."}
    
    // 检查是否是认证请求
    if (data.find("\"type\":\"auth\"") == std::string::npos) {
        return false;  // 不是认证请求
    }
    
    // 提取 key 字段（简单字符串查找）
    size_t key_start = data.find("\"key\":\"");
    if (key_start == std::string::npos) {
        ESP_LOGW(TAG, "🔐 认证请求缺少 key 字段");
        SendData("{\"type\":\"auth_result\",\"success\":false,\"error\":\"missing key\"}");
        return true;
    }
    
    key_start += 7;  // 跳过 "key":"
    size_t key_end = data.find("\"", key_start);
    if (key_end == std::string::npos) {
        ESP_LOGW(TAG, "🔐 认证请求 key 格式错误");
        SendData("{\"type\":\"auth_result\",\"success\":false,\"error\":\"invalid key format\"}");
        return true;
    }
    
    std::string provided_key = data.substr(key_start, key_end - key_start);
    
    // 计算预期的认证密钥（AUTH_SECRET_KEY 的十六进制表示）
    char expected_key[65];
    for (int i = 0; i < 32; i++) {
        snprintf(&expected_key[i * 2], 3, "%02x", AUTH_SECRET_KEY[i]);
    }
    expected_key[64] = '\0';
    
    // 验证密钥
    if (provided_key == expected_key) {
        authenticated_ = true;
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════");
        ESP_LOGI(TAG, "║ 🔐 应用层认证成功");
        ESP_LOGI(TAG, "╠════════════════════════════════════════════════════════════");
        ESP_LOGI(TAG, "║ ✅ 该连接已通过安全验证");
        ESP_LOGI(TAG, "║ 现在可以进行所有操作");
        ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════");
        ESP_LOGI(TAG, "");
        SendData("{\"type\":\"auth_result\",\"success\":true}");
    } else {
        ESP_LOGW(TAG, "");
        ESP_LOGW(TAG, "╔════════════════════════════════════════════════════════════");
        ESP_LOGW(TAG, "║ 🔐 应用层认证失败");
        ESP_LOGW(TAG, "╠════════════════════════════════════════════════════════════");
        ESP_LOGW(TAG, "║ ❌ 提供的密钥不正确");
        ESP_LOGW(TAG, "║ 将在 1 秒后断开连接");
        ESP_LOGW(TAG, "╚════════════════════════════════════════════════════════════");
        ESP_LOGW(TAG, "");
        SendData("{\"type\":\"auth_result\",\"success\":false,\"error\":\"invalid key\"}");
        
        // 延迟后断开连接
        vTaskDelay(pdMS_TO_TICKS(1000));
        Disconnect();
    }
    
    return true;
}

