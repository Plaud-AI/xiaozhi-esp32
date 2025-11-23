/**
 * I2C 临时测试程序
 * 
 * 使用方法：
 * 1. 在 main.cc 中调用 TestI2CConnection()
 * 2. 观察串口输出，找到能检测到设备的引脚组合
 * 3. 根据结果修改 config.h 中的 I2C 引脚配置
 */

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "I2C_Test"

// 可能的 I2C 引脚组合
struct I2CPinConfig {
    int sda;
    int scl;
    const char* name;
};

static const I2CPinConfig test_pins[] = {
    // 当前配置
    {5, 4, "GPIO5(SDA)/GPIO4(SCL) - 当前配置"},
    
    // 常见的 ESP32-S3 I2C 引脚
    {1, 2, "GPIO1(SDA)/GPIO2(SCL)"},
    {6, 7, "GPIO6(SDA)/GPIO7(SCL)"},
    {8, 9, "GPIO8(SDA)/GPIO9(SCL)"},
    {10, 11, "GPIO10(SDA)/GPIO11(SCL)"},
    {41, 42, "GPIO41(SDA)/GPIO42(SCL)"},
    {15, 16, "GPIO15(SDA)/GPIO16(SCL)"},
    {17, 18, "GPIO17(SDA)/GPIO18(SCL)"},
    
    // 反转 SDA/SCL（万一接反了）
    {4, 5, "GPIO4(SDA)/GPIO5(SCL) - 反转"},
};

// ES8311 和 ES7210 可能的地址
static const uint8_t test_addresses[] = {
    0x18, // ES8311 (AD0=GND)
    0x19, // ES8311 (AD0=VDD)
    0x30, // ES8311 (某些变体)
    0x40, // ES7210 (ADDR=GND)
    0x42, // ES7210 (ADDR=VDD)
    0x43, // ES7210 (ADDR=SDA)
    0x44, // ES7210 (ADDR=SCL)
};

/**
 * 扫描指定引脚的 I2C 总线
 */
bool ScanI2CBusOnPins(int sda, int scl, const char* name) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "════════════════════════════════════════");
    ESP_LOGI(TAG, "测试: %s", name);
    ESP_LOGI(TAG, "════════════════════════════════════════");
    
    // 配置 I2C 主机总线
    i2c_master_bus_config_t bus_config = {};
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.i2c_port = I2C_NUM_0;
    bus_config.scl_io_num = static_cast<gpio_num_t>(scl);
    bus_config.sda_io_num = static_cast<gpio_num_t>(sda);
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = true;  // 启用内部上拉
    
    i2c_master_bus_handle_t bus_handle;
    esp_err_t ret = i2c_new_master_bus(&bus_config, &bus_handle);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ 无法初始化 I2C 总线: %s", esp_err_to_name(ret));
        return false;
    }
    
    // 扫描所有测试地址
    bool found_any = false;
    
    ESP_LOGI(TAG, "扫描设备...");
    for (uint8_t addr : test_addresses) {
        i2c_device_config_t dev_cfg = {};
        dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dev_cfg.device_address = addr;
        dev_cfg.scl_speed_hz = 100000;
        
        i2c_master_dev_handle_t dev_handle;
        ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);
        
        if (ret == ESP_OK) {
            // 尝试探测设备
            ret = i2c_master_probe(bus_handle, addr, 100);
            
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "  ✅ 发现设备: 0x%02X", addr);
                
                // 识别设备
                if (addr == 0x18 || addr == 0x19 || addr == 0x30) {
                    ESP_LOGI(TAG, "      可能是 ES8311 (DAC)");
                } else if (addr >= 0x40 && addr <= 0x44) {
                    ESP_LOGI(TAG, "      可能是 ES7210 (ADC)");
                }
                
                found_any = true;
            }
            
            i2c_master_bus_rm_device(dev_handle);
        }
    }
    
    if (!found_any) {
        ESP_LOGW(TAG, "  ⚠️ 未发现任何设备");
    }
    
    // 完整扫描（0x08 - 0x77）
    if (found_any) {
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "完整扫描结果:");
        ESP_LOGI(TAG, "     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f");
        
        for (int i = 0; i < 128; i += 16) {
            printf("%02x: ", i);
            for (int j = 0; j < 16; j++) {
                uint8_t addr = i + j;
                
                if (addr < 0x08 || addr > 0x77) {
                    printf("   ");
                    continue;
                }
                
                esp_err_t ret = i2c_master_probe(bus_handle, addr, 50);
                if (ret == ESP_OK) {
                    printf("%02x ", addr);
                } else {
                    printf("-- ");
                }
            }
            printf("\n");
        }
    }
    
    // 清理
    i2c_del_master_bus(bus_handle);
    
    return found_any;
}

