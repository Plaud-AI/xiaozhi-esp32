#include "aaf_display_widget.h"
#include "aaf_animation_config.h"
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <esp_psram.h>
#include <lvgl.h>
#include <string.h>
#include "assets/lang_config.h"

static const char* TAG = "AafDisplayWidget";

namespace xiaozhi {
namespace display {

// ===== ScreenConfig 静态方法 =====

AafDisplayWidget::ScreenConfig AafDisplayWidget::ScreenConfig::CreateForResolution(int width, int height) {
    ScreenConfig config;
    config.width = width;
    config.height = height;
    
    // 状态栏高度：屏幕高度的 15-20%
    config.status_bar_height = height / 5;
    if (config.status_bar_height < 16) config.status_bar_height = 16;
    if (config.status_bar_height > 40) config.status_bar_height = 40;
    
    // 动画画布：剩余区域
    config.animation_canvas.x = 0;
    config.animation_canvas.y = config.status_bar_height;
    config.animation_canvas.width = width;
    config.animation_canvas.height = height - config.status_bar_height;
    
    return config;
}

// ===== AafDisplayWidget 实现 =====

AafDisplayWidget::AafDisplayWidget(esp_lcd_panel_io_handle_t panel_io,
                                   esp_lcd_panel_handle_t panel,
                                   int width, int height)
    : panel_io_(panel_io)
    , panel_(panel)
    , lvgl_display_(nullptr)
    , status_bar_(nullptr)
    , animation_canvas_(nullptr)
    , timeout_timer_(nullptr) {
    
    ESP_LOGI(TAG, "Creating AafDisplayWidget: %dx%d", width, height);
    
    // 初始化屏幕配置
    screen_config_ = ScreenConfig::CreateForResolution(width, height);
    
    ESP_LOGI(TAG, "Screen config: status_bar_h=%d, canvas=(%d,%d,%d,%d)",
             screen_config_.status_bar_height,
             screen_config_.animation_canvas.x,
             screen_config_.animation_canvas.y,
             screen_config_.animation_canvas.width,
             screen_config_.animation_canvas.height);
    
    // 先初始化 LVGL port（必须在创建 UI 之前）
    InitializeLvgl();
    
    // 初始化各个组件
    if (!InitializeResources()) {
        ESP_LOGE(TAG, "Failed to initialize resources");
        return;
    }
    
    if (!InitializeAnimationPlayer()) {
        ESP_LOGE(TAG, "Failed to initialize animation player");
        return;
    }
    
    if (!InitializeStateManager()) {
        ESP_LOGE(TAG, "Failed to initialize state manager");
        return;
    }
    
    // 初始化 UI
    InitializeUI();
    
    // 创建超时定时器
    esp_timer_create_args_t timer_args = {
        .callback = OnTimeoutTimer,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "emotion_timeout",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&timer_args, &timeout_timer_);
    
    ESP_LOGI(TAG, "AafDisplayWidget created successfully");
}

AafDisplayWidget::~AafDisplayWidget() {
    ESP_LOGI(TAG, "Destroying AafDisplayWidget");
    
    // 停止定时器
    if (timeout_timer_) {
        esp_timer_stop(timeout_timer_);
        esp_timer_delete(timeout_timer_);
        timeout_timer_ = nullptr;
    }
    
    // 组件会自动销毁（unique_ptr）
}

void AafDisplayWidget::SetStatus(const char* status) {
    if (!status) {
        ESP_LOGW(TAG, "Invalid status");
        return;
    }
    
    ESP_LOGI(TAG, "SetStatus: %s", status);
    
    // 映射 status 字符串到设备状态
    // 这里需要根据实际的 Lang::Strings 定义来映射
    AnimationStateManager::DeviceState device_state = AnimationStateManager::DeviceState::Unknown;
    
    if (strcmp(status, Lang::Strings::STANDBY) == 0) {
        device_state = AnimationStateManager::DeviceState::Idle;
    } else if (strcmp(status, Lang::Strings::LISTENING) == 0) {
        device_state = AnimationStateManager::DeviceState::Listening;
    } else if (strcmp(status, Lang::Strings::SPEAKING) == 0) {
        device_state = AnimationStateManager::DeviceState::Speaking;
    } else if (strcmp(status, Lang::Strings::INITIALIZING) == 0) {
        device_state = AnimationStateManager::DeviceState::Loading;
    } else {
        ESP_LOGW(TAG, "Unknown status string: %s", status);
        return;
    }
    
    // 切换状态
    state_manager_->SetDeviceState(device_state);
}

void AafDisplayWidget::SetEmotion(const char* emotion) {
    if (!emotion) {
        ESP_LOGW(TAG, "Invalid emotion");
        return;
    }
    
    ESP_LOGI(TAG, "SetEmotion: %s", emotion);
    
    // 切换感情状态（使用默认优先级和超时）
    state_manager_->SetEmotionState(emotion);
}

void AafDisplayWidget::SetChatMessage(const char* role, const char* content) {
    // AAF 显示系统主要显示动画，文本消息可以显示在状态栏或忽略
    // 这里可以根据需要实现文本显示逻辑
    ESP_LOGD(TAG, "SetChatMessage: role=%s, content=%s", role, content);
    
    // TODO: 可选实现文本显示
}

void AafDisplayWidget::SetTransitionEnabled(bool enabled) {
    transition_config_.enabled = enabled;
    ESP_LOGI(TAG, "Transition %s", enabled ? "enabled" : "disabled");
}

void AafDisplayWidget::SetTransitionDuration(int duration_ms) {
    transition_config_.duration_ms = duration_ms;
    ESP_LOGI(TAG, "Transition duration set to %d ms", duration_ms);
}

bool AafDisplayWidget::Lock(int timeout_ms) {
    return lvgl_port_lock(timeout_ms);
}

void AafDisplayWidget::Unlock() {
    lvgl_port_unlock();
}

void AafDisplayWidget::InitializeLvgl() {
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
    // 优化任务调度：
    // - 优先级 3：高于 IDLE(0) 和 opus_codec(2)，低于 audio 任务(4-8)
    // - 不固定 CPU：让调度器灵活分配，避免阻塞 Core 1 的 IDLE 任务
    port_cfg.task_priority = 3;
    // task_affinity 默认为 -1 (不固定)，无需设置
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding LCD display");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,
        .panel_handle = panel_,
        .control_handle = nullptr,
        .buffer_size = static_cast<uint32_t>(screen_config_.width * 20),
        .double_buffer = false,
        .trans_size = 0,
        .hres = static_cast<uint32_t>(screen_config_.width),
        .vres = static_cast<uint32_t>(screen_config_.height),
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

    lvgl_display_ = lvgl_port_add_disp(&display_cfg);
    if (lvgl_display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add display");
        return;
    }

    ESP_LOGI(TAG, "LVGL initialized successfully");
}

bool AafDisplayWidget::InitializeResources() {
    ESP_LOGI(TAG, "Initializing animation resources");
    
    resource_manager_ = std::make_unique<AnimationResourceManager>();
    
    // 方式一：使用 mmap_assets（推荐，零拷贝，不占用 SRAM）
    // 需要先用 mmap_assets 工具打包动画到 assets 分区
    AnimationResourceManager::PartitionConfig mmap_config = {
        .partition_label = "assets",
        .max_files = 8,       // 8 个设备状态动画
        .fps_array = nullptr, // 使用默认 FPS
        .checksum = 0,        // 跳过校验
    };
    
    esp_err_t ret = resource_manager_->InitFromPartition(mmap_config);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ Loaded %d animations from mmap partition (zero-copy)", 
                 resource_manager_->GetAnimationCount());
        return true;
    }
    
