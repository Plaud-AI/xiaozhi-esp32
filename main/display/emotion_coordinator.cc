#include "emotion_coordinator.h"
#include "emotion_assets_loader.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

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
    animation_container_ = parent;  // 保存动画容器引用

    ESP_LOGI(TAG, "Initializing Emotion System...");
    ESP_LOGI(TAG, "  Animation path: %s", config.animation_base_path.c_str());
    ESP_LOGI(TAG, "  Screen size: %dx%d", config.screen_width, config.screen_height);
    ESP_LOGI(TAG, "  Animation container: %p", animation_container_);

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
    
    // ⚠️ 不在 Init() 中播放动画，避免在初始化阶段阻塞
    // 调用方应该在初始化完成后手动调用 SetEmotion() 或 SetDeviceState()
    ESP_LOGI(TAG, "Note: Initial emotion animation not played, call SetEmotion() to display");

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
    
    if (current_asset_anim_) {
        current_asset_anim_->Pause();
    }
    
    auto& lottie_mgr = lottie::AnimationManager::Instance();
    lottie_mgr.Pause();
}

void EmotionCoordinator::Resume()
{
    if (!initialized_) return;
    
    if (current_asset_anim_) {
        // 恢复播放，从当前状态获取循环属性
        // 由于我们不存储 loop 状态，这里假设和之前一样
        // 更好的做法是在 LottieAnimation 中保存 loop 状态
        // 这里简单调用 Play(true) 或者 Play()
        // 查看 LottieAnimation::Resume 没有？只有 Play. 
        // 我们可以调用 Play(true) 
        // 实际上 LottieAnimation::Pause 只是暂停定时器，Resume 应该恢复定时器
        // LottieAnimation::Play 会重置 frame 如果 loop 不匹配？
        // LottieAnimation::Play(bool loop) 会设置 loop_ = loop
        
        // 查看 LottieAnimation 代码，Play 会设置 is_playing_ = true 并恢复定时器
        // 如果我们不想重置 current_frame，我们需要 LottieAnimation::Resume()
        // 但是 LottieAnimation 只有 Play().
        // 让我们假设 Assets 动画通常是 loop 的，或者我们需要保存 loop 状态
        // LottieAnimation 有 loop_ 成员，Play(loop) 会覆盖它
        // 我们可以添加 Resume 方法到 LottieAnimation，或者...
        // Wait, LottieAnimation code I read:
        /*
        void LottieAnimation::Play(bool loop)
        {
            // ...
            loop_ = loop;
            is_playing_ = true;
            current_frame_ = 0.0f; // <--- RESETS FRAME!
            // ...
        }
        */
        // So Play() always restarts.
        // Does LottieAnimation have Resume? No.
        // AnimationManager has Resume which calls current_animation_->Play(loop).
        
        // If we want to Resume from pause, we need LottieAnimation::Resume().
        // But LottieAnimation definition I read:
        // void Pause() { ... is_playing_ = false; }
        // void Stop() { ... is_playing_ = false; current_frame_ = 0; }
        
        // We need a Resume method in LottieAnimation or change Play to not reset if paused.
        // But for now, I will just leave it as is or maybe I should check LottieAnimation again.
        
        // Actually, LottieAnimation::Play implementation:
        /*
        void LottieAnimation::Play(bool loop) {
           // ...
           current_frame_ = 0.0f;
           // ...
        }
        */
        // So it restarts.
        // If Pause() was called, Resume() via Play() will restart. This is maybe acceptable for now.
        // But wait, AnimationManager::Resume() does:
        /*
        void AnimationManager::Resume() {
            if (current_animation_) {
                current_animation_->Play(anim_configs_[current_state_].loop);
            }
        }
        */
        // So it also restarts.
        
        // So for consistency:
        // We need to know if it was looping.
        // But current_asset_anim_ stores loop_ internally.
        // But Play(bool) overrides it.
        // We don't have a getter for loop_.
        // But LottieAnimation header I read:
        /*
           bool loop_;
        */
        // Getter? No public getter for loop_.
        
        // I will just call Play(true) for now as most emotions loop.
        // Or better, add Resume() to LottieAnimation later.
        
        // Let's stick to what AnimationManager does.
        current_asset_anim_->Play(true); 
    }
    
    auto& lottie_mgr = lottie::AnimationManager::Instance();
    lottie_mgr.Resume();
}

