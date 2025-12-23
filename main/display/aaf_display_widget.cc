#include "aaf_display_widget.h"
#include "aaf_animation_config.h"
#include "emotion_mapper.h"
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <esp_psram.h>
#include <esp_heap_caps.h>
#include <lvgl.h>
#include <string.h>
#include <string>
#include <algorithm>
#include "assets/lang_config.h"
#include "ble_wifi_provisioner.h"
#include <font_awesome.h>

static const char* TAG = "AafDisplayWidget";

namespace xiaozhi {
namespace display {

// ===== ScreenConfig 静态方法 =====

AafDisplayWidget::ScreenConfig AafDisplayWidget::ScreenConfig::CreateForResolution(int width, int height) {
    ScreenConfig config;
    config.width = width;
    config.height = height;

    // **修复白屏问题**: 移除状态栏，让动画覆盖整个屏幕
    // 这避免了 LVGL 刷新状态栏时覆盖动画区域的问题
    config.status_bar_height = 0;

    // 动画画布配置
    // AAF 动画文件是 240x240 像素，屏幕是 320x240
    // 动画水平居中，垂直方向完整显示
    const int ANIM_SIZE = 240;   // AAF 动画文件的尺寸
    
    // 水平居中
    if (width > ANIM_SIZE) {
        config.animation_canvas.x = (width - ANIM_SIZE) / 2;
        config.animation_canvas.width = ANIM_SIZE;
    } else {
        config.animation_canvas.x = 0;
        config.animation_canvas.width = width;
    }
    
    // 垂直方向：从顶部开始，显示完整动画
    config.animation_canvas.y = 0;
    config.animation_canvas.height = std::min(ANIM_SIZE, height);
    
    ESP_LOGI("AafDisplayWidget", "Screen: %dx%d, StatusBar: %d, Canvas: (%d,%d) %dx%d",
             width, height, config.status_bar_height,
             config.animation_canvas.x, config.animation_canvas.y,
             config.animation_canvas.width, config.animation_canvas.height);
    
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
    , ble_icon_label_(nullptr)
    , timeout_timer_(nullptr)
    , trans_done_sem_(nullptr) {
    
    ESP_LOGI(TAG, "Creating AafDisplayWidget: %dx%d", width, height);

    // 创建 LCD 传输完成信号量（二进制信号量，初始为"空"状态）
    // 注意：不要在初始化时给出信号量！
    // 正确的流程：DMA 传输 -> ISR 给出信号量 -> xSemaphoreTake 成功
    trans_done_sem_ = xSemaphoreCreateBinary();
    if (!trans_done_sem_) {
        ESP_LOGE(TAG, "Failed to create transfer done semaphore");
    }
    // 不调用 xSemaphoreGive，让信号量保持"空"状态
    // 第一次 DMA 传输完成后，ISR 会给出信号量
    
    // **关键**：在任何 LCD 绘制操作之前注册传输完成回调
    // 否则 DrawBitmapToLcd 中的 DMA 同步会超时
    if (panel_io_) {
        esp_lcd_panel_io_callbacks_t cbs = {
            .on_color_trans_done = OnLcdTransferDone,
        };
        esp_err_t ret = esp_lcd_panel_io_register_event_callbacks(panel_io_, &cbs, this);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register LCD transfer done callback: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "LCD transfer done callback registered");
        }
    }
    
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
    
    // 初始化资源管理器
    if (!InitializeResources()) {
        ESP_LOGE(TAG, "Failed to initialize resources");
        return;
    }
    
    // 初始化 UI（设置屏幕背景为黑色，创建状态栏）
    InitializeUI();
    
    // 初始化动画播放器（直接 LCD 绘制模式）
    if (!InitializeAnimationPlayer()) {
        ESP_LOGE(TAG, "Failed to initialize animation player");
        return;
    }
    
    // 初始化状态管理器
    if (!InitializeStateManager()) {
        ESP_LOGE(TAG, "Failed to initialize state manager");
        return;
    }
    
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
    
