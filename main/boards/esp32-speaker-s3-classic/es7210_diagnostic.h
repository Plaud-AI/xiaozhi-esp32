#ifndef ES7210_DIAGNOSTIC_H
#define ES7210_DIAGNOSTIC_H

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define ES7210_DIAG_TAG "ES7210_Diag"

/**
 * ES7210 寄存器地址
 */
#define ES7210_RESET_REG        0x00
#define ES7210_CLOCK_OFF_REG    0x01
#define ES7210_MAINCLK_REG      0x02
#define ES7210_MASTER_CLK_REG   0x03
#define ES7210_LRCK_DIVH_REG    0x04
#define ES7210_LRCK_DIVL_REG    0x05
#define ES7210_POWER_DOWN_REG   0x06
#define ES7210_OSR_REG          0x07
#define ES7210_MODE_CONFIG_REG  0x08
#define ES7210_TIME_CONTROL0_REG 0x09
#define ES7210_TIME_CONTROL1_REG 0x0A
#define ES7210_SDP_INTERFACE1_REG 0x11
#define ES7210_SDP_INTERFACE2_REG 0x12

/**
 * ES7210 诊断结果
 */
struct ES7210DiagResult {
    bool i2c_address_detected;      // I2C 地址检测
    bool mclk_present;              // MCLK 是否存在（软件层面）
    bool register_read_success;     // 寄存器读取成功
    bool register_write_success;    // 寄存器写入成功
    uint8_t chip_id;                // 芯片 ID（如果能读取）
    int error_count;                // 错误计数
};

/**
 * ES7210 详细诊断
 * 
 * @param i2c_bus I2C 总线句柄
 * @param device_addr ES7210 I2C 地址（7-bit）
 * @return 诊断结果
 */
