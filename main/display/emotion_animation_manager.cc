#include "emotion_animation_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <algorithm>

static const char* TAG = "EmotionAnimMgr";

namespace lottie {

// 自动返回定时器回调
static void auto_return_timer_callback(void* arg) {
    EmotionAnimationManager* mgr = static_cast<EmotionAnimationManager*>(arg);
    if (mgr) {
        ESP_LOGI(TAG, "Auto returning to neutral emotion");
        mgr->ShowEmotion(EmotionType::NEUTRAL);
    }
}

EmotionAnimationManager& EmotionAnimationManager::Instance() {
    static EmotionAnimationManager instance;
    return instance;
}

bool EmotionAnimationManager::Init(AnimationManager* anim_mgr) {
    if (initialized_) {
        ESP_LOGW(TAG, "Already initialized");
        return true;
    }

    // 使用传入的动画管理器或默认单例
    anim_mgr_ = anim_mgr ? anim_mgr : &AnimationManager::Instance();
    
    if (!anim_mgr_) {
        ESP_LOGE(TAG, "AnimationManager is null");
        return false;
    }

    // 初始化状态
    current_emotion_ = EmotionType::NEUTRAL;
    default_emotion_ = EmotionType::NEUTRAL;
    sequence_index_ = 0;
    sequence_loop_ = false;
    playing_sequence_ = false;
    auto_return_neutral_ = false;
    auto_return_delay_ms_ = 5000;
    auto_return_timer_ = nullptr;
    sequence_timer_ = nullptr;           // 初始化序列定时器
    pending_delete_timer_ = nullptr;     // 初始化待删除定时器
    in_timer_callback_ = false;          // 初始化回调标志
    initialized_ = true;

    ESP_LOGI(TAG, "EmotionAnimationManager initialized");
    return true;
}

void EmotionAnimationManager::RegisterEmotion(EmotionType emotion, 
                                              const EmotionAnimConfig& config) {
    if (!initialized_) {
        ESP_LOGW(TAG, "Not initialized");
        return;
    }

    emotion_configs_[emotion] = config;
    ESP_LOGI(TAG, "Registered emotion: %s -> %s", 
             EmotionTypeToString(emotion), config.file_path.c_str());
}

void EmotionAnimationManager::RegisterEmotion(EmotionType emotion, 
                                              const std::string& file_path,
                                              bool loop, bool is_local) {
    EmotionAnimConfig config(file_path, loop, -1, 1.0f, is_local);
    RegisterEmotion(emotion, config);
}

int EmotionAnimationManager::RegisterEmotionsFromDirectory(
    const std::string& dir_path, const std::string& name_pattern) {
    
    if (!initialized_) {
        ESP_LOGW(TAG, "Not initialized");
        return 0;
    }

    int count = 0;
    
    // 遍历所有情感类型
    const EmotionType emotions[] = {
        EmotionType::HAPPY, EmotionType::SAD, EmotionType::SURPRISED,
        EmotionType::ANGRY, EmotionType::CONFUSED, EmotionType::SLEEPY,
        EmotionType::NEUTRAL, EmotionType::EXCITED, EmotionType::CALM,
        EmotionType::THINKING, EmotionType::LOVE
    };

    for (auto emotion : emotions) {
        std::string emotion_name = EmotionTypeToString(emotion);
        
        // 构建文件路径
        std::string file_path = dir_path;
        if (!file_path.empty() && file_path.back() != '/') {
            file_path += '/';
        }
        
        // 替换模式中的 {emotion}
        std::string filename = name_pattern;
        size_t pos = filename.find("{emotion}");
        if (pos != std::string::npos) {
            filename.replace(pos, 9, emotion_name);
        }
        
        file_path += filename;

        // 注册情感（这里假设文件存在，实际使用时可能需要检查）
        RegisterEmotion(emotion, file_path, false, true);
        count++;
    }

    ESP_LOGI(TAG, "Registered %d emotions from directory: %s", count, dir_path.c_str());
    return count;
}

bool EmotionAnimationManager::ShowEmotion(EmotionType emotion, int duration_ms) {
    if (!initialized_) {
        ESP_LOGW(TAG, "Not initialized");
        return false;
    }

    // 停止序列播放
    if (playing_sequence_) {
        playing_sequence_ = false;
        current_sequence_.clear();
    }

    // 取消自动返回定时器
    CancelAutoReturn();

    // 检查情感是否已注册
    auto it = emotion_configs_.find(emotion);
    if (it == emotion_configs_.end()) {
        ESP_LOGW(TAG, "Emotion not registered: %s, using default", 
                 EmotionTypeToString(emotion));
        
        // 尝试使用默认情感
        it = emotion_configs_.find(default_emotion_);
        if (it == emotion_configs_.end()) {
            ESP_LOGE(TAG, "Default emotion not registered");
            return false;
        }
        emotion = default_emotion_;
    }

    const EmotionAnimConfig& config = it->second;

    // 加载并播放动画
    if (!LoadAnimationFile(config)) {
        ESP_LOGE(TAG, "Failed to load emotion animation: %s", 
                 EmotionTypeToString(emotion));
        return false;
    }

    // 更新当前情感
    current_emotion_ = emotion;

    // 调用情感切换回调
    if (emotion_change_callback_) {
        emotion_change_callback_(emotion);
    }

    // 如果设置了持续时间且启用了自动返回
    if (duration_ms > 0 || (duration_ms == 0 && auto_return_neutral_)) {
        ScheduleAutoReturn();
    }

    ESP_LOGI(TAG, "Showing emotion: %s (duration: %d ms)", 
             EmotionTypeToString(emotion), duration_ms);
    
    return true;
}

bool EmotionAnimationManager::PlayEmotionSequence(
    const std::vector<EmotionSequenceItem>& sequence, bool loop) {
    
    if (!initialized_) {
        ESP_LOGW(TAG, "Not initialized");
        return false;
    }

    if (sequence.empty()) {
        ESP_LOGW(TAG, "Empty emotion sequence");
        return false;
    }

    // 设置序列播放状态
    current_sequence_ = sequence;
    sequence_index_ = 0;
    sequence_loop_ = loop;
    playing_sequence_ = true;

    ESP_LOGI(TAG, "Starting emotion sequence playback (%zu items, loop: %d)", 
             sequence.size(), loop);

    // 播放第一个情感
    PlayNextInSequence();
    
    return true;
}

void EmotionAnimationManager::Stop() {
    if (!initialized_) return;

    playing_sequence_ = false;
    current_sequence_.clear();
    CancelAutoReturn();
    
    // 取消序列定时器（只在非回调期间删除）
    if (!in_timer_callback_ && sequence_timer_) {
        esp_timer_handle_t timer = static_cast<esp_timer_handle_t>(sequence_timer_);
        esp_timer_stop(timer);
        esp_timer_delete(timer);
        sequence_timer_ = nullptr;
        ESP_LOGD(TAG, "Sequence timer cancelled");
    }
    
    // 清理待删除的定时器（只在非回调期间删除）
    if (!in_timer_callback_ && pending_delete_timer_) {
        esp_timer_delete(static_cast<esp_timer_handle_t>(pending_delete_timer_));
        pending_delete_timer_ = nullptr;
    }
    
    if (anim_mgr_) {
        anim_mgr_->Stop();
    }

    ESP_LOGI(TAG, "Emotion animation stopped");
}

void EmotionAnimationManager::Pause() {
    if (!initialized_ || !anim_mgr_) return;
    
    anim_mgr_->Pause();
    ESP_LOGI(TAG, "Emotion animation paused");
}

void EmotionAnimationManager::Resume() {
    if (!initialized_ || !anim_mgr_) return;
    
    anim_mgr_->Resume();
    ESP_LOGI(TAG, "Emotion animation resumed");
}

void EmotionAnimationManager::SetTransitionAnimation(const std::string& transition_path) {
    transition_anim_path_ = transition_path;
    ESP_LOGI(TAG, "Transition animation set: %s", transition_path.c_str());
}

void EmotionAnimationManager::SetEmotionChangeCallback(
    std::function<void(EmotionType)> callback) {
    emotion_change_callback_ = callback;
}

void EmotionAnimationManager::SetSequenceCompleteCallback(
    std::function<void()> callback) {
    sequence_complete_callback_ = callback;
}

bool EmotionAnimationManager::IsEmotionRegistered(EmotionType emotion) const {
    return emotion_configs_.find(emotion) != emotion_configs_.end();
}

std::vector<EmotionType> EmotionAnimationManager::GetRegisteredEmotions() const {
    std::vector<EmotionType> emotions;
    for (const auto& pair : emotion_configs_) {
        emotions.push_back(pair.first);
    }
    return emotions;
}

void EmotionAnimationManager::SetDefaultEmotion(EmotionType emotion) {
    default_emotion_ = emotion;
    ESP_LOGI(TAG, "Default emotion set: %s", EmotionTypeToString(emotion));
}

void EmotionAnimationManager::SetAutoReturnNeutral(bool enable, int delay_ms) {
    auto_return_neutral_ = enable;
    auto_return_delay_ms_ = delay_ms;
    
    ESP_LOGI(TAG, "Auto return neutral: %s (delay: %d ms)", 
             enable ? "enabled" : "disabled", delay_ms);
}

void EmotionAnimationManager::TestEmotion(EmotionType emotion, int repeat_count) {
    if (!initialized_) {
        ESP_LOGW(TAG, "Not initialized");
        return;
    }

    ESP_LOGI(TAG, "Testing emotion: %s (repeat: %d)", 
             EmotionTypeToString(emotion), repeat_count);

    if (repeat_count == 0) {
        // 无限循环
        auto it = emotion_configs_.find(emotion);
        if (it != emotion_configs_.end()) {
            EmotionAnimConfig config = it->second;
            config.loop = true;
            
            if (LoadAnimationFile(config)) {
                current_emotion_ = emotion;
            }
        }
    } else {
        // 创建重复序列
        std::vector<EmotionSequenceItem> sequence;
        for (int i = 0; i < repeat_count; i++) {
            sequence.emplace_back(emotion, 0, false);
        }
        PlayEmotionSequence(sequence, false);
    }
}

// ============================================================================
// 私有方法
// ============================================================================

void EmotionAnimationManager::PlayNextInSequence() {
    if (!playing_sequence_ || current_sequence_.empty()) {
        return;
    }
    
    // ⚠️ CRITICAL: 只在非回调期间删除待删除的定时器
    // 在回调期间删除定时器会导致 ESP-IDF 定时器链表损坏 (StoreProhibited)
    if (!in_timer_callback_ && pending_delete_timer_) {
        esp_timer_delete(static_cast<esp_timer_handle_t>(pending_delete_timer_));
        pending_delete_timer_ = nullptr;
    }

    // 检查是否完成
    if (sequence_index_ >= current_sequence_.size()) {
        if (sequence_loop_) {
            // 重新开始
            sequence_index_ = 0;
            ESP_LOGI(TAG, "Looping emotion sequence");
        } else {
            // 序列完成
            playing_sequence_ = false;
            ESP_LOGI(TAG, "Emotion sequence completed");
            
            if (sequence_complete_callback_) {
                sequence_complete_callback_();
            }
            return;
        }
    }

    // 获取当前序列项
    const EmotionSequenceItem& item = current_sequence_[sequence_index_];

    // 播放过渡动画（如果需要）
    if (item.use_transition && !transition_anim_path_.empty() && sequence_index_ > 0) {
        ESP_LOGI(TAG, "Playing transition animation");
        anim_mgr_->PlayCustomAnimation(transition_anim_path_, false, false);
        // 注意：这里可能需要添加延迟或回调来等待过渡动画完成
    }

    // 显示情感
    ShowEmotion(item.emotion, item.duration_ms);

    // 设置完成回调
    if (item.duration_ms > 0) {
        // 如果存在旧的定时器（非回调调用的情况），先删除它
        if (sequence_timer_) {
            esp_timer_handle_t old_timer = static_cast<esp_timer_handle_t>(sequence_timer_);
            esp_timer_stop(old_timer);
            esp_timer_delete(old_timer);
            sequence_timer_ = nullptr;
        }
        
        // 创建新定时器
        esp_timer_handle_t timer;
        esp_timer_create_args_t timer_args = {
            .callback = [](void* arg) {
                EmotionAnimationManager* mgr = static_cast<EmotionAnimationManager*>(arg);
                
                // ⚠️ CRITICAL: 标记进入回调期间
                mgr->in_timer_callback_ = true;
                
                // 将当前定时器移动到待删除队列
                // 注意：不能在回调期间删除！会在回调结束后、下次调用时删除
                mgr->pending_delete_timer_ = mgr->sequence_timer_;
                mgr->sequence_timer_ = nullptr;
                
                mgr->sequence_index_++;
                mgr->PlayNextInSequence();
                
                // 标记退出回调期间
                mgr->in_timer_callback_ = false;
            },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "emotion_seq_timer",
            .skip_unhandled_events = true,  // ⚠️ 防止快速状态切换时回调堆积
        };
        
        if (esp_timer_create(&timer_args, &timer) == ESP_OK) {
            sequence_timer_ = timer;  // 保存定时器句柄
            esp_timer_start_once(timer, item.duration_ms * 1000);
        }
    } else {
        // 等待动画完成（通过 AnimationManager 的回调）
        anim_mgr_->SetCompleteCallback([this](AnimState state) {
            this->OnAnimationComplete();
        });
    }
}

void EmotionAnimationManager::OnAnimationComplete() {
    if (playing_sequence_) {
        sequence_index_++;
        PlayNextInSequence();
    }
}

void EmotionAnimationManager::ScheduleAutoReturn() {
    if (!auto_return_neutral_) {
        return;
    }

    // 取消之前的定时器
    CancelAutoReturn();

    // 创建新的定时器
    esp_timer_create_args_t timer_args = {
        .callback = auto_return_timer_callback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "auto_return_timer",
        .skip_unhandled_events = true,  // ⚠️ 防止快速状态切换时回调堆积
    };

    esp_timer_handle_t timer;
    if (esp_timer_create(&timer_args, &timer) == ESP_OK) {
        auto_return_timer_ = timer;
        esp_timer_start_once(timer, auto_return_delay_ms_ * 1000);
        ESP_LOGD(TAG, "Auto return timer scheduled (%d ms)", auto_return_delay_ms_);
    }
}

void EmotionAnimationManager::CancelAutoReturn() {
    if (auto_return_timer_) {
        esp_timer_handle_t timer = static_cast<esp_timer_handle_t>(auto_return_timer_);
        esp_timer_stop(timer);
        esp_timer_delete(timer);
        auto_return_timer_ = nullptr;
        ESP_LOGD(TAG, "Auto return timer cancelled");
    }
}

bool EmotionAnimationManager::LoadAnimationFile(const EmotionAnimConfig& config) {
    if (!anim_mgr_) {
        return false;
    }

    // 检查是否需要下载
    if (!config.is_local) {
        ESP_LOGW(TAG, "Remote animation download not implemented yet: %s", 
                 config.file_path.c_str());
        // TODO: 实现远程动画下载功能
        return false;
    }

    // 使用 AnimationManager 播放自定义动画
    anim_mgr_->PlayCustomAnimation(config.file_path, config.loop, false);

    // 设置播放速度
    if (config.speed != 1.0f && anim_mgr_->GetCurrentAnimation()) {
        anim_mgr_->GetCurrentAnimation()->SetSpeed(config.speed);
    }

    return true;
}

} // namespace lottie

