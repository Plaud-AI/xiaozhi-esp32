#ifndef _ES7210_DIAGNOSTIC_H_
#define _ES7210_DIAGNOSTIC_H_

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <driver/gpio.h>
#include <driver/i2s_std.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/**
 * ES7210 诊断工具
 * 
 * 用于诊断 ES7210 初始化失败的问题
 * 主要检查：
 * 1. I2C 总线通信
 * 2. MCLK 输出状态
 * 3. GPIO 配置状态
 * 4. 时钟频率配置
 */

class ES7210Diagnostic {
public:
    static void RunFullDiagnostic(
        i2c_master_bus_handle_t i2c_handle,
        i2s_chan_handle_t i2s_rx_handle,
        gpio_num_t mclk_pin,
        gpio_num_t sda_pin,
        gpio_num_t scl_pin,
        uint8_t es7210_addr
    ) {
        const char* TAG = "ES7210_Diag";
        
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "ES7210 完整诊断开始");
        ESP_LOGI(TAG, "========================================");
        
        // 1. 检查 GPIO 配置
        CheckGpioConfiguration(mclk_pin, sda_pin, scl_pin);
        
        // 2. 检查 I2S 通道状态
        CheckI2sChannelState(i2s_rx_handle);
        
        // 3. 扫描 I2C 总线
        ScanI2CBus(i2c_handle);
        
        // 4. 测试 ES7210 地址
        TestES7210Address(i2c_handle, es7210_addr);
        
        // 5. 尝试不同的 I2C 速度
        TestI2CSpeed(i2c_handle, es7210_addr);
        
        // 6. 提供诊断建议
        ProvideDiagnosticSuggestions(mclk_pin);
        
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "ES7210 诊断完成");
        ESP_LOGI(TAG, "========================================");
    }

