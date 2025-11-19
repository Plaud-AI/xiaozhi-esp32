#include "nfc_reader.h"

#ifdef CONFIG_ENABLE_DOLL_INTERACTION

#include <esp_log.h>

#define TAG "NfcReader"

NfcReader::~NfcReader() {
    // 默认析构函数
}

bool NfcReader::Initialize() {
    ESP_LOGI(TAG, "Initializing NFC Reader (simulation mode)");

    // TODO: 硬件到位后实现
    // 1. 初始化 I2C/SPI 接口
    // 2. 配置 NFC 芯片（PN532/MFRC522）
    // 3. 检测芯片是否正常响应

    ESP_LOGI(TAG, "NFC Reader initialized (simulation)");
    return true;
}

bool NfcReader::ReadTag(std::string& tag_id) {
    // TODO: 硬件到位后实现
    // 1. 检测卡片是否存在
    // 2. 读取卡片 UID
    // 3. 可选：读取自定义数据块
    // 4. 返回唯一标识符

    // 模拟模式：返回空字符串，表示没有检测到标签
    ESP_LOGD(TAG, "ReadTag called (simulation - no tag detected)");
    return false;
}

bool NfcReader::IsTagPresent() {
    // TODO: 硬件到位后实现
    // 快速检测是否有卡片在感应区

    // 模拟模式：返回 false
    return false;
}

#endif // CONFIG_ENABLE_DOLL_INTERACTION

