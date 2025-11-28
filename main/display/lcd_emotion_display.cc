#include "lcd_emotion_display.h"
#include "emotion_assets_loader.h"
#include "device_state_mapper.h"
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <esp_psram.h>
#include <lvgl.h>

#define TAG "LcdEmotionDisplay"

LcdEmotionDisplay::LcdEmotionDisplay(esp_lcd_panel_io_handle_t panel_io,
                                   esp_lcd_panel_handle_t panel,
                                   int width, int height,
                                   int offset_x, int offset_y,
                                   bool mirror_x, bool mirror_y,
                                   bool swap_xy)
    : panel_io_(panel_io)
    , panel_(panel)
    , display_(nullptr)
    , screen_(nullptr)
    , status_bar_(nullptr)
    , status_label_(nullptr)
    , notification_label_(nullptr)
    , notification_timer_(nullptr)
    , emotion_system_initialized_(false)
{
    width_ = width;
    height_ = height;

    ESP_LOGI(TAG, "Initializing LcdEmotionDisplay (%dx%d)", width, height);

    // 初始化 LVGL
    InitializeLvgl(offset_x, offset_y, mirror_x, mirror_y, swap_xy);

    // 设置 UI
    SetupUI();

    ESP_LOGI(TAG, "LcdEmotionDisplay initialized");
}

LcdEmotionDisplay::~LcdEmotionDisplay() {
    if (notification_timer_) {
        lv_timer_del(notification_timer_);
        notification_timer_ = nullptr;
    }
}

