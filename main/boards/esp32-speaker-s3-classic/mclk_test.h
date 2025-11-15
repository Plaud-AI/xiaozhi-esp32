#ifndef _MCLK_TEST_H_
#define _MCLK_TEST_H_

#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

/**
 * MCLK 诊断工具
 * 
 * 用于测试 GPIO 驱动能力和测量实际输出电压
 * 
 * 使用方法：
 * 1. 在 esp32_speaker_s3_classic.cc 的 Initialize() 中调用
 * 2. 使用示波器测量 GPIO 输出电压
 * 3. 确认驱动强度设置是否生效
 */

#define TAG_MCLK_TEST "MCLK_Test"

namespace mclk_test {

/**
 * 测试 GPIO 输出电压
 * 
 * @param gpio_num GPIO 引脚号
 * @param gpio_name GPIO 名称（用于日志）
 * @param duration_ms 测试持续时间（毫秒）
 */
inline void TestGpioOutput(gpio_num_t gpio_num, const char* gpio_name, uint32_t duration_ms = 5000) {
    ESP_LOGI(TAG_MCLK_TEST, "========================================");
    ESP_LOGI(TAG_MCLK_TEST, "开始测试 %s (GPIO%d) 输出电压", gpio_name, gpio_num);
    ESP_LOGI(TAG_MCLK_TEST, "========================================");
    
    // 1. 配置为输出模式
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << gpio_num),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    ESP_LOGI(TAG_MCLK_TEST, "✅ GPIO%d 已配置为输出模式", gpio_num);
    
    // 2. 测试不同驱动强度
    gpio_drive_cap_t drive_caps[] = {
        GPIO_DRIVE_CAP_0,  // 5mA
        GPIO_DRIVE_CAP_1,  // 10mA
        GPIO_DRIVE_CAP_2,  // 20mA
        GPIO_DRIVE_CAP_3,  // 40mA
    };
    const char* drive_names[] = {
        "GPIO_DRIVE_CAP_0 (5mA)",
        "GPIO_DRIVE_CAP_1 (10mA)",
        "GPIO_DRIVE_CAP_2 (20mA)",
        "GPIO_DRIVE_CAP_3 (40mA)",
    };
    
    for (int i = 0; i < 4; i++) {
        ESP_LOGI(TAG_MCLK_TEST, "");
        ESP_LOGI(TAG_MCLK_TEST, "──────────────────────────────────────");
        ESP_LOGI(TAG_MCLK_TEST, "测试驱动强度：%s", drive_names[i]);
        ESP_LOGI(TAG_MCLK_TEST, "──────────────────────────────────────");
        
        // 设置驱动强度
        esp_err_t ret = gpio_set_drive_capability(gpio_num, drive_caps[i]);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG_MCLK_TEST, "✅ 驱动强度已设置");
        } else {
            ESP_LOGE(TAG_MCLK_TEST, "❌ 驱动强度设置失败: %s", esp_err_to_name(ret));
            continue;
        }
        
        // 读取当前驱动强度（验证）
        gpio_drive_cap_t actual_cap;
        ret = gpio_get_drive_capability(gpio_num, &actual_cap);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG_MCLK_TEST, "✅ 当前驱动强度：%d (%s)", 
                     actual_cap, 
                     actual_cap == drive_caps[i] ? "正确" : "⚠️ 不匹配！");
        }
        
        // 输出高电平
        gpio_set_level(gpio_num, 1);
        ESP_LOGI(TAG_MCLK_TEST, "📊 GPIO%d 输出高电平 (3 秒)", gpio_num);
        ESP_LOGI(TAG_MCLK_TEST, "   请使用示波器或万用表测量电压：");
        ESP_LOGI(TAG_MCLK_TEST, "   - 空载电压（直接测 GPIO）");
        ESP_LOGI(TAG_MCLK_TEST, "   - 带载电压（测 ES7210 MCLK 引脚）");
        vTaskDelay(pdMS_TO_TICKS(3000));
        
        // 输出低电平
        gpio_set_level(gpio_num, 0);
        ESP_LOGI(TAG_MCLK_TEST, "📊 GPIO%d 输出低电平 (1 秒)", gpio_num);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    ESP_LOGI(TAG_MCLK_TEST, "");
    ESP_LOGI(TAG_MCLK_TEST, "========================================");
    ESP_LOGI(TAG_MCLK_TEST, "%s (GPIO%d) 测试完成", gpio_name, gpio_num);
    ESP_LOGI(TAG_MCLK_TEST, "========================================");
    ESP_LOGI(TAG_MCLK_TEST, "");
}

/**
 * 测试方波输出
 * 
 * @param gpio_num GPIO 引脚号
 * @param gpio_name GPIO 名称
 * @param freq_hz 方波频率（Hz）
 * @param duration_ms 测试持续时间（毫秒）
 */