    // 释放信号量
    if (trans_done_sem_) {
        vSemaphoreDelete(trans_done_sem_);
        trans_done_sem_ = nullptr;
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
    AnimationStateManager::DeviceState device_state = AnimationStateManager::DeviceState::Unknown;
    
    // 待机/空闲状态（中文 + 英文）
    if (strcmp(status, Lang::Strings::STANDBY) == 0 ||
        strcmp(status, "idle") == 0 ||
        strcmp(status, "Idle") == 0) {
        device_state = AnimationStateManager::DeviceState::Idle;
    }
    // 聆听状态（中文 + 英文）
    else if (strcmp(status, Lang::Strings::LISTENING) == 0 ||
             strcmp(status, "listening") == 0 ||
             strcmp(status, "Listening") == 0) {
        device_state = AnimationStateManager::DeviceState::Listening;
    }
    // 说话状态（中文 + 英文）
    else if (strcmp(status, Lang::Strings::SPEAKING) == 0 ||
             strcmp(status, "speaking") == 0 ||
             strcmp(status, "Speaking") == 0) {
        device_state = AnimationStateManager::DeviceState::Speaking;
    }
    // 加载/初始化状态（中文 + 英文）
    else if (strcmp(status, Lang::Strings::INITIALIZING) == 0 ||
             strcmp(status, Lang::Strings::LOADING_PROTOCOL) == 0 ||
             strcmp(status, Lang::Strings::LOADING_ASSETS) == 0 ||
             strcmp(status, Lang::Strings::PLEASE_WAIT) == 0 ||
             strcmp(status, Lang::Strings::CHECKING_NEW_VERSION) == 0 ||
             strcmp(status, "loading") == 0 ||
             strcmp(status, "Loading") == 0) {
        device_state = AnimationStateManager::DeviceState::Loading;
    }
    // 连接状态（中文 + 英文）
    else if (strcmp(status, Lang::Strings::CONNECTING) == 0 ||
             strcmp(status, Lang::Strings::SCANNING_WIFI) == 0 ||
             strcmp(status, Lang::Strings::REGISTERING_NETWORK) == 0 ||
             strcmp(status, "connecting") == 0 ||
             strcmp(status, "Connecting") == 0) {
        device_state = AnimationStateManager::DeviceState::Loading;
    }
    // 升级状态（中文 + 英文）
    else if (strcmp(status, Lang::Strings::UPGRADING) == 0 ||
             strcmp(status, "updating") == 0 ||
             strcmp(status, "Updating") == 0 ||
             strcmp(status, "upgrading") == 0 ||
             strcmp(status, "Upgrading") == 0) {
        device_state = AnimationStateManager::DeviceState::Updating;
    }
    // 配置状态（中文 + 英文）
    else if (strcmp(status, Lang::Strings::CONFIGURING) == 0 ||
             strcmp(status, Lang::Strings::WIFI_CONFIG_MODE) == 0 ||
             strcmp(status, Lang::Strings::ENTERING_WIFI_CONFIG_MODE) == 0 ||
             strcmp(status, "settings") == 0 ||
             strcmp(status, "Settings") == 0) {
        device_state = AnimationStateManager::DeviceState::Settings;
    }
    // 成功状态（英文）
    else if (strcmp(status, "success") == 0 ||
             strcmp(status, "Success") == 0) {
        device_state = AnimationStateManager::DeviceState::Success;
    }
    // 错误状态（中文 + 英文）
    else if (strcmp(status, Lang::Strings::ERROR) == 0 ||
             strcmp(status, Lang::Strings::SERVER_ERROR) == 0 ||
             strcmp(status, Lang::Strings::SERVER_NOT_CONNECTED) == 0 ||
             strcmp(status, Lang::Strings::UPGRADE_FAILED) == 0 ||
             strcmp(status, "error") == 0 ||
             strcmp(status, "Error") == 0) {
        device_state = AnimationStateManager::DeviceState::Error;
    }
    // 激活状态 - 视为加载
    else if (strcmp(status, Lang::Strings::ACTIVATION) == 0) {
        device_state = AnimationStateManager::DeviceState::Loading;
    }
    else {
        // 未知状态，使用加载动画作为默认
        ESP_LOGW(TAG, "Unknown status string: %s, using Loading as fallback", status);
        device_state = AnimationStateManager::DeviceState::Loading;
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
    
    // 使用 EmotionMapper 进行情感映射
    // EmotionMapper 负责：
    // 1. 验证情感是否已注册
    // 2. 返回映射的动画信息
    // 3. 记录情感变化（用于日志和统计）
    auto& emotion_mapper = EmotionMapper::GetInstance();
    auto mapping_result = emotion_mapper.MapEmotion(emotion, EmotionMapper::InputSource::Manual);
    
    if (mapping_result.success && state_manager_) {
        // 通过状态管理器的情感状态接口触发动画
        // 情感状态具有高优先级，会打断当前的设备状态动画
        // 并且有超时自动恢复机制
        if (!state_manager_->SetEmotionState(emotion)) {
            ESP_LOGW(TAG, "SetEmotionState failed for '%s'", emotion);
        }
    } else if (state_manager_) {
        // 如果情感未在 EmotionMapper 中注册，尝试作为设备状态处理（兼容性）
        ESP_LOGW(TAG, "Emotion '%s' not in EmotionMapper, trying as device state", emotion);
        
        // 映射常见字符串到设备状态
        using DeviceState = AnimationStateManager::DeviceState;
        DeviceState mapped_state = DeviceState::Idle;
        
        if (strcmp(emotion, "idle") == 0) {
            mapped_state = DeviceState::Idle;
        } else if (strcmp(emotion, "listening") == 0) {
            mapped_state = DeviceState::Listening;
        } else if (strcmp(emotion, "speaking") == 0) {
            mapped_state = DeviceState::Speaking;
        } else if (strcmp(emotion, "loading") == 0) {
            mapped_state = DeviceState::Loading;
        } else if (strcmp(emotion, "settings") == 0) {
            mapped_state = DeviceState::Settings;
        } else if (strcmp(emotion, "updating") == 0) {
            mapped_state = DeviceState::Updating;
        } else if (strcmp(emotion, "success") == 0) {
            mapped_state = DeviceState::Success;
        } else if (strcmp(emotion, "error") == 0) {
            mapped_state = DeviceState::Error;
        }
        
        state_manager_->SetDeviceState(mapped_state);
    }
}

void AafDisplayWidget::ShowAnimationByPath(const char* animation_path, bool loop) {
    // 第一阶段：动画由状态管理器统一驱动
    ESP_LOGD(TAG, "ShowAnimationByPath: %s (ignored, using state-driven AAF)", 
             animation_path ? animation_path : "null");
}

void AafDisplayWidget::SetChatMessage(const char* role, const char* content) {
    ESP_LOGD(TAG, "SetChatMessage: role=%s, content=%s", role, content);
    // AAF 显示系统主要显示动画，文本消息暂不实现
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
    // Direct LCD mode: 不使用 LVGL port，所以不需要锁
    // 如果需要线程安全，可以在这里添加自己的互斥锁
    (void)timeout_ms;
    return true;
}

void AafDisplayWidget::Unlock() {
    // Direct LCD mode: 不使用 LVGL port，所以不需要解锁
}

void AafDisplayWidget::InitializeLvgl() {
    ESP_LOGI(TAG, "LVGL completely disabled (Direct LCD mode)");
    
    // **完全移除 LVGL**:
    // 不调用 lv_init()，因为 LVGL 即使没有 display 也可能有后台定时器运行
    // 这彻底避免了任何潜在的 LVGL 干扰
    
    // 不调用任何 LVGL 函数
    lvgl_display_ = nullptr;
    
    ESP_LOGI(TAG, "LVGL completely disabled - using direct LCD mode only");
}

bool AafDisplayWidget::InitializeResources() {
    ESP_LOGI(TAG, "Initializing animation resources");
    
    resource_manager_ = std::make_unique<AnimationResourceManager>();
    
    // 使用项目 Assets 类加载 AAF 动画
    esp_err_t ret = resource_manager_->InitFromAssetsDefault();
    if (ret == ESP_OK && resource_manager_->GetAnimationCount() > 0) {
        ESP_LOGI(TAG, "✅ Loaded %d AAF animations from Assets partition", 
                 resource_manager_->GetAnimationCount());
        return true;
    }
    
    // 如果 Assets 加载失败，尝试从文件系统加载
    ESP_LOGW(TAG, "No AAF animations in Assets, trying file system...");
    AnimationResourceManager::AnimationPath animation_paths[] = {
        {"/spiffs/animations/idle.aaf",       15, "idle.aaf"},
        {"/spiffs/animations/listening.aaf",  20, "listening.aaf"},
        {"/spiffs/animations/speaking.aaf",   20, "speaking.aaf"},
        {"/spiffs/animations/loading.aaf",    15, "loading.aaf"},
        {"/spiffs/animations/settings.aaf",   15, "settings.aaf"},
        {"/spiffs/animations/updating.aaf",   15, "updating.aaf"},
        {"/spiffs/animations/success.aaf",    15, "success.aaf"},
        {"/spiffs/animations/error.aaf",      15, "error.aaf"},
    };
    
    ret = resource_manager_->InitFromFileSystem(animation_paths, 8);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ Loaded %d animations from file system", 
                 resource_manager_->GetAnimationCount());
        return true;
    }
    