/**
 * 主测试函数
 */
void TestI2CConnection() {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔══════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  ESP32-S3 Speaker I2C 连接测试工具           ║");
    ESP_LOGI(TAG, "║  目标: 找到 ES8311 和 ES7210 的正确引脚      ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "");
    
    bool found_working_config = false;
    
    // 测试所有可能的引脚组合
    for (const auto& config : test_pins) {
        bool result = ScanI2CBusOnPins(config.sda, config.scl, config.name);
        
        if (result) {
            found_working_config = true;
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "★★★ 找到可用的引脚配置！ ★★★");
            ESP_LOGI(TAG, "    SDA = GPIO%d", config.sda);
            ESP_LOGI(TAG, "    SCL = GPIO%d", config.scl);
            ESP_LOGI(TAG, "");
            
            // 不退出，继续测试其他组合（可能有多个 I2C 总线）
        }
        
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔══════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  测试完成                                    ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "");
    
    if (found_working_config) {
        ESP_LOGI(TAG, "✅ 找到了可用的 I2C 配置！");
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "下一步操作：");
        ESP_LOGI(TAG, "  1. 记录上面显示的 SDA 和 SCL 引脚号");
        ESP_LOGI(TAG, "  2. 记录检测到的设备地址（0x18/0x30 for ES8311, 0x40/0x42 for ES7210）");
        ESP_LOGI(TAG, "  3. 修改 main/boards/esp32-speaker-s3-classic/config.h:");
        ESP_LOGI(TAG, "     - 更新 AUDIO_CODEC_I2C_SDA_PIN");
        ESP_LOGI(TAG, "     - 更新 AUDIO_CODEC_I2C_SCL_PIN");
        ESP_LOGI(TAG, "     - 如果地址不同，还需要修改 AUDIO_CODEC_ES8311_ADDR 和 AUDIO_CODEC_ES7210_ADDR");
        ESP_LOGI(TAG, "  4. 重新编译并烧录固件");
    } else {
        ESP_LOGE(TAG, "❌ 未找到任何 I2C 设备！");
        ESP_LOGE(TAG, "");
        ESP_LOGE(TAG, "可能的原因：");
        ESP_LOGE(TAG, "  1. ES8311 和 ES7210 未正确供电");
        ESP_LOGE(TAG, "     → 使用万用表测量 VDD 引脚，应该是 3.3V");
        ESP_LOGE(TAG, "");
        ESP_LOGE(TAG, "  2. I2C 上拉电阻缺失或未连接");
        ESP_LOGE(TAG, "     → 检查 R32 (4.7kΩ) 是否已安装");
        ESP_LOGE(TAG, "     → 测量 I2C 引脚的空闲电压，应该是 3.3V");
        ESP_LOGE(TAG, "");
        ESP_LOGE(TAG, "  3. 芯片处于复位状态");
        ESP_LOGE(TAG, "     → 检查 RESET 引脚是否被拉低");
        ESP_LOGE(TAG, "");
        ESP_LOGE(TAG, "  4. I2C 引脚在原理图中使用了不同的标号");
        ESP_LOGE(TAG, "     → 需要提供更清晰的原理图进行确认");
    }
    
    ESP_LOGI(TAG, "");
}