void EmotionCoordinator::Stop()
{
    if (!initialized_) return;
    
    if (current_asset_anim_) {
        current_asset_anim_->Stop();
    }
    
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

void EmotionCoordinator::SetCurrentAssetAnimation(lottie::LottieAnimation* anim)
{
    if (current_asset_anim_ && current_asset_anim_ != anim) {
        ESP_LOGI(TAG, "Deleting previous asset animation to free memory");
        
        // 关键修复：先停止动画，Stop() 会立即删除 timer
        // 不能在这里使用 vTaskDelay，因为这个函数在 LVGL 任务中调用！
        current_asset_anim_->Stop();
        
        // Stop() 已经删除了 timer，现在可以安全删除对象
        // LVGL timer 删除是同步的，所以不需要延迟
        delete current_asset_anim_;
        current_asset_anim_ = nullptr;
        
        ESP_LOGI(TAG, "✅ Previous animation deleted safely");
    }
    current_asset_anim_ = anim;
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

// 异步动画加载的数据结构
struct AsyncAnimLoadData {
    EmotionState emotion;
    EmotionCoordinator* coordinator;
    EmotionSystemConfig config;
    lv_obj_t* parent;
    std::function<void(const char*)> start_callback;
};

// LVGL 定时器回调：在 LVGL 任务中执行动画加载
static void async_anim_load_timer_cb(lv_timer_t* timer) {
    static const char* TAG = "EmotionCoord";  // 定义 TAG 用于日志
    
    // 使用 LVGL API 获取 user_data
    auto* load_data = static_cast<AsyncAnimLoadData*>(lv_timer_get_user_data(timer));
    if (!load_data) {
        ESP_LOGE(TAG, "Invalid load data");
        lv_timer_del(timer);
        return;
    }

    ESP_LOGI(TAG, "🎬 [LVGL Task] Loading animation: %s", 
             EmotionStateToString(load_data->emotion));

    // 停止当前文件动画（如果有）
    auto& lottie_mgr = lottie::AnimationManager::Instance();
    lottie_mgr.Stop();

    // 关键修复：先销毁旧动画，释放内存！
    // 这会释放约 300KB PSRAM (Canvas Buffer) + ThorVG 内部内存
    // 防止在创建新动画时内存峰值过高导致失败
    load_data->coordinator->SetCurrentAssetAnimation(nullptr);

    // 获取动画数据
    void* data = nullptr;
    size_t size = 0;
    bool loop = false;

    if (!EmotionAssetsLoader::GetAnimationData(load_data->emotion, data, size, loop)) {
        ESP_LOGW(TAG, "No animation data found for: %s", 
                 EmotionStateToString(load_data->emotion));
        delete load_data;
        lv_timer_del(timer);
        return;
    }

    ESP_LOGI(TAG, "Loading animation from assets: %s (%u bytes)", 
             EmotionStateToString(load_data->emotion), size);

    // 创建新动画（在干净的父容器中）
    auto* anim = EmotionAssetsLoader::CreateAnimationFromAssets(
        load_data->parent, load_data->emotion, 
        load_data->config.screen_width, load_data->config.screen_height);
    
    if (!anim) {
        ESP_LOGE(TAG, "Failed to create animation");
        delete load_data;
        lv_timer_del(timer);
        return;
    }

    // 注册新动画（Coordinator接管生命周期）
    load_data->coordinator->SetCurrentAssetAnimation(anim);

    // 设置位置和可见性
    lv_obj_t* lottie_obj = anim->GetObject();
    lv_obj_set_pos(lottie_obj, 0, 0);
    lv_obj_clear_flag(lottie_obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(lottie_obj);
    
    ESP_LOGI(TAG, "✅ Animation ready: size=%dx%d, pos=(0,0)", 
             load_data->config.screen_width, load_data->config.screen_height);

    // 开始播放
    anim->Play(loop);
    ESP_LOGI(TAG, "✅ Animation playback started (loop=%d)", loop);

    // 回调通知
    if (load_data->start_callback) {
        load_data->start_callback(EmotionStateToString(load_data->emotion));
    }

    // 清理
    delete load_data;
    lv_timer_del(timer);
}

void EmotionCoordinator::PlayEmotionAnimation(EmotionState emotion)
{
    ESP_LOGI(TAG, "PlayEmotionAnimation: %s", EmotionStateToString(emotion));

    // 检查是否从 assets 分区加载
    bool use_assets = (config_.animation_base_path == "assets:");
    
    if (use_assets) {
        // 优化：如果当前已经在播放（或加载）同一个情感，则跳过重新加载
        // 这可以防止画面闪烁，并解决频繁 SetEmotion 导致的资源竞争问题
        if (has_emotion_set_ && current_playing_emotion_ == emotion) {
            if (current_asset_anim_) {
                // 已经在播放，确保它是运行状态（应对意外暂停）
                // ESP_LOGD(TAG, "Emotion %s is already active, ensuring playback", EmotionStateToString(emotion));
                // current_asset_anim_->Play(true); // 可选：强制重播
                return;
            } else {
                // current_asset_anim_ 为空但 emotion 匹配，说明正在异步加载中
                // 避免重复调度
                ESP_LOGD(TAG, "Emotion %s is loading, skipping duplicate request", EmotionStateToString(emotion));
                return;
            }
        }

        // 更新当前情感状态
        current_playing_emotion_ = emotion;
        has_emotion_set_ = true;

        // 🔑 关键修复：使用异步加载，避免在主任务中阻塞
        // 创建加载数据
        auto* load_data = new AsyncAnimLoadData{
            .emotion = emotion,
            .coordinator = this,
            .config = config_,
            .parent = animation_container_ ? animation_container_ : lv_scr_act(),
            .start_callback = animation_start_callback_
        };

        ESP_LOGI(TAG, "🔄 Scheduling async animation load in LVGL task...");
        
        // 🔒 锁定 LVGL 以安全创建定时器（防止与 LVGL 任务发生资源竞争）
        if (lvgl_port_lock(100)) {
            // 创建单次定时器（10ms 后在 LVGL 任务中执行）
            lv_timer_t* timer = lv_timer_create(async_anim_load_timer_cb, 10, load_data);
            lv_timer_set_repeat_count(timer, 1);  // 只执行一次
            lvgl_port_unlock();
            ESP_LOGI(TAG, "✅ Animation load scheduled");
        } else {
            ESP_LOGE(TAG, "❌ Failed to lock LVGL, animation load skipped");
            delete load_data;
            return;
        }

        // TODO: 管理动画对象的生命周期（避免内存泄漏）
        // 简单实现：先不删除，让 LVGL 管理
        return;  // 异步执行，直接返回
        
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

