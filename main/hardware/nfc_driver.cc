#include "nfc_driver.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <cstring>

#define TAG "NfcDriver"

// 默认配置常量
#define DEFAULT_BAUDRATE        115200
#define DEFAULT_RX_BUFFER_SIZE  1024
#define DEFAULT_TX_BUFFER_SIZE  1024

// NFC 协议常量
static const uint8_t NFC_POLLING_CMD[] = {0x43, 0x4D, 0x74, 0x02, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t NFC_RESPONSE_HEADER[] = {0x43, 0x4D, 0x74};

// 无卡响应长度
#define NFC_NO_CARD_RESPONSE_LEN    10
// 有卡响应最小长度
#define NFC_CARD_RESPONSE_MIN_LEN   18
// 卡号起始位置
#define NFC_CARD_ID_START_INDEX     13
// 卡号长度字段位置
#define NFC_CARD_ID_LEN_INDEX       12

// 卡片去重时间窗口 (毫秒)
#define CARD_DEBOUNCE_TIME_MS       1000

nfc_config_t NfcDriver::GetDefaultConfig(uart_port_t uart_port, gpio_num_t tx_pin, gpio_num_t rx_pin) {
    return nfc_config_t {
        .uart_port = uart_port,
        .tx_pin = tx_pin,
        .rx_pin = rx_pin,
        .baudrate = DEFAULT_BAUDRATE,
        .rx_buffer_size = DEFAULT_RX_BUFFER_SIZE,
        .tx_buffer_size = DEFAULT_TX_BUFFER_SIZE
    };
}

NfcDriver::NfcDriver(uart_port_t uart_port, gpio_num_t tx_pin, gpio_num_t rx_pin)
    : config_(GetDefaultConfig(uart_port, tx_pin, rx_pin))
    , initialized_(false)
    , card_callback_(nullptr)
    , continuous_running_(false)
    , continuous_interval_ms_(200)
    , continuous_task_(nullptr)
    , last_card_time_(0) {
}

NfcDriver::NfcDriver(const nfc_config_t& config)
    : config_(config)
    , initialized_(false)
    , card_callback_(nullptr)
    , continuous_running_(false)
    , continuous_interval_ms_(200)
    , continuous_task_(nullptr)
    , last_card_time_(0) {
}

NfcDriver::~NfcDriver() {
    Deinit();
}

esp_err_t NfcDriver::Init() {
    if (initialized_) {
        ESP_LOGW(TAG, "NFC driver already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing NFC driver:");
    ESP_LOGI(TAG, "  UART port: %d", config_.uart_port);
    ESP_LOGI(TAG, "  TX pin: %d", config_.tx_pin);
    ESP_LOGI(TAG, "  RX pin: %d", config_.rx_pin);
    ESP_LOGI(TAG, "  Baudrate: %lu", config_.baudrate);

    // 配置 UART
    uart_config_t uart_config = {
        .baud_rate = (int)config_.baudrate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT
    };

    esp_err_t ret = uart_driver_install(config_.uart_port, 
                                         config_.rx_buffer_size, 
                                         config_.tx_buffer_size, 
                                         0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = uart_param_config(config_.uart_port, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART: %s", esp_err_to_name(ret));
        uart_driver_delete(config_.uart_port);
        return ret;
    }

    ret = uart_set_pin(config_.uart_port, config_.tx_pin, config_.rx_pin, 
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART pins: %s", esp_err_to_name(ret));
        uart_driver_delete(config_.uart_port);
        return ret;
    }

    initialized_ = true;
    ESP_LOGI(TAG, "NFC driver initialized successfully");
    return ESP_OK;
}

void NfcDriver::Deinit() {
    if (!initialized_) {
        return;
    }

    ESP_LOGI(TAG, "Deinitializing NFC driver");

    // 停止连续模式
    StopContinuousMode();

    // 删除 UART 驱动
    uart_driver_delete(config_.uart_port);

    initialized_ = false;
}

esp_err_t NfcDriver::SendPollingCommand() {
    if (!initialized_) {
        ESP_LOGE(TAG, "NFC driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    int written = uart_write_bytes(config_.uart_port, (const char*)NFC_POLLING_CMD, sizeof(NFC_POLLING_CMD));
    if (written != sizeof(NFC_POLLING_CMD)) {
        ESP_LOGE(TAG, "Failed to send polling command");
        return ESP_FAIL;
    }

    // 等待发送完成
    uart_wait_tx_done(config_.uart_port, pdMS_TO_TICKS(20));

    return ESP_OK;
}

bool NfcDriver::ReadCard(nfc_card_info_t& card_info, uint32_t timeout_ms) {
    if (!initialized_) {
        ESP_LOGE(TAG, "NFC driver not initialized");
        return false;
    }

    uint8_t rx_buffer[256];
    int len = uart_read_bytes(config_.uart_port, rx_buffer, sizeof(rx_buffer), pdMS_TO_TICKS(timeout_ms));

    if (len <= 0) {
        return false;
    }

    return ParseResponse(rx_buffer, len, card_info);
}

bool NfcDriver::PollAndRead(nfc_card_info_t& card_info, uint32_t timeout_ms) {
    esp_err_t ret = SendPollingCommand();
    if (ret != ESP_OK) {
        return false;
    }

    return ReadCard(card_info, timeout_ms);
}

bool NfcDriver::ParseResponse(const uint8_t* data, int length, nfc_card_info_t& card_info) {
    // 检查最小长度
    if (length < NFC_NO_CARD_RESPONSE_LEN) {
        return false;
    }

    // 检查响应头
    if (memcmp(data, NFC_RESPONSE_HEADER, sizeof(NFC_RESPONSE_HEADER)) != 0) {
        ESP_LOGW(TAG, "Invalid response header");
        return false;
    }

    // 检查是否为无卡响应
    // 无卡响应: 43 4D 74 03 03 00 01 00 30 31
    if (length == NFC_NO_CARD_RESPONSE_LEN && data[6] == 0x01) {
        return false;  // 无卡
    }

    // 有卡响应检查
    if (length < NFC_CARD_RESPONSE_MIN_LEN) {
        return false;
    }

    // 获取卡号长度
    uint8_t id_len = data[NFC_CARD_ID_LEN_INDEX] + 1;
    
    // 验证数据长度
    if (length < NFC_CARD_ID_START_INDEX + id_len) {
        ESP_LOGW(TAG, "Response too short for card ID");
        return false;
    }

    // 提取卡号
    card_info.card_id.clear();
    for (int i = 0; i < id_len; i++) {
        card_info.card_id.push_back(data[NFC_CARD_ID_START_INDEX + i]);
    }
    card_info.id_length = id_len;
    card_info.card_id_hex = BytesToHexString(data + NFC_CARD_ID_START_INDEX, id_len);
    card_info.timestamp = (uint32_t)(esp_timer_get_time() / 1000);

    ESP_LOGI(TAG, "Card detected: %s (length: %d)", card_info.card_id_hex.c_str(), id_len);
    return true;
}

std::string NfcDriver::BytesToHexString(const uint8_t* data, uint8_t length) {
    std::string result;
    result.reserve(length * 2);
    
    const char hex_chars[] = "0123456789ABCDEF";
    for (int i = 0; i < length; i++) {
        result += hex_chars[(data[i] >> 4) & 0x0F];
        result += hex_chars[data[i] & 0x0F];
    }
    
    return result;
}

esp_err_t NfcDriver::StartContinuousMode(uint32_t interval_ms) {
    if (!initialized_) {
        ESP_LOGE(TAG, "NFC driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (continuous_running_.load()) {
        ESP_LOGW(TAG, "Continuous mode already running");
        return ESP_OK;
    }

    continuous_interval_ms_ = interval_ms;
    continuous_running_.store(true);

    // 创建连续读卡任务
    BaseType_t ret = xTaskCreate(
        ContinuousTaskFunc,
        "nfc_continuous",
        4096,
        this,
        5,
        &continuous_task_
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create continuous task");
        continuous_running_.store(false);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Continuous mode started, interval: %lu ms", interval_ms);
    return ESP_OK;
}

void NfcDriver::StopContinuousMode() {
    if (!continuous_running_.load()) {
        return;
    }

    ESP_LOGI(TAG, "Stopping continuous mode");
    continuous_running_.store(false);

    // 等待任务退出
    if (continuous_task_ != nullptr) {
        // 给任务一些时间来完成当前循环
        vTaskDelay(pdMS_TO_TICKS(continuous_interval_ms_ + 100));
        continuous_task_ = nullptr;
    }

    // 清除上一次卡片记录
    last_card_id_.clear();
    last_card_time_ = 0;
}

void NfcDriver::ContinuousTaskFunc(void* arg) {
    NfcDriver* driver = static_cast<NfcDriver*>(arg);
    
    ESP_LOGI(TAG, "Continuous task started");

    while (driver->continuous_running_.load()) {
        nfc_card_info_t card_info;
        
        if (driver->PollAndRead(card_info, 100)) {
            // 检测到卡片
            uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
            
            // 去重检查：如果是同一张卡且在去重时间窗口内，跳过
            bool is_duplicate = (card_info.card_id_hex == driver->last_card_id_) &&
                               (now - driver->last_card_time_ < CARD_DEBOUNCE_TIME_MS);

            if (!is_duplicate) {
                // 更新上一次卡片记录
                driver->last_card_id_ = card_info.card_id_hex;
                driver->last_card_time_ = now;

                // 触发回调
                if (driver->card_callback_) {
                    driver->card_callback_(NFC_EVENT_CARD_DETECTED, card_info);
                }
            }
        } else {
            // 无卡检测 - 如果之前有卡，现在没卡，则触发移除事件
            if (!driver->last_card_id_.empty()) {
                uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
                // 如果超过去重时间窗口后仍然没有检测到卡片，认为卡片已移除
                if (now - driver->last_card_time_ > CARD_DEBOUNCE_TIME_MS * 2) {
                    nfc_card_info_t empty_info;
                    empty_info.card_id_hex = driver->last_card_id_;
                    empty_info.timestamp = now;
                    
                    if (driver->card_callback_) {
                        driver->card_callback_(NFC_EVENT_CARD_REMOVED, empty_info);
                    }
                    
                    driver->last_card_id_.clear();
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(driver->continuous_interval_ms_));
    }

    ESP_LOGI(TAG, "Continuous task exiting");
    vTaskDelete(NULL);
}