    // 方式二：回退到文件系统（占用 SRAM，仅用于测试）
    ESP_LOGW(TAG, "Failed to load from partition (%s), trying file system...", 
             esp_err_to_name(ret));
    
    AnimationResourceManager::AnimationPath animation_paths[] = {
        {"/assets/animations/idle.aaf",       AnimationConfig::GetRecommendedFps("idle"),       "idle"},
        {"/assets/animations/listening.aaf",  AnimationConfig::GetRecommendedFps("listening"),  "listening"},
        {"/assets/animations/speaking.aaf",   AnimationConfig::GetRecommendedFps("speaking"),   "speaking"},
        {"/assets/animations/loading.aaf",    AnimationConfig::GetRecommendedFps("loading"),    "loading"},
        {"/assets/animations/settings.aaf",   AnimationConfig::GetRecommendedFps("settings"),   "settings"},
        {"/assets/animations/updating.aaf",   AnimationConfig::GetRecommendedFps("updating"),   "updating"},
        {"/assets/animations/success.aaf",    AnimationConfig::GetRecommendedFps("success"),    "success"},
        {"/assets/animations/error.aaf",      AnimationConfig::GetRecommendedFps("error"),      "error"},
    };
    
    ret = resource_manager_->InitFromFileSystem(animation_paths, 8);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load animations from file system: %s", esp_err_to_name(ret));
        ESP_LOGW(TAG, "⚠️  Continuing without animations");
        // 即使失败也继续，只是没有动画
    } else {
        ESP_LOGW(TAG, "✅ Loaded %d animations from file system (using ~1.6MB SRAM!)", 
                 resource_manager_->GetAnimationCount());
    }
    
    return true;  // 即使动画加载失败，UI 仍然可以工作
}

