#include "motion_engine.h"

#ifdef CONFIG_ENABLE_DOLL_INTERACTION

#include "motor/motor_controller.h"
#include <esp_log.h>
#include <esp_random.h>
#include <cstring>

#define TAG "MotionEngine"

MotionEngine& MotionEngine::GetInstance() {
    static MotionEngine instance;
    return instance;
}

MotionEngine::MotionEngine()
    : running_(false),
      is_playing_(false),
      is_paused_(false),
      should_stop_(false),
      current_step_index_(0),
      motor_controller_(nullptr),
      playback_task_handle_(nullptr),
      idle_alive_timer_(nullptr) {
}

MotionEngine::~MotionEngine() {
    Stop();
}

void MotionEngine::Initialize() {
    if (running_) {
        ESP_LOGW(TAG, "Already initialized");
        return;
    }

    ESP_LOGI(TAG, "Initializing MotionEngine...");

    motor_controller_ = &MotorController::GetInstance();
    
    // 加载 P0 预设动作
    LoadP0Presets();
    
    // 加载情绪映射
    LoadEmotionMappings();

    ESP_LOGI(TAG, "MotionEngine initialized with %zu motions", motion_presets_.size());
}

void MotionEngine::Start() {
    if (running_) {
        ESP_LOGW(TAG, "Already running");
        return;
    }

    ESP_LOGI(TAG, "Starting MotionEngine...");
    running_ = true;
    should_stop_ = false;

    ESP_LOGI(TAG, "MotionEngine started");
}

void MotionEngine::Stop() {
    if (!running_) {
        return;
    }

    ESP_LOGI(TAG, "Stopping MotionEngine...");

    StopMotion();
    StopIdleAliveTimer();
    
    running_ = false;

    ESP_LOGI(TAG, "MotionEngine stopped");
}

// ==================== P0 动作预设加载 ====================

void MotionEngine::LoadP0Presets() {
    ESP_LOGI(TAG, "Loading P0 motion presets...");

    // ===== 1. home - 归位 =====
    {
        MotionSequence seq("home");
        seq.SetPriority(0)
           .SetInterruptible(false)
           .Home();
        motion_presets_["home"] = seq;
    }

    // ===== 2. nod - 点头（表示肯定/理解） =====
    // Pitch: ±8°, 中速, 1-2 次
    {
        MotionSequence seq("nod");
        seq.SetPriority(0)
           .SetInterruptible(true);
        
        // 低头
        seq.PitchRel(-8.0f, 200);
        seq.Delay(100);
        // 抬头
        seq.PitchRel(8.0f, 200);
        seq.Delay(100);
        // 回中
        seq.PitchRel(0.0f, 150);
        
        motion_presets_["nod"] = seq;
    }

    // ===== 3. shake - 摇头（表示否定） =====
    // Yaw: ±15°, 2-3 次
    {
        MotionSequence seq("shake");
        seq.SetPriority(0)
           .SetInterruptible(true);
        
        for (int i = 0; i < 2; i++) {
            // 左转
            seq.YawRel(-15.0f, 150);
            seq.Delay(50);
            // 右转
            seq.YawRel(15.0f, 150);
            seq.Delay(50);
        }
        // 回中
        seq.Home();
        
        motion_presets_["shake"] = seq;
    }

    // ===== 4. greeting - 打招呼（小幅点头示意） =====
    // Pitch: -5°, 轻快
    {
        MotionSequence seq("greeting");
        seq.SetPriority(0)
           .SetInterruptible(true);
        
        // 小幅低头
        seq.PitchRel(-5.0f, 150);
        seq.Delay(200);
        // 回正
        seq.PitchRel(5.0f, 150);
        
        motion_presets_["greeting"] = seq;
    }

    // ===== 5. listening - 倾听（微微侧头） =====
    // Yaw: 偏转 10-15°, 保持
    {
        MotionSequence seq("listening");
        seq.SetPriority(0)
           .SetInterruptible(true);
        
        // 侧头
        seq.YawRel(12.0f, 400);
        // 微微低头
        seq.PitchRel(-3.0f, 200);
        // 保持姿势（由上层逻辑控制何时结束）
        seq.Delay(2000);
        // 回正
        seq.Home();
        
        motion_presets_["listening"] = seq;
    }

    // ===== 6. speaking - 说话（轻微点头，循环） =====
    // Pitch: ±3°, 慢速, 循环
    {
        MotionSequence seq("speaking");
        seq.SetPriority(0)
           .SetInterruptible(true)
           .SetLoop(true);
        
        // 轻微低头
        seq.PitchRel(-3.0f, 300);
        seq.Delay(200);
        // 轻微抬头
        seq.PitchRel(3.0f, 300);
        seq.Delay(200);
        
        motion_presets_["speaking"] = seq;
    }

    // ===== 7. thinking - 思考（左右缓慢往返） =====
    // Yaw: ±5°, 慢速 (4-6s 周期)
    {
        MotionSequence seq("thinking");
        seq.SetPriority(0)
           .SetInterruptible(true)
           .SetLoop(true);
        
        // 缓慢左转
        seq.YawRel(-5.0f, 2000);
        seq.Delay(500);
        // 缓慢右转
        seq.YawRel(5.0f, 2000);
        seq.Delay(500);
        
        motion_presets_["thinking"] = seq;
    }

    // ===== 8. wake_up - 唤醒响应（小幅转向+点头） =====
    // 转向声源方向（默认正前方），小幅点头
    {
        MotionSequence seq("wake_up");
        seq.SetPriority(0)
           .SetInterruptible(false);
        
        // 先回中
        seq.Home();
        seq.Delay(100);
        // 小幅转头（模拟转向声源）
        seq.YawRel(8.0f, 200);
        seq.Delay(100);
        // 点头示意
        seq.PitchRel(-5.0f, 150);
        seq.Delay(100);
        seq.PitchRel(5.0f, 150);
        // 回正
        seq.Home();
        
        motion_presets_["wake_up"] = seq;
    }

    // ===== 9. idle_alive - 待机微动 =====
    // 默认不动，偶尔轻转 10-15°
    {
        MotionSequence seq("idle_alive");
        seq.SetPriority(0)
           .SetInterruptible(true);
        
        // 轻微转动
        seq.YawRel(10.0f, 800);
        seq.Delay(1000);
        // 回正
        seq.YawRel(-10.0f, 800);
        
        motion_presets_["idle_alive"] = seq;
    }

    ESP_LOGI(TAG, "Loaded %d P0 motions", 9);
}