    // 如果都失败，继续运行但无动画
    ESP_LOGW(TAG, "⚠️  No AAF animations available");
    return true;
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
    state_manager_->RegisterDeviceStateMapping(
        AnimationStateManager::DeviceState::Idle,
        "idle.aaf", 0, true, 15, AnimationStateManager::Priority::Normal
    );
    
    // **DEBUG FIX**: 临时使用 idle.aaf 替代 listening.aaf（测试是否是 listening.aaf 尺寸问题）
    // TODO: 一旦确认是 listening.aaf 的问题，需要替换为正确尺寸（240x240）的动画文件
    state_manager_->RegisterDeviceStateMapping(
        AnimationStateManager::DeviceState::Listening,
        "idle.aaf", 0, true, 15, AnimationStateManager::Priority::Normal  // 使用 idle.aaf 替代
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
        AnimationStateManager::DeviceState::Settings,
        "settings.aaf", 4, true, 15, AnimationStateManager::Priority::Normal
    );
    
    state_manager_->RegisterDeviceStateMapping(
        AnimationStateManager::DeviceState::Updating,
        "updating.aaf", 5, true, 15, AnimationStateManager::Priority::Normal
    );
    
    state_manager_->RegisterDeviceStateMapping(
        AnimationStateManager::DeviceState::Success,
        "success.aaf", 6, false, 15, AnimationStateManager::Priority::Normal
    );
    
