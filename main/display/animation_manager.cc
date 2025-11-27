#include "animation_manager.h"
#include "esp_log.h"

//xxx

static const char* TAG = "AnimMgr";

namespace lottie {

AnimationManager& AnimationManager::Instance()
{
    static AnimationManager instance;
    return instance;
}

void AnimationManager::Init(lv_obj_t* parent, int width, int height)
{
    parent_ = parent;
    default_width_ = width;
    default_height_ = height;
    current_state_ = AnimState::IDLE;
    previous_state_ = AnimState::IDLE;
    auto_return_ = false;
    audio_level_ = 0.0f;
    initialized_ = true;

    ESP_LOGI(TAG, "AnimationManager initialized (size: %dx%d)", width, height);
}

void AnimationManager::RegisterAnimation(AnimState state, const AnimConfig& config)
{
    if (!initialized_) {
        ESP_LOGW(TAG, "AnimationManager not initialized");
        return;
    }

    anim_configs_[state] = config;
    ESP_LOGI(TAG, "Registered animation: %s -> %s", 
             AnimStateToString(state), config.file_path.c_str());
}

void AnimationManager::RegisterAnimation(AnimState state, const std::string& file_path, bool loop)
{
    AnimConfig config;
    config.file_path = file_path;
    config.loop = loop;
    config.width = default_width_;
    config.height = default_height_;
    RegisterAnimation(state, config);
}

void AnimationManager::SetState(AnimState state, bool force)
{
    if (!initialized_) {
        ESP_LOGW(TAG, "AnimationManager not initialized");
        return;
    }

    // 如果已经是该状态且不强制切换，则忽略
    if (!force && current_state_ == state) {
        ESP_LOGD(TAG, "Already in state: %s", AnimStateToString(state));
        return;
    }

    // 检查动画是否已注册
    if (anim_configs_.find(state) == anim_configs_.end()) {
        ESP_LOGW(TAG, "Animation not registered for state: %s", AnimStateToString(state));
        return;
    }

    // 保存之前的状态
    previous_state_ = current_state_;
    current_state_ = state;

    // 加载并播放动画
    LoadAndPlayAnimation(state);

    ESP_LOGI(TAG, "State changed: %s -> %s", 
             AnimStateToString(previous_state_), 
             AnimStateToString(current_state_));
}

void AnimationManager::PlayCustomAnimation(const std::string& file_path, bool loop, bool auto_return)
{
    if (!initialized_) {
        ESP_LOGW(TAG, "AnimationManager not initialized");
        return;
    }

    // 保存当前状态
    previous_state_ = current_state_;
    current_state_ = AnimState::CUSTOM;
    auto_return_ = auto_return;

    // 创建临时配置
    AnimConfig config;
    config.file_path = file_path;
    config.loop = loop;
    config.width = default_width_;
    config.height = default_height_;

    // 临时注册并播放
    anim_configs_[AnimState::CUSTOM] = config;
    LoadAndPlayAnimation(AnimState::CUSTOM);

    ESP_LOGI(TAG, "Playing custom animation: %s (auto_return: %d)", 
             file_path.c_str(), auto_return);
}

void AnimationManager::Pause()
{
    if (current_animation_) {
        current_animation_->Pause();
        ESP_LOGI(TAG, "Animation paused");
    }
}

void AnimationManager::Resume()
{
    if (current_animation_) {
        current_animation_->Play(anim_configs_[current_state_].loop);
        ESP_LOGI(TAG, "Animation resumed");
    }
}

void AnimationManager::Stop()
{
    if (current_animation_) {
        current_animation_->Stop();
        ESP_LOGI(TAG, "Animation stopped");
    }
}

void AnimationManager::SetAudioLevel(float level)
{
    audio_level_ = level;

    // 如果当前是说话状态，根据音频电平调整动画
    if (current_state_ == AnimState::SPEAKING && current_animation_) {
        // 根据音频电平调整缩放（1.0 - 1.2 倍）
        float scale = 1.0f + level * 0.2f;
        int zoom = static_cast<int>(scale * 256);
        lv_obj_set_style_transform_zoom(current_animation_->GetObject(), zoom, 0);
    }
}

void AnimationManager::SetCompleteCallback(std::function<void(AnimState)> callback)
{
    complete_callback_ = callback;
}

void AnimationManager::SetVisible(bool visible)
{
    if (current_animation_) {
        current_animation_->SetVisible(visible);
    }
}

void AnimationManager::LoadAndPlayAnimation(AnimState state)
{
    auto it = anim_configs_.find(state);
    if (it == anim_configs_.end()) {
        ESP_LOGE(TAG, "Animation config not found for state: %s", AnimStateToString(state));
        return;
    }

    const AnimConfig& config = it->second;

    // 删除旧动画
    current_animation_.reset();

    // 创建新动画
    current_animation_ = std::make_unique<LottieAnimation>(parent_);

    // 设置大小
    int width = (config.width > 0) ? config.width : default_width_;
    int height = (config.height > 0) ? config.height : default_height_;
    current_animation_->SetSize(width, height);

    // 居中显示
    current_animation_->Center();

    // 加载动画文件
    if (!current_animation_->LoadFromFile(config.file_path.c_str())) {
        ESP_LOGE(TAG, "Failed to load animation: %s", config.file_path.c_str());
        return;
    }

    // 设置播放速度
    if (config.speed != 1.0f) {
        current_animation_->SetSpeed(config.speed);
    }

    // 设置完成回调
    current_animation_->SetCompleteCallback([this]() {
        OnAnimationComplete();
    });

    // 播放动画
    current_animation_->Play(config.loop);

    ESP_LOGI(TAG, "Animation loaded and playing: %s (loop: %d, size: %dx%d)", 
             config.file_path.c_str(), config.loop, width, height);
}

void AnimationManager::OnAnimationComplete()
{
    ESP_LOGI(TAG, "Animation complete: %s", AnimStateToString(current_state_));

    // 如果是自定义动画且需要自动返回
    if (current_state_ == AnimState::CUSTOM && auto_return_) {
        ESP_LOGI(TAG, "Auto returning to previous state: %s", AnimStateToString(previous_state_));
        SetState(previous_state_, true);
    }

    // 调用用户回调
    if (complete_callback_) {
        complete_callback_(current_state_);
    }
}

} // namespace lottie