inline ES7210DiagResult DiagnoseES7210(i2c_master_bus_handle_t i2c_bus, uint8_t device_addr) {
    ES7210DiagResult result = {0};
    
    ESP_LOGI(ES7210_DIAG_TAG, "========================================");
    ESP_LOGI(ES7210_DIAG_TAG, "开始 ES7210 详细诊断");
    ESP_LOGI(ES7210_DIAG_TAG, "目标地址: 0x%02x", device_addr);
    ESP_LOGI(ES7210_DIAG_TAG, "========================================");
    
    // 1. 测试 I2C 地址检测
    ESP_LOGI(ES7210_DIAG_TAG, "");
    ESP_LOGI(ES7210_DIAG_TAG, "1️⃣ 测试 I2C 地址检测...");
    i2c_master_dev_handle_t dev_handle;
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = device_addr,
        .scl_speed_hz = 100000,  // 降低速度以提高稳定性
    };
    
    esp_err_t ret = i2c_master_bus_add_device(i2c_bus, &dev_cfg, &dev_handle);
    if (ret == ESP_OK) {
        ESP_LOGI(ES7210_DIAG_TAG, "   ✅ I2C 地址 0x%02x 响应正常", device_addr);
        result.i2c_address_detected = true;
        
        // 2. 测试寄存器读取
        ESP_LOGI(ES7210_DIAG_TAG, "");
        ESP_LOGI(ES7210_DIAG_TAG, "2️⃣ 测试寄存器读取...");
        
        // 尝试读取几个关键寄存器
        uint8_t test_registers[] = {
            ES7210_RESET_REG,
            ES7210_CLOCK_OFF_REG,
            ES7210_MAINCLK_REG,
            ES7210_MODE_CONFIG_REG
        };
        
        int read_success_count = 0;
        for (int i = 0; i < sizeof(test_registers); i++) {
            uint8_t reg_addr = test_registers[i];
            uint8_t reg_value = 0;
            
            ret = i2c_master_transmit_receive(dev_handle, &reg_addr, 1, &reg_value, 1, 1000);
            if (ret == ESP_OK) {
                ESP_LOGI(ES7210_DIAG_TAG, "   ✅ 寄存器 0x%02x 读取成功: 0x%02x", reg_addr, reg_value);
                read_success_count++;
            } else {
                ESP_LOGE(ES7210_DIAG_TAG, "   ❌ 寄存器 0x%02x 读取失败: %s", reg_addr, esp_err_to_name(ret));
                result.error_count++;
            }
            vTaskDelay(pdMS_TO_TICKS(10));  // 短暂延迟
        }
        
        result.register_read_success = (read_success_count > 0);
        
        // 3. 测试寄存器写入
        ESP_LOGI(ES7210_DIAG_TAG, "");
        ESP_LOGI(ES7210_DIAG_TAG, "3️⃣ 测试寄存器写入...");
        
        // 尝试写入一个安全的寄存器（时钟关闭寄存器）
        uint8_t write_data[2] = {ES7210_CLOCK_OFF_REG, 0x00};
        ret = i2c_master_transmit(dev_handle, write_data, 2, 1000);
        if (ret == ESP_OK) {
            ESP_LOGI(ES7210_DIAG_TAG, "   ✅ 寄存器写入成功");
            result.register_write_success = true;
            
            // 回读验证
            uint8_t read_back = 0;
            ret = i2c_master_transmit_receive(dev_handle, &write_data[0], 1, &read_back, 1, 1000);
            if (ret == ESP_OK) {
                ESP_LOGI(ES7210_DIAG_TAG, "   ✅ 写入验证: 写入 0x%02x, 读回 0x%02x", write_data[1], read_back);
            }
        } else {
            ESP_LOGE(ES7210_DIAG_TAG, "   ❌ 寄存器写入失败: %s", esp_err_to_name(ret));
            result.register_write_success = false;
            result.error_count++;
        }
        
        // 4. 尝试软件复位
        ESP_LOGI(ES7210_DIAG_TAG, "");
        ESP_LOGI(ES7210_DIAG_TAG, "4️⃣ 尝试软件复位...");
        uint8_t reset_data[2] = {ES7210_RESET_REG, 0xFF};  // 复位命令
        ret = i2c_master_transmit(dev_handle, reset_data, 2, 1000);
        if (ret == ESP_OK) {
            ESP_LOGI(ES7210_DIAG_TAG, "   ✅ 软件复位命令发送成功");
            vTaskDelay(pdMS_TO_TICKS(100));  // 等待复位完成
            
            // 复位后再次尝试读取
            uint8_t reg_addr = ES7210_RESET_REG;
            uint8_t reg_value = 0;
            ret = i2c_master_transmit_receive(dev_handle, &reg_addr, 1, &reg_value, 1, 1000);
            if (ret == ESP_OK) {
                ESP_LOGI(ES7210_DIAG_TAG, "   ✅ 复位后寄存器可读: 0x%02x", reg_value);
            } else {
                ESP_LOGE(ES7210_DIAG_TAG, "   ❌ 复位后寄存器读取失败");
            }
        } else {
            ESP_LOGE(ES7210_DIAG_TAG, "   ❌ 软件复位失败: %s", esp_err_to_name(ret));
        }
        
        i2c_master_bus_rm_device(dev_handle);
    } else {
        ESP_LOGE(ES7210_DIAG_TAG, "   ❌ I2C 地址 0x%02x 无响应: %s", device_addr, esp_err_to_name(ret));
        result.i2c_address_detected = false;
    }
    
    // 5. 诊断总结
    ESP_LOGI(ES7210_DIAG_TAG, "");
    ESP_LOGI(ES7210_DIAG_TAG, "========================================");
    ESP_LOGI(ES7210_DIAG_TAG, "诊断结果总结");
    ESP_LOGI(ES7210_DIAG_TAG, "========================================");
    ESP_LOGI(ES7210_DIAG_TAG, "I2C 地址检测: %s", result.i2c_address_detected ? "✅ 正常" : "❌ 失败");
    ESP_LOGI(ES7210_DIAG_TAG, "寄存器读取:   %s", result.register_read_success ? "✅ 正常" : "❌ 失败");
    ESP_LOGI(ES7210_DIAG_TAG, "寄存器写入:   %s", result.register_write_success ? "✅ 正常" : "❌ 失败");
    ESP_LOGI(ES7210_DIAG_TAG, "错误计数:     %d", result.error_count);
    ESP_LOGI(ES7210_DIAG_TAG, "========================================");
    
    // 6. 给出建议
    ESP_LOGI(ES7210_DIAG_TAG, "");
    ESP_LOGI(ES7210_DIAG_TAG, "📋 诊断建议:");
    if (!result.i2c_address_detected) {
        ESP_LOGI(ES7210_DIAG_TAG, "   ⚠️ I2C 地址无响应 → 检查硬件连接和供电");
    } else if (!result.register_read_success) {
        ESP_LOGI(ES7210_DIAG_TAG, "   ⚠️ 地址响应但无法读取寄存器 → 芯片可能损坏或 MCLK 缺失");
    } else if (!result.register_write_success) {
        ESP_LOGI(ES7210_DIAG_TAG, "   ⚠️ 可读但无法写入 → 芯片可能处于保护模式或损坏");
    } else {
        ESP_LOGI(ES7210_DIAG_TAG, "   ✅ I2C 通信正常 → 问题可能在初始化序列");
    }
    ESP_LOGI(ES7210_DIAG_TAG, "");
    
    return result;
}