    state_manager_->RegisterDeviceStateMapping(
        AnimationStateManager::DeviceState::Error,
        "error.aaf", 7, true, 15, AnimationStateManager::Priority::Critical
    );
    
    // ========================================
    // 使用 EmotionMapper 注册情感状态映射
    // EmotionMapper 是独立的情感映射模块，负责：
    // 1. 管理情感名称到动画的映射
    // 2. 提供 LLM 情感解析接口（预留）
    // 3. 支持运行时动态注册新情感
    // ========================================
    auto& emotion_mapper = EmotionMapper::GetInstance();
    emotion_mapper.Initialize();
    
    // 从 EmotionMapper 获取所有注册的情感，并注册到状态管理器
    auto emotions = emotion_mapper.GetRegisteredEmotions();
    ESP_LOGI(TAG, "Registering %zu emotions from EmotionMapper...", emotions.size());
    
    for (const auto& emotion_name : emotions) {
        auto info = emotion_mapper.GetEmotionInfo(emotion_name);
        if (!info.name.empty()) {
            state_manager_->RegisterEmotionMapping(
                info.name.c_str(),
                info.mapped_animation.c_str(),
                info.animation_index,
                info.is_loop,
                info.fps,
                AnimationStateManager::Priority::High
            );
        }
    }
    