void MotionEngine::LoadEmotionMappings() {
    // 情绪到动作的映射
    emotion_motion_map_["happy"] = "nod";
    emotion_motion_map_["sad"] = "nod";           // 悲伤用慢速点头
    emotion_motion_map_["angry"] = "shake";
    emotion_motion_map_["surprised"] = "wake_up";
    emotion_motion_map_["thinking"] = "thinking";
    emotion_motion_map_["sleepy"] = "idle_alive";
    emotion_motion_map_["neutral"] = "home";
    emotion_motion_map_["confused"] = "shake";
    emotion_motion_map_["love"] = "nod";

    ESP_LOGI(TAG, "Loaded %zu emotion mappings", emotion_motion_map_.size());
}

// ==================== 动作播放 ====================

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

    PlayMotionSequence(it->second);
}

void MotionEngine::PlayP0Motion(P0ActionId id) {
    const char* name = P0ActionIdToName(id);
    PlayMotion(name);
}

void MotionEngine::PlayMotionSequence(const MotionSequence& sequence) {
    if (!running_) {
        ESP_LOGW(TAG, "MotionEngine not running");
        return;
    }

    // 如果正在播放且当前动作不可中断，则忽略
    if (is_playing_ && !current_sequence_.interruptible) {
        ESP_LOGW(TAG, "Current motion not interruptible");
        return;
    }

    // 停止当前动作
    if (is_playing_) {
        should_stop_ = true;
        // 等待当前步骤完成
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    ESP_LOGI(TAG, "Playing motion: %s (steps=%zu, loop=%d)", 
             sequence.name.c_str(), sequence.steps.size(), sequence.loop);

    current_sequence_ = sequence;
    current_motion_ = sequence.name;
    current_step_index_ = 0;
    is_playing_ = true;
    is_paused_ = false;
    should_stop_ = false;

    // 回调
    if (on_motion_start_) {
        on_motion_start_(sequence.name);
    }

    // 创建播放任务
    if (playback_task_handle_ == nullptr) {
        xTaskCreate(PlaybackTask, "motion_playback", 4096, this, 5, &playback_task_handle_);
    }
}

void MotionEngine::PlayEmotionMotion(const std::string& emotion) {
    auto it = emotion_motion_map_.find(emotion);
    if (it != emotion_motion_map_.end()) {
        PlayMotion(it->second);
    } else {
        ESP_LOGW(TAG, "No motion mapping for emotion: %s", emotion.c_str());
    }
}

// ==================== 动作控制 ====================

void MotionEngine::StopMotion() {
    if (!is_playing_) {
        return;
    }

    ESP_LOGI(TAG, "Stopping motion: %s", current_motion_.c_str());
    
    should_stop_ = true;
    is_playing_ = false;
    is_paused_ = false;

    // 等待任务结束
    if (playback_task_handle_ != nullptr) {
        vTaskDelay(pdMS_TO_TICKS(100));
        // 任务会自己删除
    }

    current_motion_.clear();
}

void MotionEngine::PauseMotion() {
    if (is_playing_ && !is_paused_) {
        is_paused_ = true;
        ESP_LOGI(TAG, "Motion paused");
    }
}

void MotionEngine::ResumeMotion() {
    if (is_playing_ && is_paused_) {
        is_paused_ = false;
        ESP_LOGI(TAG, "Motion resumed");
    }
}

// ==================== 状态查询 ====================

bool MotionEngine::IsPlaying() const {
    return is_playing_;
}

std::string MotionEngine::GetCurrentMotion() const {
    return current_motion_;
}

// ==================== 动作注册 ====================

void MotionEngine::RegisterMotion(const std::string& name, const MotionSequence& sequence) {
    motion_presets_[name] = sequence;
    ESP_LOGI(TAG, "Registered motion: %s", name.c_str());
}

bool MotionEngine::HasMotion(const std::string& name) const {
    return motion_presets_.find(name) != motion_presets_.end();
}

std::vector<std::string> MotionEngine::GetMotionNames() const {
    std::vector<std::string> names;
    for (const auto& pair : motion_presets_) {
        names.push_back(pair.first);
    }
    return names;
}

// ==================== 回调 ====================

void MotionEngine::SetOnMotionCompleteCallback(std::function<void()> callback) {
    on_motion_complete_ = callback;
}

void MotionEngine::SetOnMotionStartCallback(std::function<void(const std::string&)> callback) {
    on_motion_start_ = callback;
}

// ==================== 测试接口 ====================

std::string MotionEngine::GetMotionListString() const {
    std::string result = "Registered Motions:\n";
    for (const auto& pair : motion_presets_) {
        char buf[64];
        snprintf(buf, sizeof(buf), "  - %s (P%d, steps=%zu, loop=%d)\n",
                 pair.first.c_str(),
                 pair.second.priority,
                 pair.second.steps.size(),
                 pair.second.loop);
        result += buf;
    }
    return result;
}

std::string MotionEngine::GetStatusString() const {
    char buf[256];
    snprintf(buf, sizeof(buf),
        "MotionEngine Status:\n"
        "  Running: %s\n"
        "  Playing: %s\n"
        "  Paused: %s\n"
        "  Current: %s\n"
        "  Step: %zu/%zu\n"
        "  Idle Timer: %s",
        running_ ? "yes" : "no",
        is_playing_ ? "yes" : "no",
        is_paused_ ? "yes" : "no",
        current_motion_.empty() ? "(none)" : current_motion_.c_str(),
        current_step_index_,
        current_sequence_.steps.size(),
        idle_alive_timer_ ? "running" : "stopped"
    );
    return std::string(buf);
}

void MotionEngine::TestStep(const MotionStep& step) {
    ESP_LOGI(TAG, "Testing step: type=%d, p1=%.1f, p2=%.1f", 
             (int)step.type, step.param1, step.param2);
    ExecuteMotionStep(step);
}

void MotionEngine::TestAllP0Actions(uint32_t delay_between_ms) {
    ESP_LOGI(TAG, "Testing all P0 actions...");

    for (int i = 0; i < (int)P0ActionId::kCount; i++) {
        P0ActionId id = static_cast<P0ActionId>(i);
        const char* name = P0ActionIdToName(id);
        
        ESP_LOGI(TAG, "Testing P0 action: %s", name);
        PlayP0Motion(id);
        
        // 等待动作完成
        vTaskDelay(pdMS_TO_TICKS(delay_between_ms));
        StopMotion();
    }

    ESP_LOGI(TAG, "P0 action test completed");
    
    // 最后回到中位
    PlayMotion("home");
}

// ==================== Idle Alive 定时器 ====================

void MotionEngine::StartIdleAliveTimer() {
    if (idle_alive_timer_ != nullptr) {
        ESP_LOGW(TAG, "Idle alive timer already running");
        return;
    }

    // 创建定时器，2-5 分钟随机触发
    uint32_t interval_ms = 120000 + (esp_random() % 180000);  // 2-5 分钟
    
    idle_alive_timer_ = xTimerCreate(
        "idle_alive",
        pdMS_TO_TICKS(interval_ms),
        pdTRUE,  // 自动重载
        this,
        IdleAliveTimerCallback
    );

    if (idle_alive_timer_ != nullptr) {
        xTimerStart(idle_alive_timer_, 0);
        ESP_LOGI(TAG, "Idle alive timer started (interval=%lu ms)", interval_ms);
    }
}

void MotionEngine::StopIdleAliveTimer() {
    if (idle_alive_timer_ == nullptr) {
        return;
    }

    xTimerStop(idle_alive_timer_, 0);
    xTimerDelete(idle_alive_timer_, 0);
    idle_alive_timer_ = nullptr;
    
    ESP_LOGI(TAG, "Idle alive timer stopped");
}

void MotionEngine::IdleAliveTimerCallback(TimerHandle_t timer) {
    MotionEngine* engine = static_cast<MotionEngine*>(pvTimerGetTimerID(timer));
    
    if (engine && engine->running_ && !engine->is_playing_) {
        ESP_LOGD(TAG, "Idle alive triggered");
        engine->PlayMotion("idle_alive");
        
        // 随机调整下次触发时间
        uint32_t next_interval_ms = 120000 + (esp_random() % 180000);
        xTimerChangePeriod(timer, pdMS_TO_TICKS(next_interval_ms), 0);
    }
}

// ==================== 播放任务 ====================

void MotionEngine::PlaybackTask(void* param) {
    MotionEngine* engine = static_cast<MotionEngine*>(param);
    engine->RunPlaybackLoop();
    
    engine->playback_task_handle_ = nullptr;
    vTaskDelete(nullptr);
}

void MotionEngine::RunPlaybackLoop() {
    ESP_LOGD(TAG, "Playback loop started");

    while (is_playing_ && !should_stop_) {
        // 暂停检查
        while (is_paused_ && !should_stop_) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }

        if (should_stop_) {
            break;
        }

        // 执行当前步骤
        if (current_step_index_ < current_sequence_.steps.size()) {
            const MotionStep& step = current_sequence_.steps[current_step_index_];
            ExecuteMotionStep(step);
            current_step_index_++;
        } else {
            // 序列结束
            if (current_sequence_.loop && !should_stop_) {
                // 循环播放
                current_step_index_ = 0;
                ESP_LOGD(TAG, "Motion loop restart");
            } else {
                // 播放完成
                break;
            }
        }
    }

    ESP_LOGD(TAG, "Playback loop ended");

    is_playing_ = false;
    current_motion_.clear();

    // 完成回调
    if (on_motion_complete_ && !should_stop_) {
        on_motion_complete_();
    }
}

