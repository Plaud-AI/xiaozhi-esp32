#include "bluetooth_service.h"
#include <esp_log.h>
#include <esp_mac.h>
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
    : initialized_(false), connected_(false), conn_handle_(0) {
    g_instance = this;
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
                    
                    ESP_LOGI(TAG, "收到数据: %s (长度: %d)", data, om_len);
                    
                    if (g_instance->data_received_callback_) {
                        g_instance->data_received_callback_(std::string(data, om_len));
                    }
                    
                    // 自动回复
                    std::string reply = "收到: " + std::string(data, om_len);
                    g_instance->SendData(reply);
                    
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
            ESP_LOGI(TAG, "连接事件: status=%d", event->connect.status);
            if (event->connect.status == 0) {
                g_instance->connected_ = true;
                g_instance->conn_handle_ = event->connect.conn_handle;
                ESP_LOGI(TAG, "客户端已连接，连接句柄: %d", event->connect.conn_handle);
            } else {
                // 连接失败，重新开始广播
                g_instance->StartAdvertising();
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "客户端断开连接，原因: %d", event->disconnect.reason);
            g_instance->connected_ = false;
            g_instance->conn_handle_ = 0;
            // 重新开始广播
            g_instance->StartAdvertising();
            break;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            ESP_LOGI(TAG, "广播完成");
            g_instance->StartAdvertising();
            break;

        case BLE_GAP_EVENT_SUBSCRIBE:
            ESP_LOGI(TAG, "订阅事件: conn_handle=%d attr_handle=%d",
                     event->subscribe.conn_handle,
                     event->subscribe.attr_handle);
            break;

        case BLE_GAP_EVENT_MTU:
            ESP_LOGI(TAG, "MTU更新: conn_handle=%d mtu=%d",
                     event->mtu.conn_handle,
                     event->mtu.value);
            break;

        default:
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

    // 开始广播
    if (g_instance) {
        g_instance->StartAdvertising();
    }
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

    // 初始化NimBLE
    int ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NimBLE端口初始化失败: %d", ret);
        return false;
    }

    // 初始化GAP和GATT服务
    ble_svc_gap_init();
    ble_svc_gatt_init();

    // 设置设备名称
    ret = ble_svc_gap_device_name_set(device_name_.c_str());
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
    adv_params.itvl_min = 0x20;  // 20ms
    adv_params.itvl_max = 0x40;  // 40ms

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

    ble_gap_adv_stop();
    ESP_LOGI(TAG, "BLE广播已停止");
}

bool BluetoothService::SendData(const std::string& data) {
    if (!connected_) {
        ESP_LOGW(TAG, "未连接，无法发送数据");
        return false;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data.c_str(), data.length());
    if (!om) {
        ESP_LOGE(TAG, "分配mbuf失败");
        return false;
    }

    int rc = ble_gatts_notify_custom(conn_handle_, g_char_val_handle, om);
    if (rc != 0) {
        ESP_LOGE(TAG, "发送通知失败: %d", rc);
        return false;
    }

    ESP_LOGI(TAG, "已发送数据: %s", data.c_str());
    return true;
}

void BluetoothService::SetDataReceivedCallback(std::function<void(const std::string&)> callback) {
    data_received_callback_ = callback;
}