bool AafDisplayWidget::InitializeStateManager() {
    ESP_LOGI(TAG, "Initializing state manager");
    
    state_manager_ = std::make_unique<AnimationStateManager>();
    
    // 注册状态变更回调
    state_manager_->SetStateChangeCallback(
        [this](const AnimationStateManager::StateChangeEvent& event) {
            OnStateChanged(event);
        }
    );
    
    // 注册设备状态映射
    // 示例：这里需要根据实际的动画索引来配置
    state_manager_->RegisterDeviceStateMapping(
        AnimationStateManager::DeviceState::Idle,
        "idle.aaf", 0, true, 15, AnimationStateManager::Priority::Normal
    );
    
    state_manager_->RegisterDeviceStateMapping(
        AnimationStateManager::DeviceState::Listening,
        "listening.aaf", 1, true, 20, AnimationStateManager::Priority::Normal
    );
    
    state_manager_->RegisterDeviceStateMapping(
        AnimationStateManager::DeviceState::Speaking,
        "speaking.aaf", 2, true, 20, AnimationStateManager::Priority::Normal
    );
    
    state_manager_->RegisterDeviceStateMapping(
        AnimationStateManager::DeviceState::Loading,
        "loading.aaf", 3, true, 15, AnimationStateManager::Priority::Normal
    );
    
    state_manager_->RegisterDeviceStateMapping(
        AnimationStateManager::DeviceState::Error,
        "error.aaf", 7, true, 15, AnimationStateManager::Priority::Critical
    );
    
    // 注册感情状态映射（示例）
    state_manager_->RegisterEmotionMapping(
        "happy", "happy.aaf", 8, true, 20, AnimationStateManager::Priority::High
    );
    
    state_manager_->RegisterEmotionMapping(
        "sad", "sad.aaf", 9, true, 15, AnimationStateManager::Priority::High
    );
    
    ESP_LOGI(TAG, "State manager initialized successfully");
    return true;
}

bool AafDisplayWidget::InitializeAnimationPlayer() {
    ESP_LOGI(TAG, "Initializing animation player");
    
    // 使用构造函数中保存的 panel_io 和 panel
    animation_player_ = std::make_unique<AafAnimationPlayer>(
        panel_io_,
        panel_,
        screen_config_.animation_canvas.x,
        screen_config_.animation_canvas.y,
        screen_config_.animation_canvas.width,
        screen_config_.animation_canvas.height
    );
    
    // 注册动画结束回调
    animation_player_->SetAnimationEndCallback([this]() {
        ESP_LOGI(TAG, "Animation ended");
        
        // 如果是感情状态，恢复到上一个状态
        if (state_manager_->IsEmotionActive()) {
            ESP_LOGI(TAG, "Emotion animation ended, restoring previous state");
            state_manager_->RestorePreviousState();
        }
    });
    
    ESP_LOGI(TAG, "Animation player initialized successfully");
    return true;
}

