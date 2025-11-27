#include "emotion_display.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

static const char* TAG = "EmotionDisplay";

namespace display {

EmotionDisplay::EmotionDisplay(int width, int height)
    : width_(width)
    , height_(height)
    , screen_(nullptr)
    , top_bar_(nullptr)
    , top_bar_icon_(nullptr)
    , top_bar_label_(nullptr)
    , animation_container_(nullptr)
    , top_bar_timer_(nullptr)
    , emotion_system_initialized_(false)
    , top_bar_visible_(false) {
    
    ESP_LOGI(TAG, "Creating EmotionDisplay (%dx%d)", width, height);
    
    // 设置 UI
    SetupUI();
}

EmotionDisplay::~EmotionDisplay() {
    // 停止定时器
    if (top_bar_timer_) {
        lv_timer_del(top_bar_timer_);
        top_bar_timer_ = nullptr;
    }

    // 删除 UI 对象
    if (screen_) {
        lv_obj_del(screen_);
        screen_ = nullptr;
    }

    ESP_LOGI(TAG, "EmotionDisplay destroyed");
}

// ============================================================================
// Display 接口实现
// ============================================================================

bool EmotionDisplay::Lock(int timeout_ms) {
    return lvgl_port_lock(timeout_ms);
}

void EmotionDisplay::Unlock() {
    lvgl_port_unlock();
}

void EmotionDisplay::SetChatMessage(const char* role, const char* content) {
    // 情感显示界面不显示聊天消息，可以选择在顶部状态栏显示
    if (content && strlen(content) > 0) {
        ShowTopBar(nullptr, content, 3000);
    }
}

void EmotionDisplay::ShowNotification(const char* message, int duration_ms) {
    if (!message) return;
    
    ESP_LOGI(TAG, "Notification: %s (duration: %d ms)", message, duration_ms);
    ShowTopBar(nullptr, message, duration_ms);
}

void EmotionDisplay::ShowLowBatteryWarning() {
    ShowTopBar(nullptr, "⚠️ 电量低", -1);  // 持续显示
    
    // 同时显示困倦表情
    if (emotion_system_initialized_) {
        ShowEmotion(lottie::EmotionType::SLEEPY, -1);
    }
}

void EmotionDisplay::HideLowBatteryWarning() {
    HideTopBar();
    
    // 返回中性表情
    if (emotion_system_initialized_) {
        ShowEmotion(lottie::EmotionType::NEUTRAL);
    }
}

void EmotionDisplay::SetBatteryLevel(int percentage) {
    // 根据电量显示不同表情
    if (percentage < 10) {
        ShowLowBatteryWarning();
    } else if (percentage < 20) {
        // 可以显示略微疲惫的表情
        if (emotion_system_initialized_) {
            ShowEmotion(lottie::EmotionType::SLEEPY, 2000);
        }
    }
}

void EmotionDisplay::SetNetworkStatus(bool connected) {
    if (connected) {
        ShowTopBar(nullptr, "✓ 网络已连接", 2000);
        if (emotion_system_initialized_) {
            ShowEmotion(lottie::EmotionType::HAPPY, 2000);
        }
    } else {
        ShowTopBar(nullptr, "✗ 网络断开", -1);
        if (emotion_system_initialized_) {
            ShowEmotion(lottie::EmotionType::CONFUSED, -1);
        }
    }
}

void EmotionDisplay::SetVolume(int level) {
    // 可以在顶部短暂显示音量信息
    char msg[32];
    snprintf(msg, sizeof(msg), "🔊 音量: %d%%", level);
    ShowTopBar(nullptr, msg, 1500);
}

void EmotionDisplay::SetPreviewImage(const void* image_data) {
    // 情感显示界面不支持预览图片
    ESP_LOGW(TAG, "Preview image not supported in EmotionDisplay");
}

void EmotionDisplay::SetStatusIcon(StatusIcon icon, bool visible) {
    // 可以根据不同的图标类型显示不同的表情
    if (!visible || !emotion_system_initialized_) return;
    
    switch (icon) {
        case StatusIcon::WIFI:
            ShowEmotion(lottie::EmotionType::THINKING, 1000);
            break;
        case StatusIcon::RECORDING:
            ShowEmotion(lottie::EmotionType::NEUTRAL, -1);
            break;
        case StatusIcon::SPEAKING:
            ShowEmotion(lottie::EmotionType::HAPPY, -1);
            break;
        case StatusIcon::LOADING:
            ShowEmotion(lottie::EmotionType::THINKING, -1);
            break;
        default:
            break;
    }
}

// ============================================================================
// 情感动画相关接口
// ============================================================================

bool EmotionDisplay::InitEmotionSystem(const char* emotions_dir) {
    if (emotion_system_initialized_) {
        ESP_LOGW(TAG, "Emotion system already initialized");
        return true;
    }

    ESP_LOGI(TAG, "Initializing emotion animation system...");

    // 初始化动画管理器
    lottie::AnimationManager::Instance().Init(animation_container_, width_, height_);

    // 初始化情感动画管理器
    if (!lottie::EmotionAnimationManager::Instance().Init()) {
        ESP_LOGE(TAG, "Failed to initialize EmotionAnimationManager");
        return false;
    }

    // 批量注册情感动画
    int count = lottie::EmotionAnimationManager::Instance()
                .RegisterEmotionsFromDirectory(emotions_dir);
    
    if (count == 0) {
        ESP_LOGW(TAG, "No emotions registered from directory: %s", emotions_dir);
    } else {
        ESP_LOGI(TAG, "Registered %d emotions", count);
    }

    // 设置默认情感
    lottie::EmotionAnimationManager::Instance().SetDefaultEmotion(lottie::EmotionType::NEUTRAL);

    emotion_system_initialized_ = true;
    ESP_LOGI(TAG, "Emotion system initialized successfully");

    // 显示默认中性表情
    ShowEmotion(lottie::EmotionType::NEUTRAL);

    return true;
}

bool EmotionDisplay::ShowEmotion(lottie::EmotionType emotion, int duration_ms) {
    if (!emotion_system_initialized_) {
        ESP_LOGW(TAG, "Emotion system not initialized");
        return false;
    }

    ESP_LOGI(TAG, "Showing emotion: %s (duration: %d ms)", 
             lottie::EmotionTypeToString(emotion), duration_ms);

    return lottie::EmotionAnimationManager::Instance().ShowEmotion(emotion, duration_ms);
}

bool EmotionDisplay::ShowEmotion(const char* emotion_name, int duration_ms) {
    if (!emotion_name) return false;
    
    lottie::EmotionType emotion = lottie::StringToEmotionType(emotion_name);
    return ShowEmotion(emotion, duration_ms);
}

bool EmotionDisplay::PlayEmotionSequence(
    const std::vector<lottie::EmotionSequenceItem>& sequence, bool loop) {
    
    if (!emotion_system_initialized_) {
        ESP_LOGW(TAG, "Emotion system not initialized");
        return false;
    }

    ESP_LOGI(TAG, "Playing emotion sequence (%zu items, loop: %d)", sequence.size(), loop);

    return lottie::EmotionAnimationManager::Instance().PlayEmotionSequence(sequence, loop);
}

void EmotionDisplay::StopEmotion() {
    if (emotion_system_initialized_) {
        lottie::EmotionAnimationManager::Instance().Stop();
    }
}

void EmotionDisplay::PauseEmotion() {
    if (emotion_system_initialized_) {
        lottie::EmotionAnimationManager::Instance().Pause();
    }
}

void EmotionDisplay::ResumeEmotion() {
    if (emotion_system_initialized_) {
        lottie::EmotionAnimationManager::Instance().Resume();
    }
}

lottie::EmotionType EmotionDisplay::GetCurrentEmotion() const {
    if (!emotion_system_initialized_) {
        return lottie::EmotionType::UNKNOWN;
    }
    return lottie::EmotionAnimationManager::Instance().GetCurrentEmotion();
}

// ============================================================================
// 顶部状态栏接口
// ============================================================================

void EmotionDisplay::ShowTopBar(const char* icon, const char* text, int duration_ms) {
    if (!Lock(100)) return;

    // 更新图标
    if (icon && top_bar_icon_) {
        UpdateTopBarIcon(icon);
        lv_obj_clear_flag(top_bar_icon_, LV_OBJ_FLAG_HIDDEN);
    } else if (top_bar_icon_) {
        lv_obj_add_flag(top_bar_icon_, LV_OBJ_FLAG_HIDDEN);
    }

    // 更新文字
    if (text && top_bar_label_) {
        UpdateTopBarText(text);
        lv_obj_clear_flag(top_bar_label_, LV_OBJ_FLAG_HIDDEN);
    } else if (top_bar_label_) {
        lv_obj_add_flag(top_bar_label_, LV_OBJ_FLAG_HIDDEN);
    }

    // 显示顶部栏
    if (top_bar_ && (icon || text)) {
        lv_obj_clear_flag(top_bar_, LV_OBJ_FLAG_HIDDEN);
        top_bar_visible_ = true;

        // 设置定时器自动隐藏
        if (duration_ms > 0) {
            if (top_bar_timer_) {
                lv_timer_del(top_bar_timer_);
            }
            top_bar_timer_ = lv_timer_create(OnTopBarTimeout, duration_ms, this);
            lv_timer_set_repeat_count(top_bar_timer_, 1);
        } else if (duration_ms == 0) {
            // 默认 3 秒
            if (top_bar_timer_) {
                lv_timer_del(top_bar_timer_);
            }
            top_bar_timer_ = lv_timer_create(OnTopBarTimeout, 3000, this);
            lv_timer_set_repeat_count(top_bar_timer_, 1);
        }
        // duration_ms == -1 表示持续显示，不设置定时器
    }

    Unlock();
}

void EmotionDisplay::HideTopBar() {
    if (!Lock(100)) return;

    if (top_bar_) {
        lv_obj_add_flag(top_bar_, LV_OBJ_FLAG_HIDDEN);
        top_bar_visible_ = false;
    }

    // 删除定时器
    if (top_bar_timer_) {
        lv_timer_del(top_bar_timer_);
        top_bar_timer_ = nullptr;
    }

    Unlock();
}

void EmotionDisplay::UpdateTopBarText(const char* text) {
    if (top_bar_label_ && text) {
        lv_label_set_text(top_bar_label_, text);
    }
}

void EmotionDisplay::UpdateTopBarIcon(const char* icon) {
    // TODO: 实现图标加载
    // 可以根据图标路径加载图片或使用图标字体
    ESP_LOGD(TAG, "UpdateTopBarIcon: %s", icon);
}

void EmotionDisplay::SetTopBarConfig(const TopBarConfig& config) {
    top_bar_config_ = config;

    if (!Lock(100)) return;

    if (top_bar_) {
        // 应用配置
        lv_obj_set_height(top_bar_, config.height);
        lv_obj_set_style_bg_color(top_bar_, config.bg_color, 0);
        lv_obj_set_style_bg_opa(top_bar_, config.bg_opacity, 0);

        if (config.visible) {
            lv_obj_clear_flag(top_bar_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(top_bar_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    Unlock();
}

// ============================================================================
// 高级功能
// ============================================================================

void EmotionDisplay::SetEmotionChangeCallback(
    std::function<void(lottie::EmotionType)> callback) {
    
    if (emotion_system_initialized_) {
        lottie::EmotionAnimationManager::Instance().SetEmotionChangeCallback(callback);
    }
}

void EmotionDisplay::SetAutoReturnNeutral(bool enable, int delay_ms) {
    if (emotion_system_initialized_) {
        lottie::EmotionAnimationManager::Instance().SetAutoReturnNeutral(enable, delay_ms);
    }
}

void EmotionDisplay::SetTransitionAnimation(const char* transition_path) {
    if (emotion_system_initialized_ && transition_path) {
        lottie::EmotionAnimationManager::Instance().SetTransitionAnimation(transition_path);
    }
}

void EmotionDisplay::TestEmotion(lottie::EmotionType emotion, int repeat_count) {
    if (emotion_system_initialized_) {
        lottie::EmotionAnimationManager::Instance().TestEmotion(emotion, repeat_count);
    }
}

// ============================================================================
// 私有方法
// ============================================================================

void EmotionDisplay::SetupUI() {
    ESP_LOGI(TAG, "Setting up UI...");

    // 获取或创建屏幕
    screen_ = lv_scr_act();
    if (!screen_) {
        ESP_LOGE(TAG, "Failed to get screen");
        return;
    }

    // 设置屏幕背景
    lv_obj_set_style_bg_color(screen_, lv_color_black(), 0);

    // 创建顶部状态栏
    CreateTopBar();

    // 创建动画区域
    CreateAnimationArea();

    ESP_LOGI(TAG, "UI setup complete");
}

void EmotionDisplay::CreateTopBar() {
    // 创建顶部状态栏容器
    top_bar_ = lv_obj_create(screen_);
    lv_obj_set_size(top_bar_, width_, top_bar_config_.height);
    lv_obj_align(top_bar_, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(top_bar_, top_bar_config_.bg_color, 0);
    lv_obj_set_style_bg_opa(top_bar_, top_bar_config_.bg_opacity, 0);
    lv_obj_set_style_border_width(top_bar_, 0, 0);
    lv_obj_set_style_radius(top_bar_, 0, 0);
    lv_obj_set_style_pad_all(top_bar_, 8, 0);
    lv_obj_set_flex_flow(top_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_bar_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // 创建图标（预留）
    top_bar_icon_ = lv_img_create(top_bar_);
    lv_obj_set_size(top_bar_icon_, 24, 24);
    lv_obj_add_flag(top_bar_icon_, LV_OBJ_FLAG_HIDDEN);  // 默认隐藏

    // 创建文字标签
    top_bar_label_ = lv_label_create(top_bar_);
    lv_label_set_text(top_bar_label_, "");
    lv_obj_set_style_text_color(top_bar_label_, lv_color_white(), 0);
    lv_obj_set_style_text_font(top_bar_label_, &lv_font_montserrat_16, 0);
    lv_obj_add_flag(top_bar_label_, LV_OBJ_FLAG_HIDDEN);  // 默认隐藏

    // 默认隐藏整个状态栏
    lv_obj_add_flag(top_bar_, LV_OBJ_FLAG_HIDDEN);

    ESP_LOGI(TAG, "Top bar created (height: %d)", top_bar_config_.height);
}

void EmotionDisplay::CreateAnimationArea() {
    // 创建动画容器，占据除顶部状态栏外的所有空间
    animation_container_ = lv_obj_create(screen_);
    
    int anim_height = height_ - (top_bar_config_.visible ? top_bar_config_.height : 0);
    int anim_y = top_bar_config_.visible ? top_bar_config_.height : 0;
    
    lv_obj_set_size(animation_container_, width_, anim_height);
    lv_obj_set_pos(animation_container_, 0, anim_y);
    lv_obj_set_style_bg_opa(animation_container_, LV_OPA_TRANSP, 0);  // 透明背景
    lv_obj_set_style_border_width(animation_container_, 0, 0);
    lv_obj_set_style_pad_all(animation_container_, 0, 0);
    lv_obj_set_scrollbar_mode(animation_container_, LV_SCROLLBAR_MODE_OFF);

    ESP_LOGI(TAG, "Animation area created (%dx%d at y=%d)", width_, anim_height, anim_y);
}

void EmotionDisplay::OnTopBarTimeout(lv_timer_t* timer) {
    EmotionDisplay* display = static_cast<EmotionDisplay*>(timer->user_data);
    if (display) {
        display->HideTopBar();
    }
}

} // namespace display

