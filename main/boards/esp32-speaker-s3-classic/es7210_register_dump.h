#ifndef _ES7210_REGISTER_DUMP_H_
#define _ES7210_REGISTER_DUMP_H_

#include <esp_log.h>
<driver/i2c_master.h>
#include <vector>

/**
 * ES7210 寄存器诊断工具
 * 用于读取和显示所有关键寄存器的值
 */
class ES7210RegisterDump {
public:
    /**
     * 读取并显示 ES7210 的所有关键寄存器
     * @param i2c_handle I2C 总线句柄
     * @param es7210_addr ES7210 I2C 地址（7位）
     */
    static void DumpAllRegisters(i2c_master_bus_handle_t i2c_handle, uint8_t es7210_addr) {
        const char* TAG = "ES7210_RegDump";
        
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "╔════════════════════════════════════════════════════╗");
        ESP_LOGI(TAG, "║   ES7210 寄存器完整转储                          ║");
        ESP_LOGI(TAG, "╚════════════════════════════════════════════════════╝");
        ESP_LOGI(TAG, "I2C 地址: 0x%02X", es7210_addr);
        ESP_LOGI(TAG, "");
        
        // 创建 I2C 设备句柄
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = es7210_addr,
            .scl_speed_hz = 100000,
        };
        
        i2c_master_dev_handle_t dev_handle;
        esp_err_t ret = i2c_master_bus_add_device(i2c_handle, &dev_cfg, &dev_handle);
        
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "❌ 无法创建 I2C 设备句柄: %s", esp_err_to_name(ret));
            return;
        }
        
        // ES7210 关键寄存器列表（根据数据手册）
        struct RegisterInfo {
            uint8_t addr;
            const char* name;
            const char* description;
        };
        
        std::vector<RegisterInfo> registers = {
            // 系统寄存器
            {0x00, "CHIP_ID",        "芯片ID（应为0x32或0x41）"},
            {0x01, "RESET",          "复位控制"},
            {0x02, "CLK_ON_OFF",     "时钟使能控制"},
            {0x03, "CLK_DIV",        "时钟分频器"},
            {0x04, "MODE_CFG",       "模式配置（Master/Slave, I2S/TDM）"},
            {0x05, "SDP_CFG1",       "串行数据端口配置1"},
            {0x06, "SDP_CFG2",       "串行数据端口配置2"},
            
            // 电源和偏置
            {0x07, "PWR_CTRL2",      "电源控制2（MICBIAS）"},
            {0x08, "PWR_CTRL1",       "电源控制1（ADC使能）"},
            {0x09, "MIC_EN",         "麦克风通道使能"},
            
            // 增益控制
            {0x10, "MIC1_GAIN",      "MIC1 增益"},
            {0x11, "MIC2_GAIN",      "MIC2 增益"},
            {0x12, "MIC3_GAIN",      "MIC3 增益"},
            {0x13, "MIC4_GAIN",      "MIC4 增益"},
            
            // ADC 配置
            {0x14, "ADC_OSR",        "ADC 过采样率"},
            {0x15, "ADC_HPF",        "ADC 高通滤波器"},
            
            // TDM 配置
            {0x1A, "TDM_CTRL",       "TDM 控制"},
            {0x1B, "TDM_SLOT",       "TDM 时间槽配置"},
        };
        
        ESP_LOGI(TAG, "开始读取寄存器...");
        ESP_LOGI(TAG, "┌──────┬──────────────┬──────┬─────────────────────────────┐");
        ESP_LOGI(TAG, "│ 地址 │ 名称         │ 值   │ 说明                        │");
        ESP_LOGI(TAG, "├──────┼──────────────┼──────┼─────────────────────────────┤");
        
        bool has_error = false;
        
        for (const auto& reg : registers) {
            uint8_t value = 0;
            uint8_t reg_addr = reg.addr;
            
            ret = i2c_master_transmit_receive(dev_handle, &reg_addr, 1, &value, 1, 1000);
            
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "│ 0x%02X │ %-12s │ 0x%02X │ %s", 
                         reg.addr, reg.name, value, reg.description);
                
                // 特殊检查
                if (reg.addr == 0x00) {  // CHIP_ID
                    if (value != 0x32 && value != 0x41) {
                        ESP_LOGW(TAG, "│      │              │      │ ⚠️ 芯片ID异常！           │");
                        has_error = true;
                    }
                }
                
                if (reg.addr == 0x08) {  // PWR_CTRL1
                    if (value == 0xFF) {
                        ESP_LOGW(TAG, "│      │              │      │ ⚠️ ADC 全部关闭！         │");
                        has_error = true;
                    }
                }
                
                if (reg.addr == 0x09) {  // MIC_EN
                    if (value == 0x0F) {
                        ESP_LOGW(TAG, "│      │              │      │ ⚠️ 所有MIC通道禁用！      │");
                        has_error = true;
                    }
                }
                
            } else {
                ESP_LOGE(TAG, "│ 0x%02X │ %-12s │ ERR  │ 读取失败: %s", 
                         reg.addr, reg.name, esp_err_to_name(ret));
                has_error = true;
            }
        }
        
        ESP_LOGI(TAG, "└──────┴──────────────┴──────┴─────────────────────────────┘");
        ESP_LOGI(TAG, "");
        
        if (has_error) {
            ESP_LOGW(TAG, "⚠️  检测到配置异常，请参考上述警告信息");
        } else {
            ESP_LOGI(TAG, "✅ 寄存器读取完成，未发现明显配置错误");
        }
        
        // 额外诊断：检查关键寄存器的预期值
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "【关键寄存器诊断】");
        ESP_LOGI(TAG, "─────────────────────────────────────────────────");
        
        CheckRegister(dev_handle, 0x00, "CHIP_ID", 
                     "应为 0x32 或 0x41", nullptr);
        
        CheckRegister(dev_handle, 0x04, "MODE_CFG", 
                     "bit3=0（Slave模式）", 
                     [](uint8_t val) { return (val & 0x08) == 0; });
        
        CheckRegister(dev_handle, 0x07, "PWR_CTRL2 (MICBIAS)", 
                     "bit0=1（禁用内部BIAS）", 
                     [](uint8_t val) { return (val & 0x01) == 0x01; });
        
        CheckRegister(dev_handle, 0x08, "PWR_CTRL1 (ADC使能)", 
                     "应为 0x00（所有ADC使能）", 
                     [](uint8_t val) { return val == 0x00; });
        
        CheckRegister(dev_handle, 0x09, "MIC_EN", 
                     "应为 0x00（所有MIC使能）", 
                     [](uint8_t val) { return val == 0x00; });
        
        ESP_LOGI(TAG, "");
        
        // 清理
        i2c_master_bus_rm_device(dev_handle);
    }
    
private:
    /**
     * 检查单个寄存器值是否符合预期
     */
    static void CheckRegister(i2c_master_dev_handle_t dev_handle, 
                             uint8_t reg_addr, 
                             const char* name,
                             const char* expected_desc,
                             bool (*validator)(uint8_t)) {
        const char* TAG = "ES7210_RegDump";
        
        uint8_t value = 0;
        esp_err_t ret = i2c_master_transmit_receive(dev_handle, &reg_addr, 1, &value, 1, 1000);
        
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "  ❌ 0x%02X %-20s : 读取失败", reg_addr, name);
            return;
        }
        
        bool is_valid = (validator == nullptr) || validator(value);
        
        if (is_valid) {
            ESP_LOGI(TAG, "  ✅ 0x%02X %-20s = 0x%02X  (%s)", 
                     reg_addr, name, value, expected_desc);
        } else {
            ESP_LOGW(TAG, "  ⚠️  0x%02X %-20s = 0x%02X  (预期: %s)", 
                     reg_addr, name, value, expected_desc);
        }
    }
};

#endif // _ES7210_REGISTER_DUMP_H_