void AafDisplayWidget::InitializeUI() {
    ESP_LOGI(TAG, "Initializing UI");
    
    // 创建 LVGL 对象
    // 注意：这里需要在 LVGL 锁内执行
    Lock();
    
    auto screen = lv_screen_active();
    
    // 创建状态栏
    status_bar_ = lv_obj_create(screen);
    lv_obj_set_size(status_bar_, screen_config_.width, screen_config_.status_bar_height);
    lv_obj_set_pos(status_bar_, 0, 0);
    lv_obj_set_style_bg_color(status_bar_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    
    // 创建动画画布区域（可选，用于显示边框或背景）
    animation_canvas_ = lv_obj_create(screen);
    lv_obj_set_size(animation_canvas_, 
                    screen_config_.animation_canvas.width,
                    screen_config_.animation_canvas.height);
    lv_obj_set_pos(animation_canvas_,
                   screen_config_.animation_canvas.x,
                   screen_config_.animation_canvas.y);
    lv_obj_set_style_bg_color(animation_canvas_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(animation_canvas_, 0, 0);
    
    Unlock();
    
    ESP_LOGI(TAG, "UI initialized successfully");
}

void AafDisplayWidget::OnStateChanged(const AnimationStateManager::StateChangeEvent& event) {
    ESP_LOGI(TAG, "State changed: %s (type=%d, priority=%d)",
             event.state_name.c_str(),
             static_cast<int>(event.type),
             static_cast<int>(event.priority));
    
    // 停止之前的超时定时器
    StopTimeoutTimer();
    
    // 获取动画数据
    size_t data_size = 0;
    int fps = event.fps;
    const void* data = nullptr;
    
    if (event.animation_index >= 0) {
        // 使用索引获取
        data = resource_manager_->GetAnimationData(event.animation_index, &data_size, &fps);
    } else if (!event.animation_file.empty()) {
        // 使用文件名获取
        data = resource_manager_->GetAnimationData(event.animation_file.c_str(), &data_size, &fps);
    }
    
    if (!data || data_size == 0) {
        ESP_LOGE(TAG, "Failed to get animation data for: %s", event.state_name.c_str());
        return;
    }
    
    // 配置播放参数
    AafAnimationPlayer::PlaybackConfig play_config;
    play_config.data_address = data;
    play_config.data_length = data_size;
    play_config.mode = event.loop ? AafAnimationPlayer::PlayMode::Loop 
                                   : AafAnimationPlayer::PlayMode::Once;
    play_config.fps = fps;
    play_config.interrupt_current = true;
    
    // 播放动画
    if (!animation_player_->Play(play_config)) {
        ESP_LOGE(TAG, "Failed to play animation");
        return;
    }
    
    // 如果有超时时间，启动定时器
    if (event.timeout_ms > 0) {
        StartTimeoutTimer(event.timeout_ms);
    }
    
    // 更新状态栏
    UpdateStatusBar();
}

void AafDisplayWidget::UpdateStatusBar() {
    // TODO: 更新状态栏显示
    // 可以显示当前状态名称、电池、网络等信息
}

void AafDisplayWidget::StartTimeoutTimer(uint32_t timeout_ms) {
    if (!timeout_timer_) {
        return;
    }
    
    ESP_LOGI(TAG, "Starting timeout timer: %lu ms", timeout_ms);
    
    esp_timer_stop(timeout_timer_);
    esp_timer_start_once(timeout_timer_, timeout_ms * 1000);  // 转换为微秒
}

void AafDisplayWidget::StopTimeoutTimer() {
    if (!timeout_timer_) {
        return;
    }
    
    esp_timer_stop(timeout_timer_);
}

void AafDisplayWidget::OnTimeoutTimer(void* arg) {
    auto* self = static_cast<AafDisplayWidget*>(arg);
    if (!self) {
        return;
    }
    
    ESP_LOGI(TAG, "Emotion timeout, restoring previous state");
    
    // 恢复到上一个状态
    self->state_manager_->RestorePreviousState();
}

} // namespace display
} // namespace xiaozhi

