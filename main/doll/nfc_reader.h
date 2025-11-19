#ifndef NFC_READER_H
#define NFC_READER_H

#include <string>

#ifdef CONFIG_ENABLE_DOLL_INTERACTION

/**
 * @brief NFC 读取器抽象接口
 * 
 * 支持多种 NFC 芯片（PN532, MFRC522 等）
 * 当前为模拟实现，硬件到位后替换为实际驱动
 */
class NfcReader {
public:
    NfcReader() = default;
    virtual ~NfcReader();

    // 初始化 NFC 硬件
    virtual bool Initialize();

    // 读取 NFC 标签（返回 UID 或自定义 ID）
    // @param[out] tag_id 读取到的标签 ID
    // @return true 成功，false 失败
    virtual bool ReadTag(std::string& tag_id);

    // 检测是否有标签存在
    virtual bool IsTagPresent();

    // 写入 NFC 标签（可选功能）
    // TODO: 后续如果需要写入功能，可以添加
    // virtual bool WriteTag(const std::string& data);

private:
    // TODO: 添加具体 NFC 芯片的私有成员
    // 例如：I2C/SPI 句柄、GPIO 配置等
};

#endif // CONFIG_ENABLE_DOLL_INTERACTION

#endif // NFC_READER_H