void MotionEngine::ExecuteMotionStep(const MotionStep& step) {
    if (!motor_controller_) {
        ESP_LOGE(TAG, "Motor controller not set");
        return;
    }

    switch (step.type) {
        case MotionStepType::kRotate:
            // 绝对 Yaw 角度
            motor_controller_->SetYawAngle(step.param1);
            break;

        case MotionStepType::kNod:
            // 绝对 Pitch 角度
            motor_controller_->SetPitchAngle(step.param1);
            break;

        case MotionStepType::kDelay:
            vTaskDelay(pdMS_TO_TICKS((uint32_t)step.param1));
            break;

        case MotionStepType::kReset:
        case MotionStepType::kHome:
            motor_controller_->Home();
            vTaskDelay(pdMS_TO_TICKS(200));  // 等待回中完成
            break;

        case MotionStepType::kYawRelative:
            motor_controller_->MoveYawRelative(step.param1);
            if (step.param2 > 0) {
                vTaskDelay(pdMS_TO_TICKS((uint32_t)step.param2));
            }
            break;

        case MotionStepType::kPitchRelative:
            motor_controller_->MovePitchRelative(step.param1);
            if (step.param2 > 0) {
                vTaskDelay(pdMS_TO_TICKS((uint32_t)step.param2));
            }
            break;

        case MotionStepType::kBothRelative:
            motor_controller_->MoveBothRelative(step.param1, step.param2);
            if (step.param3 > 0) {
                vTaskDelay(pdMS_TO_TICKS((uint32_t)step.param3));
            }
            break;

        case MotionStepType::kShake:
            motor_controller_->Shake((int)step.param1, (int)step.param2);
            break;

        case MotionStepType::kNodRepeat:
            for (int i = 0; i < (int)step.param2; i++) {
                motor_controller_->Nod((int)step.param1, 50);
                vTaskDelay(pdMS_TO_TICKS(100));
                motor_controller_->Nod(-(int)step.param1, 50);
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            break;

        case MotionStepType::kLoop:
            // 循环标记，不做任何事
            break;

        default:
            ESP_LOGW(TAG, "Unknown step type: %d", (int)step.type);
            break;
    }
}

#endif // CONFIG_ENABLE_DOLL_INTERACTION
