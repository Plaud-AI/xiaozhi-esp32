#include "emotion_coordinator.h"
#include "emotion_assets_loader.h"
#include "esp_log.h"

static const char* TAG = "EmotionCoord";

namespace emotion {

EmotionCoordinator& EmotionCoordinator::Instance()
{
    static EmotionCoordinator instance;
    return instance;
}

bool EmotionCoordinator::Init(const EmotionSystemConfig& config, lv_obj_t* parent)
{
    config_ = config;

    ESP_LOGI(TAG, "Initializing Emotion System...");
    ESP_LOGI(TAG, "  Animation path: %s", config.animation_base_path.c_str());
    ESP_LOGI(TAG, "  Screen size: %dx%d", config.screen_width, config.screen_height);

    // 1. 初始化情感状态管理器
    auto& state_mgr = EmotionStateManager::Instance();
    state_mgr.Init(config.default_emotion);
    state_mgr.SetAutoRestore(config.enable_auto_restore);
    
    // 设置状态变化回调
    state_mgr.SetStateChangeCallback([this](const EmotionChangeEvent& event) {
        OnEmotionChanged(event);
    });

    // 2. 初始化动画映射器
    auto& anim_mapper = EmotionAnimationMapper::Instance();
    anim_mapper.Init(config.animation_base_path);
    
    if (config.auto_register_mappings) {
        int anim_count = anim_mapper.RegisterDefaultMappings();
        ESP_LOGI(TAG, "Auto-registered %d animation mappings", anim_count);
    }

    // 3. 初始化设备状态映射器
    auto& device_mapper = DeviceStateMapper::Instance();
    device_mapper.Init();
    
    if (config.auto_register_mappings) {
        int device_count = device_mapper.RegisterDefaultMappings();
        ESP_LOGI(TAG, "Auto-registered %d device state mappings", device_count);
    }

    // 4. 初始化 Lottie 动画管理器
    auto& lottie_mgr = lottie::AnimationManager::Instance();
    lottie_mgr.Init(parent, config.screen_width, config.screen_height);

    initialized_ = true;

    ESP_LOGI(TAG, "Emotion System initialized successfully");
    
    // 显示默认情感
    PlayEmotionAnimation(config.default_emotion);

    return true;
}

bool EmotionCoordinator::QuickInit(const char* anim_path, int width, int height, lv_obj_t* parent)
{
    EmotionSystemConfig config;
    config.animation_base_path = anim_path;
    config.screen_width = width;
    config.screen_height = height;
    
    return Init(config, parent);
}

bool EmotionCoordinator::SetDeviceState(DeviceState device_state, const MappingContext* context)
{
    if (!initialized_) {
        ESP_LOGW(TAG, "System not initialized");
        return false;
    }

    ESP_LOGI(TAG, "SetDeviceState: %s", DeviceStateToString(device_state));

    // 通过设备状态映射器应用状态
    auto& device_mapper = DeviceStateMapper::Instance();
    return device_mapper.ApplyDeviceState(device_state, context);
}

bool EmotionCoordinator::SetDeviceState(const char* state_name)
{
    // TODO: 实现字符串到 DeviceState 的转换
    ESP_LOGW(TAG, "String-based SetDeviceState not fully implemented yet");
    return false;
}

bool EmotionCoordinator::SetEmotion(EmotionState emotion, const EmotionConfig& config)
{
    if (!initialized_) {
        ESP_LOGW(TAG, "System not initialized");
        return false;
    }

    auto& state_mgr = EmotionStateManager::Instance();
    return state_mgr.SetEmotion(emotion, config);
}

bool EmotionCoordinator::SetTemporaryEmotion(EmotionState emotion, int duration_ms)
{
    if (!initialized_) {
        ESP_LOGW(TAG, "System not initialized");
        return false;
    }

    auto& state_mgr = EmotionStateManager::Instance();
    return state_mgr.SetTemporaryEmotion(emotion, duration_ms);
}

void EmotionCoordinator::ResetToDefault()
{
    if (!initialized_) return;

    auto& state_mgr = EmotionStateManager::Instance();
    state_mgr.ResetToDefault();
}

void EmotionCoordinator::Pause()
{
    if (!initialized_) return;
    
    auto& lottie_mgr = lottie::AnimationManager::Instance();
    lottie_mgr.Pause();
}

void EmotionCoordinator::Resume()
{
    if (!initialized_) return;
    
    auto& lottie_mgr = lottie::AnimationManager::Instance();
    lottie_mgr.Resume();
}

void EmotionCoordinator::Stop()
{
    if (!initialized_) return;
    
    auto& lottie_mgr = lottie::AnimationManager::Instance();
    lottie_mgr.Stop();
}

void EmotionCoordinator::PlayCustomAnimation(const std::string& animation_path, 
                                             bool loop, 
                                             bool auto_return)
{
    if (!initialized_) return;
    
    auto& lottie_mgr = lottie::AnimationManager::Instance();
    lottie_mgr.PlayCustomAnimation(animation_path, loop, auto_return);
}

EmotionState EmotionCoordinator::GetCurrentEmotion() const
{
    auto& state_mgr = EmotionStateManager::Instance();
    return state_mgr.GetCurrentEmotion();
}

EmotionState EmotionCoordinator::GetPreviousEmotion() const
{
    auto& state_mgr = EmotionStateManager::Instance();
    return state_mgr.GetPreviousEmotion();
}

void EmotionCoordinator::SetStateChangeCallback(std::function<void(const EmotionChangeEvent&)> callback)
{
    auto& state_mgr = EmotionStateManager::Instance();
    state_mgr.SetStateChangeCallback(callback);
}

void EmotionCoordinator::SetAnimationStartCallback(std::function<void(const std::string&)> callback)
{
    animation_start_callback_ = callback;
}

void EmotionCoordinator::RegisterDeviceStateMapping(DeviceState device_state, 
                                                    EmotionState emotion,
                                                    EmotionPriority priority)
{
    MappingRule rule;
    rule.device_state = device_state;
    rule.emotion_state = emotion;
    rule.priority = priority;
    rule.duration = EmotionDuration::PERSISTENT;
    
    auto& device_mapper = DeviceStateMapper::Instance();
    device_mapper.RegisterMapping(rule);
}

void EmotionCoordinator::RegisterEmotionAnimation(EmotionState emotion, 
                                                  const std::string& animation_path,
                                                  int weight)
{
    auto& anim_mapper = EmotionAnimationMapper::Instance();
    anim_mapper.RegisterAnimation(emotion, animation_path, weight);
}

EmotionStateManager::Statistics EmotionCoordinator::GetStatistics() const
{
    auto& state_mgr = EmotionStateManager::Instance();
    return state_mgr.GetStatistics();
}

void EmotionCoordinator::PrintSystemInfo() const
{
    ESP_LOGI(TAG, "=== Emotion System Info ===");
    ESP_LOGI(TAG, "Initialized: %s", initialized_ ? "YES" : "NO");
    ESP_LOGI(TAG, "Animation path: %s", config_.animation_base_path.c_str());
    ESP_LOGI(TAG, "Screen size: %dx%d", config_.screen_width, config_.screen_height);
    ESP_LOGI(TAG, "Current emotion: %s", EmotionStateToString(GetCurrentEmotion()));
    
    auto stats = GetStatistics();
    ESP_LOGI(TAG, "Total switches: %d", stats.total_switches);
    ESP_LOGI(TAG, "Most used: %s", EmotionStateToString(stats.most_used));
    ESP_LOGI(TAG, "Current duration: %lu ms", stats.current_duration_ms);
    ESP_LOGI(TAG, "===========================");
    
    // 打印映射信息
    auto& anim_mapper = EmotionAnimationMapper::Instance();
    anim_mapper.PrintMappings();
    
    auto& device_mapper = DeviceStateMapper::Instance();
    device_mapper.PrintMappings();
}

void EmotionCoordinator::OnEmotionChanged(const EmotionChangeEvent& event)
{
    ESP_LOGI(TAG, "Emotion changed: %s -> %s (priority: %d)", 
             EmotionStateToString(event.from_state),
             EmotionStateToString(event.to_state),
             (int)event.priority);

    // 播放对应的动画
    PlayEmotionAnimation(event.to_state);
}

void EmotionCoordinator::PlayEmotionAnimation(EmotionState emotion)
{
    ESP_LOGI(TAG, "PlayEmotionAnimation: %s", EmotionStateToString(emotion));

    // 检查是否从 assets 分区加载
    bool use_assets = (config_.animation_base_path == "assets:");
    
    if (use_assets) {
        // 从 assets 分区加载（memory-mapped 方式）
        void* data = nullptr;
        size_t size = 0;
        bool loop = false;

        if (!EmotionAssetsLoader::GetAnimationData(emotion, data, size, loop)) {
            ESP_LOGW(TAG, "No animation data found for: %s", EmotionStateToString(emotion));
            return;
        }

        ESP_LOGI(TAG, "Loading animation from assets: %s (%u bytes)", 
                 EmotionStateToString(emotion), size);

        // 获取当前屏幕
        lv_obj_t* screen = lv_scr_act();
        
        // 创建新的 Lottie 动画对象
        auto* anim = EmotionAssetsLoader::CreateAnimationFromAssets(screen, emotion);
        if (!anim) {
            ESP_LOGE(TAG, "Failed to create animation");
            return;
        }

        // 设置大小和位置
        anim->SetSize(config_.screen_width, config_.screen_height);
        anim->Center();
        
        // 播放
        anim->Play(loop);

        ESP_LOGI(TAG, "Animation playing from assets");

        // 通知动画开始回调
        if (animation_start_callback_) {
            animation_start_callback_(EmotionStateToString(emotion));
        }

        // TODO: 管理动画对象的生命周期（避免内存泄漏）
        // 简单实现：先不删除，让 LVGL 管理
        
    } else {
        // 从文件系统加载（原有逻辑）
        auto& anim_mapper = EmotionAnimationMapper::Instance();
        AnimationInfo anim_info;
        
        if (!anim_mapper.GetAnimation(emotion, anim_info)) {
            ESP_LOGW(TAG, "No animation found for emotion: %s", EmotionStateToString(emotion));
            return;
        }

        ESP_LOGI(TAG, "Playing animation: %s", anim_info.file_path.c_str());

        // 通知动画开始回调
        if (animation_start_callback_) {
            animation_start_callback_(anim_info.file_path);
        }

        // 配置 Lottie 动画
        lottie::AnimConfig lottie_config;
        lottie_config.file_path = anim_info.file_path;
        lottie_config.loop = anim_info.loop;
        lottie_config.width = config_.screen_width;
        lottie_config.height = config_.screen_height;

        // 转换为 AnimState（临时映射）
        lottie::AnimState anim_state = lottie::AnimState::CUSTOM;
        switch(emotion) {
            case EmotionState::LISTENING:   anim_state = lottie::AnimState::LISTENING; break;
            case EmotionState::THINKING:    anim_state = lottie::AnimState::THINKING; break;
            case EmotionState::SPEAKING:    anim_state = lottie::AnimState::SPEAKING; break;
            default: anim_state = lottie::AnimState::CUSTOM; break;
        }

        // 如果是自定义状态，使用 PlayCustomAnimation
        if (anim_state == lottie::AnimState::CUSTOM) {
            auto& lottie_mgr = lottie::AnimationManager::Instance();
            lottie_mgr.PlayCustomAnimation(anim_info.file_path, anim_info.loop, false);
        } else {
            // 注册并播放
            auto& lottie_mgr = lottie::AnimationManager::Instance();
            lottie_mgr.RegisterAnimation(anim_state, lottie_config);
            lottie_mgr.SetState(anim_state);
        }
    }
}

} // namespace emotion