/**
 * 测试不同的 I2C 速度
 */
inline void TestES7210I2CSpeed(i2c_master_bus_handle_t i2c_bus, uint8_t device_addr) {
    ESP_LOGI(ES7210_DIAG_TAG, "========================================");
    ESP_LOGI(ES7210_DIAG_TAG, "测试不同的 I2C 速度");
    ESP_LOGI(ES7210_DIAG_TAG, "========================================");
    
    uint32_t speeds[] = {50000, 100000, 200000, 400000};  // 50kHz, 100kHz, 200kHz, 400kHz
    
    for (int i = 0; i < sizeof(speeds) / sizeof(speeds[0]); i++) {
        ESP_LOGI(ES7210_DIAG_TAG, "");
        ESP_LOGI(ES7210_DIAG_TAG, "测试速度: %d Hz", speeds[i]);
        
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = device_addr,
            .scl_speed_hz = speeds[i],
        };
        
        i2c_master_dev_handle_t dev_handle;
        esp_err_t ret = i2c_master_bus_add_device(i2c_bus, &dev_cfg, &dev_handle);
        if (ret == ESP_OK) {
            // 尝试读取寄存器
            uint8_t reg_addr = ES7210_RESET_REG;
            uint8_t reg_value = 0;
            ret = i2c_master_transmit_receive(dev_handle, &reg_addr, 1, &reg_value, 1, 1000);
            if (ret == ESP_OK) {
                ESP_LOGI(ES7210_DIAG_TAG, "   ✅ 速度 %d Hz: 读取成功 (值: 0x%02x)", speeds[i], reg_value);
            } else {
                ESP_LOGE(ES7210_DIAG_TAG, "   ❌ 速度 %d Hz: 读取失败 (%s)", speeds[i], esp_err_to_name(ret));
            }
            i2c_master_bus_rm_device(dev_handle);
        } else {
            ESP_LOGE(ES7210_DIAG_TAG, "   ❌ 速度 %d Hz: 无法添加设备", speeds[i]);
        }
        
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    ESP_LOGI(ES7210_DIAG_TAG, "========================================");
}

/**
 * 测试不同的等待时间
 */
inline void TestES7210StabilizationTime(i2c_master_bus_handle_t i2c_bus, uint8_t device_addr, 
                                       void (*start_mclk_func)()) {
    ESP_LOGI(ES7210_DIAG_TAG, "========================================");
    ESP_LOGI(ES7210_DIAG_TAG, "测试不同的 MCLK 稳定时间");
    ESP_LOGI(ES7210_DIAG_TAG, "========================================");
    
    uint32_t delays[] = {10, 50, 100, 200, 500};  // ms
    
    for (int i = 0; i < sizeof(delays) / sizeof(delays[0]); i++) {
        ESP_LOGI(ES7210_DIAG_TAG, "");
        ESP_LOGI(ES7210_DIAG_TAG, "测试延迟: %d ms", delays[i]);
        
        // 重新启动 MCLK
        if (start_mclk_func) {
            start_mclk_func();
        }
        
        // 等待指定时间
        vTaskDelay(pdMS_TO_TICKS(delays[i]));
        
        // 尝试 I2C 通信
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = device_addr,
            .scl_speed_hz = 100000,
        };
        
        i2c_master_dev_handle_t dev_handle;
        esp_err_t ret = i2c_master_bus_add_device(i2c_bus, &dev_cfg, &dev_handle);
        if (ret == ESP_OK) {
            uint8_t reg_addr = ES7210_RESET_REG;
            uint8_t reg_value = 0;
            ret = i2c_master_transmit_receive(dev_handle, &reg_addr, 1, &reg_value, 1, 1000);
            if (ret == ESP_OK) {
                ESP_LOGI(ES7210_DIAG_TAG, "   ✅ 延迟 %d ms: 通信成功", delays[i]);
            } else {
                ESP_LOGE(ES7210_DIAG_TAG, "   ❌ 延迟 %d ms: 通信失败", delays[i]);
            }
            i2c_master_bus_rm_device(dev_handle);
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    ESP_LOGI(ES7210_DIAG_TAG, "========================================");
}

#endif // ES7210_DIAGNOSTIC_H

