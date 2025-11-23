#include "motion_engine.h"

#ifdef CONFIG_ENABLE_DOLL_INTERACTION

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "motor/motor_controller.h"

#define TAG "MotionEngine"

MotionEngine& MotionEngine::GetInstance() {
    static MotionEngine instance;
    return instance;
}

MotionEngine::MotionEngine()
    : running_(false),
      is_playing_(false),
      is_paused_(false),
      current_step_index_(0),
      motor_controller_(nullptr),
      playback_task_handle_(nullptr) {
}

MotionEngine::~MotionEngine() {
    Stop();
}

void MotionEngine::Initialize() {
    ESP_LOGI(TAG, "Initializing MotionEngine...");

    motor_controller_ = &MotorController::GetInstance();
    
    // 加载预设动作
    LoadPresets();

    ESP_LOGI(TAG, "MotionEngine initialized with %d presets", motion_presets_.size());
}

void MotionEngine::Start() {
    if (running_) {
        ESP_LOGW(TAG, "MotionEngine already running");
        return;
    }

    ESP_LOGI(TAG, "Starting MotionEngine...");
    running_ = true;

    // 创建播放任务
    xTaskCreate(PlaybackTask, "motion_play", 4096, this, 5, &playback_task_handle_);

    ESP_LOGI(TAG, "MotionEngine started");
}

void MotionEngine::Stop() {
    if (!running_) {
        return;
    }

    ESP_LOGI(TAG, "Stopping MotionEngine...");
    
    StopMotion();
    running_ = false;

    if (playback_task_handle_) {
        vTaskDelete(playback_task_handle_);
        playback_task_handle_ = nullptr;
    }

    ESP_LOGI(TAG, "MotionEngine stopped");
}

void MotionEngine::PlayMotion(const std::string& motion_name) {
    if (!running_) {
        ESP_LOGW(TAG, "MotionEngine not running");
        return;
    }

    auto it = motion_presets_.find(motion_name);
    if (it == motion_presets_.end()) {
        ESP_LOGW(TAG, "Motion not found: %s", motion_name.c_str());
        return;
    }

    ESP_LOGI(TAG, "Playing motion: %s", motion_name.c_str());
    PlayMotionSequence(it->second);
}

void MotionEngine::PlayMotionSequence(const MotionSequence& sequence) {
    if (is_playing_) {
        ESP_LOGW(TAG, "Already playing a motion, stopping it first");
        StopMotion();
    }

    current_motion_ = sequence.name;
    current_sequence_ = sequence;
    current_step_index_ = 0;
    is_playing_ = true;
    is_paused_ = false;

    ESP_LOGI(TAG, "Starting motion sequence: %s (%d steps)", 
             sequence.name.c_str(), sequence.steps.size());
}

void MotionEngine::PlayEmotionMotion(const std::string& emotion) {
    auto it = emotion_motion_map_.find(emotion);
    if (it != emotion_motion_map_.end()) {
        PlayMotion(it->second);
    } else {
        ESP_LOGW(TAG, "No motion mapped for emotion: %s", emotion.c_str());
    }
}

void MotionEngine::StopMotion() {
    if (!is_playing_) {
        return;
    }

    ESP_LOGI(TAG, "Stopping motion: %s", current_motion_.c_str());

    is_playing_ = false;
    is_paused_ = false;
    current_motion_.clear();
    current_step_index_ = 0;

    if (motor_controller_) {
        motor_controller_->StopAllMotors();
    }
}

void MotionEngine::PauseMotion() {
    if (is_playing_ && !is_paused_) {
        ESP_LOGI(TAG, "Pausing motion");
        is_paused_ = true;
        if (motor_controller_) {
            motor_controller_->StopAllMotors();
        }
    }
}

void MotionEngine::ResumeMotion() {
    if (is_playing_ && is_paused_) {
        ESP_LOGI(TAG, "Resuming motion");
        is_paused_ = false;
    }
}

bool MotionEngine::IsPlaying() const {
    return is_playing_;
}

std::string MotionEngine::GetCurrentMotion() const {
    return current_motion_;
}

void MotionEngine::RegisterMotion(const std::string& name, const MotionSequence& sequence) {
    motion_presets_[name] = sequence;
    ESP_LOGI(TAG, "Registered motion: %s", name.c_str());
}

bool MotionEngine::HasMotion(const std::string& name) const {
    return motion_presets_.find(name) != motion_presets_.end();
}