void LcdEmotionDisplay::InitializeLvgl(int offset_x, int offset_y,
                                      bool mirror_x, bool mirror_y, bool swap_xy)
{
    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

#if CONFIG_SPIRAM
    // lv image cache
    size_t psram_size_mb = esp_psram_get_size() / 1024 / 1024;
    if (psram_size_mb >= 8) {
        lv_image_cache_resize(2 * 1024 * 1024, true);
        ESP_LOGI(TAG, "Use 2MB of PSRAM for image cache");
    } else if (psram_size_mb >= 2) {
        lv_image_cache_resize(512 * 1024, true);
        ESP_LOGI(TAG, "Use 512KB of PSRAM for image cache");
    }
#endif

    ESP_LOGI(TAG, "Initialize LVGL port");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 1;
#if CONFIG_SOC_CPU_CORES_NUM > 1
    port_cfg.task_affinity = 1;
#endif
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding LCD display");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,
        .panel_handle = panel_,
        .control_handle = nullptr,
        .buffer_size = static_cast<uint32_t>(width_ * 20),
        .double_buffer = false,
        .trans_size = 0,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .monochrome = false,
        .rotation = {
            .swap_xy = swap_xy,
            .mirror_x = mirror_x,
            .mirror_y = mirror_y,
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

    display_ = lvgl_port_add_disp(&display_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add display");
        return;
    }

    if (offset_x != 0 || offset_y != 0) {
        lv_display_set_offset(display_, offset_x, offset_y);
    }

    ESP_LOGI(TAG, "LVGL initialized successfully");
}

void LcdEmotionDisplay::SetupUI() {
    if (!Lock(1000)) {
        ESP_LOGE(TAG, "Failed to lock display for UI setup");
        return;
    }

    // 获取当前屏幕
    screen_ = lv_screen_active();
    if (!screen_) {
        ESP_LOGE(TAG, "Failed to get active screen");
        Unlock();
        return;
    }

    // 🔑 临时修复：设置背景色为白色，验证动画是否可见
    // TODO: 动画显示正常后改回黑色
    lv_obj_set_style_bg_color(screen_, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(screen_, LV_OPA_COVER, 0);
    ESP_LOGI(TAG, "🎨 Screen background set to WHITE for testing");

    // 创建状态栏（顶部）
    CreateStatusBar();

    // 注意：动画区域由 EmotionCoordinator 直接在 screen_ 上创建
    // 我们的 status_bar_ 会自动显示在动画上方

    Unlock();

    ESP_LOGI(TAG, "UI setup complete");
}

void LcdEmotionDisplay::CreateStatusBar() {
    // 创建半透明状态栏
    status_bar_ = lv_obj_create(screen_);
    lv_obj_set_size(status_bar_, width_, 30);
    lv_obj_align(status_bar_, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(status_bar_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(status_bar_, LV_OPA_50, 0);
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_style_pad_all(status_bar_, 5, 0);
    
    // 确保状态栏始终显示在最前面（作为 overlay）
    lv_obj_move_foreground(status_bar_);

    // 默认隐藏状态栏
    lv_obj_add_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);

    // 状态文字
    status_label_ = lv_label_create(status_bar_);
    lv_label_set_text(status_label_, "");
    lv_obj_set_style_text_color(status_label_, lv_color_white(), 0);
    lv_obj_align(status_label_, LV_ALIGN_LEFT_MID, 0, 0);

    // 通知文字（右对齐）
    notification_label_ = lv_label_create(status_bar_);
    lv_label_set_text(notification_label_, "");
    lv_obj_set_style_text_color(notification_label_, lv_color_white(), 0);
    lv_obj_align(notification_label_, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    ESP_LOGI(TAG, "Status bar created");
}

// ============================================================================
// Display 接口实现
// ============================================================================

bool LcdEmotionDisplay::Lock(int timeout_ms) {
    return lvgl_port_lock(timeout_ms);
}

void LcdEmotionDisplay::Unlock() {
    lvgl_port_unlock();
}

void LcdEmotionDisplay::SetStatus(const char* status) {
    if (!status || !status_label_) return;

    if (!Lock(100)) return;

    lv_label_set_text(status_label_, status);
    
    // 如果有状态文字，显示状态栏
    if (strlen(status) > 0) {
        lv_obj_clear_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
        // 确保状态栏显示在动画上方
        lv_obj_move_foreground(status_bar_);
    } else {
        lv_obj_add_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
    }

    Unlock();
}

void LcdEmotionDisplay::ShowNotification(const char* message, int duration_ms) {
    if (!message || !notification_label_) return;

    if (!Lock(100)) return;

    ESP_LOGI(TAG, "Notification: %s (duration: %d ms)", message, duration_ms);

    lv_label_set_text(notification_label_, message);
    lv_obj_clear_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(status_bar_, LV_OBJ_FLAG_HIDDEN);
    
    // 确保状态栏显示在动画上方
    lv_obj_move_foreground(status_bar_);

    // 设置定时隐藏
    if (duration_ms > 0) {
        if (notification_timer_) {
            lv_timer_del(notification_timer_);
        }
        notification_timer_ = lv_timer_create(HideNotificationCallback, duration_ms, this);
    }

    Unlock();
}

void LcdEmotionDisplay::HideNotificationCallback(lv_timer_t* timer) {
    auto* display = static_cast<LcdEmotionDisplay*>(lv_timer_get_user_data(timer));
    if (!display || !display->Lock(100)) return;

    if (display->notification_label_) {
        lv_obj_add_flag(display->notification_label_, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(display->notification_label_, "");
    }

    // 如果状态也是空的，隐藏整个状态栏
    if (display->status_label_) {
        const char* status_text = lv_label_get_text(display->status_label_);
        if (!status_text || strlen(status_text) == 0) {
            lv_obj_add_flag(display->status_bar_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    display->Unlock();

    if (display->notification_timer_) {
        lv_timer_del(display->notification_timer_);
        display->notification_timer_ = nullptr;
    }
}

void LcdEmotionDisplay::SetChatMessage(const char* role, const char* content) {
    // 情感显示界面不显示聊天消息，可以作为通知显示
    if (content && strlen(content) > 0) {
        ShowNotification(content, 3000);
    }
}

void LcdEmotionDisplay::SetEmotion(const char* emotion) {
    if (!emotion || !emotion_system_initialized_) {
        ESP_LOGW(TAG, "Cannot set emotion: %s (initialized=%d)", 
                 emotion ? emotion : "null", emotion_system_initialized_);
        return;
    }

    // 简单映射：字符串 -> EmotionState
    // Application 通常调用这个接口，传入的是字符串（如 "neutral", "happy" 等）
    ESP_LOGI(TAG, "SetEmotion called with: %s", emotion);
    
    // 对于常见的情感名称，映射到对应的 EmotionState
    emotion::EmotionState state = emotion::EmotionState::CALM;  // 默认
    
    if (strcmp(emotion, "neutral") == 0 || strcmp(emotion, "calm") == 0) {
        state = emotion::EmotionState::CALM;
    } else if (strcmp(emotion, "happy") == 0) {
        state = emotion::EmotionState::HAPPY;
    } else if (strcmp(emotion, "sad") == 0) {
        state = emotion::EmotionState::SAD;
    } else if (strcmp(emotion, "excited") == 0) {
        state = emotion::EmotionState::EXCITED;
    } else if (strcmp(emotion, "sleepy") == 0) {
        state = emotion::EmotionState::SLEEPY;
    } else if (strcmp(emotion, "surprised") == 0) {
        state = emotion::EmotionState::SURPRISED;
    } else if (strcmp(emotion, "listening") == 0) {
        state = emotion::EmotionState::LISTENING;
    } else {
        ESP_LOGW(TAG, "Unknown emotion: %s, using CALM", emotion);
    }
    
    ShowEmotion(state);
}

void LcdEmotionDisplay::UpdateStatusBar(bool update_all) {
    // 情感显示界面的状态栏比较简单，暂时不需要复杂更新
    // 可以根据需要扩展
}

// ============================================================================
// 情感系统相关
// ============================================================================

bool LcdEmotionDisplay::InitEmotionSystem() {
    if (emotion_system_initialized_) {
        ESP_LOGW(TAG, "Emotion system already initialized");
        return true;
    }

    ESP_LOGI(TAG, "Initializing emotion system from Assets...");

    // 🔒 重要：持有 LVGL 锁（InitEmotionSystemFromAssets 会创建LVGL对象）
    if (!Lock(1000)) {
        ESP_LOGE(TAG, "Failed to lock display for emotion system init");
        return false;
    }

    // 使用 emotion_assets_loader 的初始化函数
    bool init_success = emotion::InitEmotionSystemFromAssets(this, width_, height_);
    
    Unlock();

    if (!init_success) {
        ESP_LOGE(TAG, "Failed to initialize emotion system");
        return false;
    }

    emotion_system_initialized_ = true;
    ESP_LOGI(TAG, "✅ Emotion system initialized successfully");

    // 使用定时器延迟播放初始情感动画（确保在 LVGL 任务中执行）
    ESP_LOGI(TAG, "Scheduling initial emotion animation (CALM) in 500ms");
    
    if (!Lock(1000)) {
        ESP_LOGW(TAG, "Failed to lock for timer creation");
        return true;  // 初始化成功，但不创建定时器
    }
    
    lv_timer_t* init_timer = lv_timer_create([](lv_timer_t* timer) {
        auto* self = static_cast<LcdEmotionDisplay*>(lv_timer_get_user_data(timer));
        if (self) {
            ESP_LOGI("LcdEmotionDisplay", "🎬 Playing initial CALM animation (timer callback)");
            self->ShowEmotion(emotion::EmotionState::CALM);
        }
        lv_timer_del(timer);  // 一次性定时器
    }, 500, this);
    
    Unlock();
    
    if (!init_timer) {
        ESP_LOGW(TAG, "Failed to create init timer");
    }

    return true;
}

void LcdEmotionDisplay::SetDeviceState(emotion::DeviceState state) {
    if (!emotion_system_initialized_) {
        ESP_LOGW(TAG, "Emotion system not initialized");
        return;
    }

    auto& coordinator = emotion::EmotionCoordinator::Instance();
    coordinator.SetDeviceState(state);
}

void LcdEmotionDisplay::ShowEmotion(emotion::EmotionState emotion) {
    if (!emotion_system_initialized_) {
        ESP_LOGW(TAG, "Emotion system not initialized");
        return;
    }

    auto& state_mgr = emotion::EmotionStateManager::Instance();
    state_mgr.SetEmotion(emotion);
}

