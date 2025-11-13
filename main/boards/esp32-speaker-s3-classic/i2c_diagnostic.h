#ifndef _I2C_DIAGNOSTIC_H_
#define _I2C_DIAGNOSTIC_H_

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <driver/gpio.h>

/**
 * I2C 诊断工具
 * 
 * 用于调试 I2C 通信问题，扫描所有可能的地址和引脚组合
 */

#define TAG "I2C_Diagnostic"

// 可能的 I2C 引脚组合（基于 ESP32-S3 常用配置）
struct I2CPinConfig {
    gpio_num_t sda;
    gpio_num_t scl;
    const char* name;
};

static const I2CPinConfig possible_i2c_pins[] = {
    {GPIO_NUM_5, GPIO_NUM_4, "GPIO5/GPIO4 (当前配置)"},
    {GPIO_NUM_1, GPIO_NUM_2, "GPIO1/GPIO2"},
    {GPIO_NUM_6, GPIO_NUM_7, "GPIO6/GPIO7"},
    {GPIO_NUM_8, GPIO_NUM_9, "GPIO8/GPIO9"},
    {GPIO_NUM_41, GPIO_NUM_42, "GPIO41/GPIO42"},
    {GPIO_NUM_15, GPIO_NUM_16, "GPIO15/GPIO16"},
};

// 可能的 I2C 地址（7位地址）
struct I2CDeviceAddr {
    uint8_t addr;
    const char* device;
};

static const I2CDeviceAddr possible_devices[] = {
    {0x18, "ES8311 (AD0=GND)"},
    {0x19, "ES8311 (AD0=VDD)"},
    {0x30, "ES8311 (其他配置)"},
    {0x40, "ES7210 (ADDR=GND)"},
    {0x41, "ES7210 (ADDR=GND, 8位转换)"},
    {0x42, "ES7210 (ADDR=VDD)"},
    {0x43, "ES7210 (ADDR=SDA)"},
    {0x44, "ES7210 (ADDR=SCL)"},
    {0x34, "AXP2101 (电源管理)"},
    {0x38, "触摸屏控制器"},
};

/**
 * 扫描指定 I2C 总线上的所有设备
 */
inline void ScanI2CBus(i2c_master_bus_handle_t bus_handle) {
    ESP_LOGI(TAG, "开始扫描 I2C 总线...");
    ESP_LOGI(TAG, "     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f");
    
    int found_count = 0;
    
    for (int i = 0; i < 128; i += 16) {
        ESP_LOGI(TAG, "%02x: ", i);
        for (int j = 0; j < 16; j++) {
            uint8_t addr = i + j;
            
            // 跳过保留地址
            if (addr < 0x08 || addr > 0x77) {
                printf("   ");
                continue;
            }
            
            // 尝试通信
            i2c_device_config_t dev_cfg = {
                .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                .device_address = addr,
                .scl_speed_hz = 100000,
            };
            
            i2c_master_dev_handle_t dev_handle;
            esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);
            
            if (ret == ESP_OK) {
                // 尝试读取一个字节
                uint8_t dummy;
                ret = i2c_master_transmit_receive(dev_handle, NULL, 0, &dummy, 1, 100);
                
                if (ret == ESP_OK) {
                    printf("%02x ", addr);
                    found_count++;
                    
                    // 检查是否是已知设备
                    for (const auto& device : possible_devices) {
                        if (device.addr == addr) {
                            ESP_LOGI(TAG, "\n    发现设备: %s (地址 0x%02x)", device.device, addr);
                        }
                    }
                } else {
                    printf("-- ");
                }
                
                i2c_master_bus_rm_device(dev_handle);
            } else {
                printf("-- ");
            }
        }
        printf("\n");
    }
    
    ESP_LOGI(TAG, "扫描完成，发现 %d 个设备", found_count);
}

/**
 * 测试指定引脚组合的 I2C 通信
 */
inline void TestI2CPinCombination(gpio_num_t sda, gpio_num_t scl, const char* name) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "测试 I2C 引脚: %s", name);
    ESP_LOGI(TAG, "  SDA = GPIO%d, SCL = GPIO%d", sda, scl);
    ESP_LOGI(TAG, "========================================");
    
    // 配置 I2C 总线
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = scl,
        .sda_io_num = sda,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = true,  // 启用内部上拉
        },
    };
    
    i2c_master_bus_handle_t bus_handle;
    esp_err_t ret = i2c_new_master_bus(&bus_config, &bus_handle);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "无法初始化 I2C 总线: %s", esp_err_to_name(ret));
        return;
    }
    
    // 扫描总线
    ScanI2CBus(bus_handle);
    
    // 清理
    i2c_del_master_bus(bus_handle);
    
    vTaskDelay(pdMS_TO_TICKS(500));
}

/**
 * 运行完整的 I2C 诊断
 */
inline void RunI2CDiagnostic() {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║   ESP32-S3 I2C 诊断工具 v1.0          ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════╝");
    ESP_LOGI(TAG, "");
    
    // 测试所有可能的引脚组合
    for (const auto& pin_config : possible_i2c_pins) {
        TestI2CPinCombination(pin_config.sda, pin_config.scl, pin_config.name);
    }
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║   诊断完成                            ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════╝");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "如果所有引脚组合都没有发现设备，请检查：");
    ESP_LOGI(TAG, "  1. ES8311 和 ES7210 的电源是否正常（3.3V）");
    ESP_LOGI(TAG, "  2. I2C 上拉电阻是否已安装（通常 4.7kΩ）");
    ESP_LOGI(TAG, "  3. 芯片的复位引脚是否处于正常状态");
    ESP_LOGI(TAG, "  4. 使用万用表测量 I2C 引脚的空闲电压（应该是 3.3V）");
}

/**
 * 测试特定设备的寄存器读取
 */
inline void TestDeviceRegisterRead(i2c_master_bus_handle_t bus_handle, uint8_t device_addr, const char* device_name) {
    ESP_LOGI(TAG, "测试 %s (地址 0x%02x) 的寄存器读取...", device_name, device_addr);
    
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = device_addr,
        .scl_speed_hz = 100000,
    };
    
    i2c_master_dev_handle_t dev_handle;
    esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "  ❌ 无法添加设备");
        return;
    }
    
    // 尝试读取芯片 ID 寄存器（假设地址 0x00）
    uint8_t reg_addr = 0x00;
    uint8_t reg_value;
    
    ret = i2c_master_transmit_receive(dev_handle, &reg_addr, 1, &reg_value, 1, 1000);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "  ✅ 成功读取寄存器 0x00: 0x%02x", reg_value);
    } else {
        ESP_LOGE(TAG, "  ❌ 读取寄存器失败: %s", esp_err_to_name(ret));
    }
    
    i2c_master_bus_rm_device(dev_handle);
}

#endif // _I2C_DIAGNOSTIC_H_

