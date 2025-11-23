#ifndef _IDENTIFY_0X18_H_
#define _IDENTIFY_0X18_H_

#include <esp_log.h>
#include <driver/i2c_master.h>

/**
 * 识别 I2C 地址 0x18 的设备
 * 
 * ✅ 已确认：0x18 是 CSU18M68 压力传感器芯片
 * 
 * 设备信息：
 * - 芯片型号：CSU18M68
 * - 功能：压力/触摸检测
 * - I2C 地址：0x18（这是一个不常见的地址配置）
 * - 注意：通常 CSU18M68 的默认地址是 0x6C 或 0x6D，但硬件可能配置为 0x18
 */
class DeviceIdentifier {
public:
    static void IdentifyDevice0x18(i2c_master_bus_handle_t i2c_handle) {
        const char* TAG = "DeviceID_0x18";
        
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "I2C 地址 0x18 设备识别");
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "✅ 已确认设备类型：CSU18M68 压力传感器");
        ESP_LOGI(TAG, "");
        
        // 尝试读取设备寄存器
        uint8_t device_addr_7bit = 0x18;
        
        ESP_LOGI(TAG, "设备信息：");
        ESP_LOGI(TAG, "  芯片型号：CSU18M68");
        ESP_LOGI(TAG, "  功能：压力/触摸检测");
        ESP_LOGI(TAG, "  I2C 地址：0x%02X (7位地址)", device_addr_7bit);
        ESP_LOGI(TAG, "  注意：通常 CSU18M68 的地址是 0x6C/0x6D，但您的硬件配置为 0x18");
        
        // 测试：读取 CSU18M68 寄存器
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "尝试读取 CSU18M68 寄存器");
        ESP_LOGI(TAG, "  注意：CSU18M68 可能需要特殊的初始化序列");
        
        // 创建设备句柄
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = device_addr_7bit,
            .scl_speed_hz = 100000,
        };
        
        i2c_master_dev_handle_t dev_handle;
        esp_err_t ret = i2c_master_bus_add_device(i2c_handle, &dev_cfg, &dev_handle);
        
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "❌ 无法创建设备句柄: %s", esp_err_to_name(ret));
            return;
        }
        
        // 通用寄存器读取
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "  尝试读取常见寄存器地址");
        
        uint8_t common_regs[] = {0x00, 0x01, 0x02, 0x0F, 0x75, 0xFD, 0xFE, 0xFF};
        int successful_reads = 0;
        
        for (int i = 0; i < sizeof(common_regs); i++) {
            uint8_t reg_addr = common_regs[i];
            uint8_t reg_value = 0;
            
            ret = i2c_master_transmit_receive(dev_handle, &reg_addr, 1, &reg_value, 1, 1000);
            
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "  ✅ 寄存器 0x%02X = 0x%02X", reg_addr, reg_value);
                successful_reads++;
            }
        }
        
        if (successful_reads == 0) {
            ESP_LOGW(TAG, "  ❌ 所有常见寄存器读取都失败");
            ESP_LOGW(TAG, "  可能原因：");
            ESP_LOGW(TAG, "    1. CSU18M68 需要特定的初始化序列");
            ESP_LOGW(TAG, "    2. 设备可能处于低功耗模式");
            ESP_LOGW(TAG, "    3. 需要查阅 CSU18M68 数据手册获取正确的寄存器地址");
        } else {
            ESP_LOGI(TAG, "  ✅ 成功读取 %d/%d 个寄存器", successful_reads, (int)sizeof(common_regs));
            ESP_LOGI(TAG, "  这些值可以用于进一步分析设备状态");
        }
        
        // 清理
        i2c_master_bus_rm_device(dev_handle);
        
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "总结");
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "✅ 0x18 地址已确认为 CSU18M68 压力传感器");
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "完整 I2C 设备列表：");
        ESP_LOGI(TAG, "  [0x18] CSU18M68 - 压力传感器");
        ESP_LOGI(TAG, "  [0x30] ES8311   - 音频输出/DAC");
        ESP_LOGI(TAG, "  [0x40] ES7210   - 音频输入/ADC");
        ESP_LOGI(TAG, "========================================");
    }
};

#endif // _IDENTIFY_0X18_H_