    // 设置情感变化回调（用于日志）
    emotion_mapper.SetEmotionChangeCallback(
        [](const std::string& emotion, EmotionMapper::InputSource source,
           const EmotionMappingResult& result) {
            ESP_LOGI("EmotionMapper", "Emotion changed: %s (source=%s, anim=%s)",
                     emotion.c_str(), InputSourceToString(source),
                     result.animation_name.c_str());
        }
    );
    
    ESP_LOGI(TAG, "State manager initialized (device + %zu emotion states)", emotions.size());
    return true;
}

bool AafDisplayWidget::InitializeAnimationPlayer() {
    ESP_LOGI(TAG, "Initializing animation player (Direct LCD mode)");
    
    // 创建动画播放器
    animation_player_ = std::make_unique<AafAnimationPlayer>();
    
    // 配置播放器初始化数据
    AnimPlayerInitData init_data = {
        .canvas = {
            .coord_x = screen_config_.animation_canvas.x,
            .coord_y = screen_config_.animation_canvas.y,
            .width = screen_config_.animation_canvas.width,
            .height = screen_config_.animation_canvas.height,
        },
        .task = {
            .task_priority = 4,           // 与官方示例一致
            .task_stack = 7168,           // 与官方示例一致
            .task_affinity = 1,           // 绑定到 CPU1
            .task_stack_in_ext = true,    // 使用外部 SRAM
        },
        .flags = {
            .enable_data_swap_bytes = 1,  // RGB565 字节交换
        },
    };
    
    // 初始化播放器
    if (!animation_player_->Begin(init_data)) {
        ESP_LOGE(TAG, "Failed to begin animation player");
        return false;
    }
    
    // 设置帧刷新回调（直接 LCD 绘制）
    animation_player_->SetFlushCallback(OnAnimationFlush, this);
    
    // 设置动画停止回调（清空画面）
    animation_player_->SetStopCallback(OnAnimationStop, this);
    
    ESP_LOGI(TAG, "Animation player initialized successfully (Direct LCD mode)");
    return true;
}

void AafDisplayWidget::InitializeUI() {
    ESP_LOGI(TAG, "Initializing UI (Direct LCD mode)");
    
    // 清空屏幕为黑色
    ESP_LOGI(TAG, "Clearing entire screen to black...");
    ClearLcdArea(0, 0, screen_config_.width, screen_config_.height);
    
    // Direct LCD mode: 不使用 LVGL display，不需要设置任何回调
    // 不创建状态栏和其他 LVGL 对象
    status_bar_ = nullptr;
    ble_icon_label_ = nullptr;
    
    ESP_LOGI(TAG, "UI initialized successfully (Direct LCD mode)");
}

// ===== LCD 直接绘制方法（核心改进）=====

bool AafDisplayWidget::DrawBitmapToLcd(int x_start, int y_start, int x_end, int y_end, const void* data) {
    // **同步模式**: 发起 DMA 传输并等待完成
    // 这确保了每帧完全传输后才进行下一帧，避免 DMA 竞争问题
    
    // 诊断计数器
    static int draw_count = 0;
    static int draw_success_count = 0;
    static int draw_timeout_count = 0;
    static int draw_error_count = 0;
    draw_count++;
    
    // 调试日志：每1000次绘制输出一次（debug级别，避免刷屏）
    if (draw_count % 1000 == 1) {
        ESP_LOGD(TAG, "DrawBitmapToLcd #%d: (%d,%d)-(%d,%d), panel=%p, data=%p",
                 draw_count, x_start, y_start, x_end, y_end, panel_, data);
    }
    
    if (!panel_ || !data) {
        draw_error_count++;
        if (draw_error_count <= 5) {
            ESP_LOGE(TAG, "DrawBitmapToLcd: panel=%p, data=%p (NULL check failed)", panel_, data);
        }
        return false;
    }
    
    // 边界检查（坐标已在 anim_player 回调中预处理）
    if (x_start >= x_end || y_start >= y_end) {
        return true;  // 无效区域，静默跳过
    }
    
    // 确保坐标在屏幕范围内
    if (x_start < 0 || y_start < 0 || x_end > screen_config_.width || y_end > screen_config_.height) {
        return true;  // 坐标超出屏幕，静默跳过
    }
    
    // 加锁保护绘制过程（确保同一时间只有一个绘制操作）
    std::lock_guard<std::mutex> lock(draw_mutex_);
    
    // 发起 DMA 传输
    esp_err_t ret = esp_lcd_panel_draw_bitmap(panel_, x_start, y_start, x_end, y_end, data);
    
    if (ret != ESP_OK) {
        draw_error_count++;
        ESP_LOGE(TAG, "esp_lcd_panel_draw_bitmap failed: %s (count=%d)", esp_err_to_name(ret), draw_error_count);
        return false;
    }
    
    // 等待 DMA 传输完成（ISR 回调会给出信号量）
    // 注意：trans_queue_depth=1 确保同一时间只有一个传输
    if (trans_done_sem_) {
        if (xSemaphoreTake(trans_done_sem_, pdMS_TO_TICKS(200)) != pdTRUE) {
            draw_timeout_count++;
            ESP_LOGW(TAG, "DrawBitmapToLcd: DMA transfer timeout! (count=%d, total=%d)", 
                     draw_timeout_count, draw_count);
            // 超时：尝试恢复，给出信号量以便下次传输
            xSemaphoreGive(trans_done_sem_);
        } else {
            draw_success_count++;
        }
    }
    
    
    return true;
}

// 同步版本：与 DrawBitmapToLcd 相同（现在都是同步的）
bool AafDisplayWidget::DrawBitmapToLcdSync(int x_start, int y_start, int x_end, int y_end, const void* data) {
    // 现在 DrawBitmapToLcd 已经是同步的了，直接调用它
    return DrawBitmapToLcd(x_start, y_start, x_end, y_end, data);
}

bool AafDisplayWidget::ClearLcdArea(int x_start, int y_start, int x_end, int y_end) {
    if (!panel_) {
        return false;
    }
    
    int width = x_end - x_start;
    int height = y_end - y_start;
    
    if (width <= 0 || height <= 0) {
        return false;
    }
    
    // **对齐参考项目**: 分配整个清屏区域的缓冲区
    // 参考: products/speaker/main/modules/display.cpp::clear_display()
    size_t buffer_size = width * height * 2;  // RGB565
    uint8_t* buffer = (uint8_t*)heap_caps_malloc(buffer_size, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!buffer) {
        // 如果内存不足，回退到分块清除
        ESP_LOGW(TAG, "Failed to allocate full buffer (%zu bytes), falling back to chunked clear", buffer_size);
        const int LINES_PER_CHUNK = 16;
        size_t chunk_size = width * LINES_PER_CHUNK * 2;
        buffer = (uint8_t*)heap_caps_malloc(chunk_size, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
        if (!buffer) {
            ESP_LOGE(TAG, "Failed to allocate chunk buffer for clear");
            return false;
        }
        memset(buffer, 0, chunk_size);
        
        for (int y = y_start; y < y_end; y += LINES_PER_CHUNK) {
            int chunk_height = std::min(LINES_PER_CHUNK, y_end - y);
            // 使用同步版本，确保 DMA 完成后再释放缓冲区
            DrawBitmapToLcdSync(x_start, y, x_end, y + chunk_height, buffer);
        }
        heap_caps_free(buffer);
        return true;
    }
    
    memset(buffer, 0, buffer_size);
    
    // 使用同步版本，确保 DMA 完成后再释放缓冲区
    bool result = DrawBitmapToLcdSync(x_start, y_start, x_end, y_end, buffer);
    
    heap_caps_free(buffer);
    return result;
}

// ===== 静态回调函数 =====

void AafDisplayWidget::OnAnimationFlush(int x_start, int y_start, int x_end, int y_end,
                                         const void* data, void* user_data) {
    auto* self = static_cast<AafDisplayWidget*>(user_data);
    
    // 调试日志：每1000次输出一次（debug级别，避免刷屏）
    static int flush_count = 0;
    flush_count++;
    if (flush_count % 1000 == 1) {
        ESP_LOGD(TAG, "OnAnimationFlush #%d: (%d,%d)-(%d,%d), self=%p, data=%p",
                 flush_count, x_start, y_start, x_end, y_end, (void*)self, data);
    }
    
    if (!self) {
        ESP_LOGE(TAG, "OnAnimationFlush: self is NULL!");
        return;
    }
    
    // 绘制到 LCD（**同步模式**，等待 DMA 完成）
    // 坐标已在 anim_player 回调中预处理
    // DrawBitmapToLcd 内部会等待 DMA 传输完成后才返回
    self->DrawBitmapToLcd(x_start, y_start, x_end, y_end, data);
    
    // **同步模式**: anim_player_flush_ready() 在 aaf_animation_player.cc 的 flush_cb 中调用
    // DrawBitmapToLcd 返回时，DMA 已完成，可以安全地通知动画播放器继续
}

void AafDisplayWidget::OnAnimationStop(int x_start, int y_start, int x_end, int y_end,
                                        void* user_data) {
    auto* self = static_cast<AafDisplayWidget*>(user_data);
    if (!self) {
        return;
    }
    
    ESP_LOGI(TAG, "Animation stopped, clearing area: (%d,%d)-(%d,%d)",
             x_start, y_start, x_end, y_end);
    
    // 清空动画区域（显示黑色）
    self->ClearLcdArea(x_start, y_start, x_end, y_end);
}

void AafDisplayWidget::OnStateChanged(const AnimationStateManager::StateChangeEvent& event) {
    ESP_LOGI(TAG, "State changed: %s (type=%d, priority=%d, file=%s, index=%d)",
             event.state_name.c_str(),
             static_cast<int>(event.type),
             static_cast<int>(event.priority),
             event.animation_file.c_str(),
             event.animation_index);
    
    // 检查资源管理器是否初始化
    if (!resource_manager_ || resource_manager_->GetAnimationCount() == 0) {
        ESP_LOGW(TAG, "Resource manager not ready, skipping animation");
        return;
    }
    
    // 停止之前的超时定时器
    StopTimeoutTimer();
    
    // 获取动画数据
    size_t data_size = 0;
    int fps = event.fps;
    const void* data = nullptr;
    
    // 优先使用文件名查找
    if (!event.animation_file.empty()) {
        data = resource_manager_->GetAnimationData(event.animation_file.c_str(), &data_size, &fps);
        
        // 如果失败，尝试去掉扩展名
        if (!data) {
            std::string name_without_ext = event.animation_file;
            size_t dot_pos = name_without_ext.rfind('.');
            if (dot_pos != std::string::npos) {
                name_without_ext = name_without_ext.substr(0, dot_pos);
                data = resource_manager_->GetAnimationData(name_without_ext.c_str(), &data_size, &fps);
            }
        }
    }
    
    // 如果文件名查找失败，尝试用索引
    if (!data && event.animation_index >= 0 && 
        event.animation_index < resource_manager_->GetAnimationCount()) {
        data = resource_manager_->GetAnimationData(event.animation_index, &data_size, &fps);
    }
    
    if (!data || data_size == 0) {
        ESP_LOGE(TAG, "Failed to get animation data for: %s (file=%s, index=%d)",
                 event.state_name.c_str(), 
                 event.animation_file.c_str(),
                 event.animation_index);
        // 打印可用动画列表
        ESP_LOGI(TAG, "Available animations:");
        for (int i = 0; i < resource_manager_->GetAnimationCount() && i < 10; i++) {
            const char* name = resource_manager_->GetAnimationName(i);
            ESP_LOGI(TAG, "  [%d]: %s", i, name ? name : "(null)");
        }
        return;
    }
    
    // 验证数据指针有效性
    uintptr_t addr_val = reinterpret_cast<uintptr_t>(data);
    if (addr_val < 0x3C000000 || data_size < 100 || data_size > 10000000) {
        ESP_LOGE(TAG, "Invalid animation data: addr=0x%08lx, size=%lu",
                 (unsigned long)addr_val, (unsigned long)data_size);
        return;
    }
    
    ESP_LOGI(TAG, "Animation data: addr=0x%08lx, size=%lu, fps=%d", 
             (unsigned long)data, (unsigned long)data_size, fps);
    
    // 检查动画播放器是否有效
    if (!animation_player_) {
        ESP_LOGE(TAG, "Animation player not initialized");
        return;
    }
    
    // 配置并播放动画
    AnimDataConfig anim_config = {
        .data_address = data,
        .data_length = data_size,
        .fps = fps > 0 && fps < 120 ? fps : 15,
    };
    
    if (!animation_player_->Play(anim_config, event.loop, true)) {
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
    // 更新蓝牙图标显示
    if (ble_icon_label_) {
        Lock();
        auto& ble_provisioner = BLEWiFiProvisioner::GetInstance();
        if (ble_provisioner.IsProvisioning()) {
            lv_label_set_text(ble_icon_label_, FONT_AWESOME_BLUETOOTH);
        } else {
            lv_label_set_text(ble_icon_label_, "");
        }
        Unlock();
    }
}

void AafDisplayWidget::StartTimeoutTimer(uint32_t timeout_ms) {
    if (!timeout_timer_) {
        return;
    }
    
    ESP_LOGI(TAG, "Starting timeout timer: %lu ms", timeout_ms);
    
    esp_timer_stop(timeout_timer_);
    esp_timer_start_once(timeout_timer_, timeout_ms * 1000);
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
    
    ESP_LOGI(TAG, "Emotion timeout, restoring to current device state");
    // 使用当前设备状态恢复，而不是历史栈中保存的旧状态
    self->state_manager_->RestoreToCurrentDeviceState();
}

// ===== LCD 传输完成回调（用于 DMA 同步）=====

bool AafDisplayWidget::OnLcdTransferDone(esp_lcd_panel_io_handle_t panel_io,
                                          esp_lcd_panel_io_event_data_t* edata,
                                          void* user_ctx) {
    // ISR 诊断计数器（每 1000 次打印一次）
    static int isr_count = 0;
    isr_count++;
    
    auto* self = static_cast<AafDisplayWidget*>(user_ctx);
    if (!self) {
        return false;
    }

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // 释放 DMA 同步信号量
    if (self->trans_done_sem_) {
        xSemaphoreGiveFromISR(self->trans_done_sem_, &xHigherPriorityTaskWoken);
    }

    // 同步模式：anim_player_flush_ready 在 flush_cb 中调用
    // 不在 ISR 中调用 NotifyFlushFinished()

    return xHigherPriorityTaskWoken == pdTRUE;
}

bool AafDisplayWidget::WaitForTransferDone(int timeout_ms) {
    if (!trans_done_sem_) {
        return false;
    }
    return xSemaphoreTake(trans_done_sem_, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

} // namespace display
} // namespace xiaozhi