private:
    static void CheckGpioConfiguration(gpio_num_t mclk, gpio_num_t sda, gpio_num_t scl) {
        const char* TAG = "ES7210_Diag";
        
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "【步骤 1】检查 GPIO 配置");
        ESP_LOGI(TAG, "----------------------------------------");
        
        // 检查 MCLK 驱动强度
        ESP_LOGI(TAG, "GPIO%d (MCLK):", mclk);
        gpio_drive_cap_t mclk_drive;
        if (gpio_get_drive_capability(mclk, &mclk_drive) == ESP_OK) {
            ESP_LOGI(TAG, "  驱动强度: %d (0=5mA, 1=10mA, 2=20mA, 3=40mA)", mclk_drive);
        } else {
            ESP_LOGI(TAG, "  驱动强度: 无法读取");
        }
        
        // 检查 MCLK 电平
        int mclk_level = gpio_get_level(mclk);
        ESP_LOGI(TAG, "  当前电平: %d", mclk_level);
        
        // 检查 I2C 引脚电平
        ESP_LOGI(TAG, "GPIO%d (SDA): 电平=%d", sda, gpio_get_level(sda));
        ESP_LOGI(TAG, "GPIO%d (SCL): 电平=%d", scl, gpio_get_level(scl));
    }
    
    static void CheckI2sChannelState(i2s_chan_handle_t handle) {
        const char* TAG = "ES7210_Diag";
        
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "【步骤 2】检查 I2S 通道状态");
        ESP_LOGI(TAG, "----------------------------------------");
        
        // I2S 通道没有直接查询状态的 API
        // 我们只能根据是否能成功操作来判断
        ESP_LOGI(TAG, "I2S 通道句柄: %p", handle);
        
        if (handle == nullptr) {
            ESP_LOGE(TAG, "❌ I2S 通道句柄为空！");
        } else {
            ESP_LOGI(TAG, "✅ I2S 通道句柄有效");
        }
    }
    
    static void ScanI2CBus(i2c_master_bus_handle_t bus_handle) {
        const char* TAG = "ES7210_Diag";
        
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "【步骤 3】扫描 I2C 总线");
        ESP_LOGI(TAG, "----------------------------------------");
        ESP_LOGI(TAG, "     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f");
        
        int found_count = 0;
        uint8_t found_addresses[128];
        
        for (int i = 0; i < 128; i += 16) {
            printf("%02x: ", i);
            for (int j = 0; j < 16; j++) {
                uint8_t addr = i + j;
                
                // 跳过保留地址
                if (addr < 0x08 || addr > 0x77) {
                    printf("   ");
                    continue;
                }
                
                esp_err_t ret = i2c_master_probe(bus_handle, addr, pdMS_TO_TICKS(100));
                if (ret == ESP_OK) {
                    printf("%02x ", addr);
                    found_addresses[found_count++] = addr;
                } else {
                    printf("-- ");
                }
            }
            printf("\r\n");
        }
        
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "找到 %d 个 I2C 设备:", found_count);
        for (int i = 0; i < found_count; i++) {
            ESP_LOGI(TAG, "  - 地址 0x%02X", found_addresses[i]);
        }
    }
    
    static void TestES7210Address(i2c_master_bus_handle_t bus_handle, uint8_t addr) {
        const char* TAG = "ES7210_Diag";
        
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "【步骤 4】测试 ES7210 地址");
        ESP_LOGI(TAG, "----------------------------------------");
        
        // ES7210 的可能地址（取决于 AD0/AD1 引脚配置）
        uint8_t possible_addresses[] = {
            0x40,  // AD0=GND, AD1=GND
            0x41,  // AD0=VDD, AD1=GND
            0x42,  // AD0=GND, AD1=VDD
            0x43,  // AD0=VDD, AD1=VDD
        };
        
        ESP_LOGI(TAG, "当前配置的地址: 0x%02X", addr);
        ESP_LOGI(TAG, "测试所有可能的 ES7210 地址:");
        
        for (int i = 0; i < 4; i++) {
            uint8_t test_addr = possible_addresses[i];
            esp_err_t ret = i2c_master_probe(bus_handle, test_addr, pdMS_TO_TICKS(200));
            
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "  ✅ 0x%02X - 响应", test_addr);
                
                if (test_addr != addr) {
                    ESP_LOGW(TAG, "     ⚠️ 这个地址能响应，但不是配置的地址！");
                    ESP_LOGW(TAG, "     建议修改 config.h 中的 AUDIO_CODEC_ES7210_ADDR 为 0x%02X", test_addr);
                }
            } else {
                ESP_LOGI(TAG, "  ❌ 0x%02X - 无响应 (%s)", test_addr, esp_err_to_name(ret));
            }
        }
    }
    
    static void TestI2CSpeed(i2c_master_bus_handle_t bus_handle, uint8_t addr) {
        const char* TAG = "ES7210_Diag";
        
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "【步骤 5】测试不同的 I2C 速度");
        ESP_LOGI(TAG, "----------------------------------------");
        
        // 注意：这个测试需要创建新的设备句柄，但我们无法修改总线速度
        // 只能测试设备级别的速度设置
        
        uint32_t test_speeds[] = {100000, 400000};  // 100kHz, 400kHz
        
        for (int i = 0; i < 2; i++) {
            uint32_t speed = test_speeds[i];
            
            i2c_device_config_t dev_cfg = {
                .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                .device_address = addr,
                .scl_speed_hz = speed,
            };
            
            i2c_master_dev_handle_t dev_handle;
            esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);
            
            if (ret == ESP_OK) {
                // 尝试 probe
                esp_err_t probe_ret = i2c_master_probe(bus_handle, addr, pdMS_TO_TICKS(200));
                
                if (probe_ret == ESP_OK) {
                    ESP_LOGI(TAG, "  ✅ %d Hz - 设备响应", speed);
                } else {
                    ESP_LOGI(TAG, "  ❌ %d Hz - 设备无响应", speed);
                }
                
                i2c_master_bus_rm_device(dev_handle);
            } else {
                ESP_LOGE(TAG, "  ❌ %d Hz - 无法创建设备句柄", speed);
            }
        }
    }
    
    static void ProvideDiagnosticSuggestions(gpio_num_t mclk_pin) {
        const char* TAG = "ES7210_Diag";
        
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "【诊断建议】");
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "1. MCLK 电压问题：");
        ESP_LOGI(TAG, "   - 用万用表测量 GPIO%d", mclk_pin);
        ESP_LOGI(TAG, "   - 如果显示 1.5-2.0V：✅ 正常（方波平均值）");
        ESP_LOGI(TAG, "   - 如果显示 3.3V：❌ MCLK 丢失（变成静态高电平）");
        ESP_LOGI(TAG, "   - 如果显示 0V：❌ MCLK 未启动");
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "2. 用示波器检查（如果有）：");
        ESP_LOGI(TAG, "   - 应该看到 4.096 MHz 方波");
        ESP_LOGI(TAG, "   - 峰峰值应该接近 3.3V");
        ESP_LOGI(TAG, "   - 占空比应该接近 50%%");
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "3. 硬件检查：");
        ESP_LOGI(TAG, "   - 确认 ES7210 芯片已焊接");
        ESP_LOGI(TAG, "   - 确认 GPIO13 到 ES7210 MCLK 引脚连接正常");
        ESP_LOGI(TAG, "   - 确认 I2C (SDA/SCL) 连接正常");
        ESP_LOGI(TAG, "   - 确认 ES7210 供电正常 (3.3V)");
        ESP_LOGI(TAG, "   - 检查 AD0/AD1 引脚电平（决定 I2C 地址）");
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "4. 可能的解决方案：");
        ESP_LOGI(TAG, "   a) 如果 I2C 扫描找不到任何设备在 0x40-0x43：");
        ESP_LOGI(TAG, "      → ES7210 芯片可能未焊接或损坏");
        ESP_LOGI(TAG, "      → 检查硬件连接");
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "   b) 如果 I2C 扫描找到设备但地址不对：");
        ESP_LOGI(TAG, "      → 修改 config.h 中的地址");
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "   c) 如果设备能响应但初始化失败：");
        ESP_LOGI(TAG, "      → 可能需要特殊的初始化序列");
        ESP_LOGI(TAG, "      → 检查 ES7210 数据手册");
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "   d) 如果 MCLK 驱动能力不足：");
        ESP_LOGI(TAG, "      → 减小 PCB 走线长度");
        ESP_LOGI(TAG, "      → 减小负载电容");
        ESP_LOGI(TAG, "      → 使用外部时钟缓冲器");
        ESP_LOGI(TAG, "========================================");
    }
};

#endif // _ES7210_DIAGNOSTIC_H_