void MotionEngine::SetOnMotionCompleteCallback(std::function<void()> callback) {
    on_motion_complete_ = callback;
}

void MotionEngine::PlaybackTask(void* param) {
    MotionEngine* engine = static_cast<MotionEngine*>(param);
    engine->RunPlaybackLoop();
    vTaskDelete(nullptr);
}

void MotionEngine::RunPlaybackLoop() {
    ESP_LOGI(TAG, "Playback loop started");

    while (running_) {
        if (!is_playing_ || is_paused_) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // 执行当前步骤
        if (current_step_index_ < current_sequence_.steps.size()) {
            const MotionStep& step = current_sequence_.steps[current_step_index_];
            ExecuteMotionStep(step);
            current_step_index_++;

            // 等待电机完成动作
            while (motor_controller_ && motor_controller_->IsBusy()) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        } else {
            // 序列播放完成
            ESP_LOGI(TAG, "Motion sequence completed: %s", current_motion_.c_str());
            
            is_playing_ = false;
            current_motion_.clear();
            current_step_index_ = 0;

            if (on_motion_complete_) {
                on_motion_complete_();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGI(TAG, "Playback loop stopped");
}

void MotionEngine::ExecuteMotionStep(const MotionStep& step) {
    if (!motor_controller_) {
        return;
    }

    ESP_LOGD(TAG, "Executing step: type=%d, param1=%d, param2=%d", 
             step.type, step.param1, step.param2);

    switch (step.type) {
        case MotionStepType::kRotate:
            motor_controller_->Rotate(step.param1, step.param2);
            break;

        case MotionStepType::kNod:
            motor_controller_->Nod(step.param1, step.param2);
            break;

        case MotionStepType::kShake:
            motor_controller_->Shake(step.param1, step.param2);
            break;

        case MotionStepType::kDelay:
            vTaskDelay(pdMS_TO_TICKS(step.param1));
            break;

        case MotionStepType::kReset:
            motor_controller_->Reset();
            break;

        default:
            ESP_LOGW(TAG, "Unknown motion step type: %d", step.type);
            break;
    }
}

void MotionEngine::LoadPresets() {
    ESP_LOGI(TAG, "Loading motion presets...");

    // TODO: 后续从配置文件或 SPIFFS 加载
    // 当前硬编码一些基础预设

    // 1. 欢迎动作
    MotionSequence welcome;
    welcome.name = "welcome";
    welcome.steps = {
        {MotionStepType::kNod, 20, 50},      // 点头 20度
        {MotionStepType::kDelay, 300, 0},
        {MotionStepType::kNod, -20, 50},     // 抬头
        {MotionStepType::kDelay, 300, 0},
        {MotionStepType::kReset, 0, 0},
    };
    RegisterMotion("welcome", welcome);

    // 2. 再见动作
    MotionSequence goodbye;
    goodbye.name = "goodbye";
    goodbye.steps = {
        {MotionStepType::kShake, 30, 2},     // 摇晃 2次
        {MotionStepType::kReset, 0, 0},
    };
    RegisterMotion("goodbye", goodbye);

    // 3. 思考动作
    MotionSequence thinking;
    thinking.name = "thinking";
    thinking.steps = {
        {MotionStepType::kNod, 10, 30},      // 轻微点头
        {MotionStepType::kDelay, 500, 0},
        {MotionStepType::kReset, 0, 0},
    };
    RegisterMotion("thinking", thinking);

    // 4. 开心动作
    MotionSequence happy;
    happy.name = "happy";
    happy.steps = {
        {MotionStepType::kNod, 15, 80},      // 快速点头
        {MotionStepType::kNod, -15, 80},
        {MotionStepType::kNod, 15, 80},
        {MotionStepType::kReset, 0, 0},
    };
    RegisterMotion("happy", happy);

    // 5. 生气动作
    MotionSequence angry;
    angry.name = "angry";
    angry.steps = {
        {MotionStepType::kShake, 40, 3},     // 激烈摇晃
        {MotionStepType::kReset, 0, 0},
    };
    RegisterMotion("angry", angry);

    // 情绪映射
    emotion_motion_map_["happy"] = "happy";
    emotion_motion_map_["joy"] = "happy";
    emotion_motion_map_["angry"] = "angry";
    emotion_motion_map_["sad"] = "thinking";
    emotion_motion_map_["neutral"] = "thinking";

    ESP_LOGI(TAG, "Loaded %d motion presets", motion_presets_.size());
}

#endif // CONFIG_ENABLE_DOLL_INTERACTION