inline void TestSquareWave(gpio_num_t gpio_num, const char* gpio_name, 
                           uint32_t freq_hz = 4000000, uint32_t duration_ms = 5000) {
    ESP_LOGI(TAG_MCLK_TEST, "========================================");
    ESP_LOGI(TAG_MCLK_TEST, "测试 %s (GPIO%d) 方波输出", gpio_name, gpio_num);
    ESP_LOGI(TAG_MCLK_TEST, "频率：%u Hz", freq_hz);
    ESP_LOGI(TAG_MCLK_TEST, "========================================");
    
    // 配置为输出
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << gpio_num),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    
    // 设置最大驱动强度
    gpio_set_drive_capability(gpio_num, GPIO_DRIVE_CAP_3);
    
    ESP_LOGI(TAG_MCLK_TEST, "开始输出方波，持续 %u ms...", duration_ms);
    ESP_LOGI(TAG_MCLK_TEST, "请使用示波器测量：");
    ESP_LOGI(TAG_MCLK_TEST, "- 峰峰值电压 (Vpp)");
    ESP_LOGI(TAG_MCLK_TEST, "- 上升时间 (tr)");
    ESP_LOGI(TAG_MCLK_TEST, "- 下降时间 (tf)");
    
    uint32_t half_period_us = 1000000 / (2 * freq_hz);
    uint32_t iterations = (duration_ms * 1000) / (half_period_us * 2);
    
    for (uint32_t i = 0; i < iterations; i++) {
        gpio_set_level(gpio_num, 1);
        esp_rom_delay_us(half_period_us);
        gpio_set_level(gpio_num, 0);
        esp_rom_delay_us(half_period_us);
    }
    
    ESP_LOGI(TAG_MCLK_TEST, "方波输出测试完成");
    ESP_LOGI(TAG_MCLK_TEST, "");
}

/**
 * 完整的 MCLK 诊断流程
 * 
 * @param es8311_mclk ES8311 MCLK 引脚
 * @param es7210_mclk ES7210 MCLK 引脚
 */
inline void RunFullDiagnostic(gpio_num_t es8311_mclk, gpio_num_t es7210_mclk) {
    ESP_LOGI(TAG_MCLK_TEST, "");
    ESP_LOGI(TAG_MCLK_TEST, "╔════════════════════════════════════════╗");
    ESP_LOGI(TAG_MCLK_TEST, "║     MCLK 输出电压诊断工具 v1.0        ║");
    ESP_LOGI(TAG_MCLK_TEST, "╚════════════════════════════════════════╝");
    ESP_LOGI(TAG_MCLK_TEST, "");
    ESP_LOGI(TAG_MCLK_TEST, "目的：");
    ESP_LOGI(TAG_MCLK_TEST, "1. 测试不同驱动强度下的 GPIO 输出电压");
    ESP_LOGI(TAG_MCLK_TEST, "2. 对比空载电压 vs 带载电压");
    ESP_LOGI(TAG_MCLK_TEST, "3. 确认是否达到 2V 以上");
    ESP_LOGI(TAG_MCLK_TEST, "");
    ESP_LOGI(TAG_MCLK_TEST, "准备：");
    ESP_LOGI(TAG_MCLK_TEST, "- 示波器或万用表");
    ESP_LOGI(TAG_MCLK_TEST, "- 测试探头");
    ESP_LOGI(TAG_MCLK_TEST, "");
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // 测试 ES8311 MCLK (GPIO3)
    TestGpioOutput(es8311_mclk, "ES8311_MCLK");
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // 测试 ES7210 MCLK (GPIO13)
    TestGpioOutput(es7210_mclk, "ES7210_MCLK");
    
    ESP_LOGI(TAG_MCLK_TEST, "");
    ESP_LOGI(TAG_MCLK_TEST, "╔════════════════════════════════════════╗");
    ESP_LOGI(TAG_MCLK_TEST, "║          诊断完成                      ║");
    ESP_LOGI(TAG_MCLK_TEST, "╚════════════════════════════════════════╝");
    ESP_LOGI(TAG_MCLK_TEST, "");
    ESP_LOGI(TAG_MCLK_TEST, "分析建议：");
    ESP_LOGI(TAG_MCLK_TEST, "");
    ESP_LOGI(TAG_MCLK_TEST, "1. 如果空载电压 > 3.0V，带载电压 < 2V：");
    ESP_LOGI(TAG_MCLK_TEST, "   → ES7210 输入阻抗太低，需要缓冲器");
    ESP_LOGI(TAG_MCLK_TEST, "");
    ESP_LOGI(TAG_MCLK_TEST, "2. 如果空载电压就 < 2.5V：");
    ESP_LOGI(TAG_MCLK_TEST, "   → 电源供电不足或 GPIO 驱动问题");
    ESP_LOGI(TAG_MCLK_TEST, "");
    ESP_LOGI(TAG_MCLK_TEST, "3. 如果 40mA 驱动电压最高：");
    ESP_LOGI(TAG_MCLK_TEST, "   → 软件配置正确，考虑硬件优化");
    ESP_LOGI(TAG_MCLK_TEST, "");
    ESP_LOGI(TAG_MCLK_TEST, "4. 如果各驱动强度电压相同：");
    ESP_LOGI(TAG_MCLK_TEST, "   → gpio_set_drive_capability 可能未生效");
    ESP_LOGI(TAG_MCLK_TEST, "");
}

} // namespace mclk_test

#endif // _MCLK_TEST_H_

