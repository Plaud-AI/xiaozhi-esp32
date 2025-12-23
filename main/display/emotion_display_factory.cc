#include "emotion_display_factory.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

static const char* TAG = "EmotionDisplayFactory";

namespace display {

EmotionDisplay* CreateSpiLcdEmotionDisplay(
    esp_lcd_panel_io_handle_t panel_io,
    esp_lcd_panel_handle_t panel,
    int width, 
    int height) {
    
    ESP_LOGI(TAG, "Creating SPI LCD EmotionDisplay (%dx%d)", width, height);

    // 初始化 LVGL
    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

    ESP_LOGI(TAG, "Initialize LVGL port");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    // 优化任务调度：优先级 3，不固定 CPU
    port_cfg.task_priority = 3;
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding LCD display");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io,
        .panel_handle = panel,
        .control_handle = nullptr,
        .buffer_size = static_cast<uint32_t>(width * 20),
        .double_buffer = false,
        .trans_size = 0,
        .hres = static_cast<uint32_t>(width),
        .vres = static_cast<uint32_t>(height),
        .monochrome = false,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = 1,
            .buff_spiram = 0,
            .sw_rotate = 0,
            .swap_bytes = 1,
            .full_refresh = 0,
            .direct_mode = 0,
        },
    };

    lv_display_t* display = lvgl_port_add_disp(&display_cfg);
    if (display == nullptr) {
        ESP_LOGE(TAG, "Failed to add display");
        return nullptr;
    }

    // 创建 EmotionDisplay
    EmotionDisplay* emotion_display = new EmotionDisplay(width, height);
    
    ESP_LOGI(TAG, "SPI LCD EmotionDisplay created");
    return emotion_display;
}

EmotionDisplay* CreateRgbLcdEmotionDisplay(
    esp_lcd_panel_io_handle_t panel_io,
    esp_lcd_panel_handle_t panel,
    int width, 
    int height) {
    
    ESP_LOGI(TAG, "Creating RGB LCD EmotionDisplay (%dx%d)", width, height);

    // 初始化 LVGL
    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

    ESP_LOGI(TAG, "Initialize LVGL port");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    // 优化任务调度：优先级 3，不固定 CPU
    port_cfg.task_priority = 3;
    port_cfg.timer_period_ms = 50;
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding LCD display");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io,
        .panel_handle = panel,
        .buffer_size = static_cast<uint32_t>(width * 20),
        .double_buffer = true,
        .hres = static_cast<uint32_t>(width),
        .vres = static_cast<uint32_t>(height),
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = 1,
            .swap_bytes = 0,
            .full_refresh = 1,
            .direct_mode = 1,
        },
    };

    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            .bb_mode = true,
            .avoid_tearing = true,
        }
    };
    
    lv_display_t* display = lvgl_port_add_disp_rgb(&display_cfg, &rgb_cfg);
    if (display == nullptr) {
        ESP_LOGE(TAG, "Failed to add RGB display");
        return nullptr;
    }

    // 创建 EmotionDisplay
    EmotionDisplay* emotion_display = new EmotionDisplay(width, height);
    
    ESP_LOGI(TAG, "RGB LCD EmotionDisplay created");
    return emotion_display;
}

EmotionDisplay* CreateEmotionDisplay(int width, int height) {
    ESP_LOGI(TAG, "Creating EmotionDisplay (%dx%d) with existing LVGL", width, height);
    
    // 假设 LVGL 已经初始化
    EmotionDisplay* emotion_display = new EmotionDisplay(width, height);
    
    ESP_LOGI(TAG, "EmotionDisplay created");
    return emotion_display;
}

} // namespace display

