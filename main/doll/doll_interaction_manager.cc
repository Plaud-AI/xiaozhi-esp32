#include "doll_interaction_manager.h"

#ifdef CONFIG_ENABLE_DOLL_INTERACTION

#include <esp_log.h>
#include "doll_service.h"
#include "template/template_manager.h"
#include "motion/motion_engine.h"

#define TAG "DollInteractionManager"




DollInteractionManager& DollInteractionManager::GetInstance() {
    static DollInteractionManager instance;
    return instance;
}

DollInteractionManager::DollInteractionManager()
    : running_(false) {
}

DollInteractionManager::~DollInteractionManager() {
    Stop();
}

void DollInteractionManager::Initialize() {
    ESP_LOGI(TAG, "Initializing DollInteractionManager...");

    // 加载手办配置
    LoadDollConfigs();

    // 设置 DollService 回调
    DollEventCallbacks callbacks;
    callbacks.on_doll_placed = [this](const std::string& doll_id) {
        OnDollPlaced(doll_id);
    };
    callbacks.on_doll_removed = [this]() {
        OnDollRemoved();
    };
    callbacks.on_touched = [this](TouchPosition pos) {
        OnDollTouched(pos);
    };

    DollService::GetInstance().SetEventCallbacks(callbacks);

    ESP_LOGI(TAG, "DollInteractionManager initialized with %d doll configs", 
             doll_configs_.size());
}

void DollInteractionManager::Start() {
    if (running_) {
        ESP_LOGW(TAG, "DollInteractionManager already running");
        return;
    }

    ESP_LOGI(TAG, "Starting DollInteractionManager...");
    running_ = true;

    // 启动依赖的服务
    DollService::GetInstance().Start();
    MotionEngine::GetInstance().Start();
    TemplateManager::GetInstance().Start();

    ESP_LOGI(TAG, "DollInteractionManager started");
}

void DollInteractionManager::Stop() {
    if (!running_) {
        return;
    }

    ESP_LOGI(TAG, "Stopping DollInteractionManager...");
    
    running_ = false;
    active_doll_id_.clear();

    // 停止依赖的服务
    DollService::GetInstance().Stop();
    MotionEngine::GetInstance().Stop();
    TemplateManager::GetInstance().Stop();

    ESP_LOGI(TAG, "DollInteractionManager stopped");
}

void DollInteractionManager::RegisterDoll(const DollConfig& config) {
    doll_configs_[config.id] = config;
    ESP_LOGI(TAG, "Registered doll: %s (%s)", config.id.c_str(), config.name.c_str());
}

bool DollInteractionManager::HasDoll(const std::string& doll_id) const {
    return doll_configs_.find(doll_id) != doll_configs_.end();
}

DollInteractionManager::DollConfig DollInteractionManager::GetDollConfig(const std::string& doll_id) const {
    auto it = doll_configs_.find(doll_id);
    if (it != doll_configs_.end()) {
        return it->second;
    }
    return DollConfig();
}

bool DollInteractionManager::HasActiveDoll() const {
    return !active_doll_id_.empty();
}

std::string DollInteractionManager::GetActiveDollId() const {
    return active_doll_id_;
}

std::string DollInteractionManager::GetActiveDollName() const {
    if (active_doll_id_.empty()) {
        return "";
    }
    auto config = GetDollConfig(active_doll_id_);
    return config.name;
}

void DollInteractionManager::SimulateDollPlaced(const std::string& doll_id) {
    ESP_LOGI(TAG, "Simulating doll placed: %s", doll_id.c_str());
    DollService::GetInstance().SimulateDollPlaced(doll_id);
}

void DollInteractionManager::SimulateDollRemoved() {
    ESP_LOGI(TAG, "Simulating doll removed");
    DollService::GetInstance().SimulateDollRemoved();
}

void DollInteractionManager::SimulateTouch(TouchPosition pos) {
    ESP_LOGI(TAG, "Simulating touch: %d", static_cast<int>(pos));
    DollService::GetInstance().SimulateTouch(pos);
}

void DollInteractionManager::SetOnDollChangedCallback(std::function<void(const std::string&)> callback) {
    on_doll_changed_ = callback;
}

void DollInteractionManager::OnDollPlaced(const std::string& doll_id) {
    ESP_LOGI(TAG, "Doll placed event: %s", doll_id.c_str());

    // 检查是否识别该手办
    if (!HasDoll(doll_id)) {
        ESP_LOGW(TAG, "Unknown doll: %s, using default", doll_id.c_str());
        // TODO: 可以创建一个默认配置，或者提示用户
    }

    active_doll_id_ = doll_id;

    // 通知 Application
    if (on_doll_changed_) {
        on_doll_changed_(doll_id);
    }

    // 播放欢迎流程
    PlayGreeting(doll_id);
}

void DollInteractionManager::OnDollRemoved() {
    if (active_doll_id_.empty()) {
        return;
    }

    ESP_LOGI(TAG, "Doll removed event: %s", active_doll_id_.c_str());

    // 播放告别流程
    PlayFarewell();

    active_doll_id_.clear();

    // 通知 Application
    if (on_doll_changed_) {
        on_doll_changed_("");
    }
}

void DollInteractionManager::OnDollTouched(TouchPosition pos) {
    ESP_LOGI(TAG, "Touch event: position=%d", static_cast<int>(pos));

    PlayTouchResponse(pos);
}

void DollInteractionManager::PlayGreeting(const std::string& doll_id) {
    ESP_LOGI(TAG, "Playing greeting for: %s", doll_id.c_str());

    auto config = GetDollConfig(doll_id);
    
    // 优先使用自定义模板
    if (!config.greeting_template.empty()) {
        TemplateManager::GetInstance().ExecuteTemplate(config.greeting_template);
    } else {
        // 使用默认欢迎模板
        TemplateManager::GetInstance().ExecuteTemplate("greeting_happy");
    }

    // TODO: 发送 MCP 消息给服务器，通知手办切换
    // Application::GetInstance().SendMcpMessage(...);
}

void DollInteractionManager::PlayFarewell() {
    ESP_LOGI(TAG, "Playing farewell");

    auto config = GetDollConfig(active_doll_id_);
    
    // 优先使用自定义模板
    if (!config.farewell_template.empty()) {
        TemplateManager::GetInstance().ExecuteTemplate(config.farewell_template);
    } else {
        // 使用默认告别模板
        TemplateManager::GetInstance().ExecuteTemplate("farewell_sad");
    }
}

void DollInteractionManager::PlayTouchResponse(TouchPosition pos) {
    ESP_LOGI(TAG, "Playing touch response for position: %d", static_cast<int>(pos));

    // 根据触摸位置播放不同的动作
    switch (pos) {
        case TouchPosition::kTouchTop:
            MotionEngine::GetInstance().PlayMotion("happy");
            break;
        case TouchPosition::kTouchLeft:
        case TouchPosition::kTouchRight:
            MotionEngine::GetInstance().PlayMotion("thinking");
            break;
        case TouchPosition::kTouchBottom:
            MotionEngine::GetInstance().PlayMotion("welcome");
            break;
        default:
            break;
    }
}

void DollInteractionManager::LoadDollConfigs() {
    ESP_LOGI(TAG, "Loading doll configs...");

    // TODO: 从文件或服务器加载配置
    // 当前使用默认配置

    LoadDefaultDollConfigs();
}

void DollInteractionManager::LoadDefaultDollConfigs() {
    // 注册一些默认手办配置（示例）

    DollConfig default_doll;
    default_doll.id = "default";
    default_doll.name = "默认手办";
    default_doll.greeting_template = "greeting_happy";
    default_doll.farewell_template = "farewell_sad";
    default_doll.main_color = 0x00FF00;  // 绿色
    default_doll.personality = "友好、活泼";
    RegisterDoll(default_doll);

    DollConfig xiaozhi;
    xiaozhi.id = "xiaozhi_001";
    xiaozhi.name = "小智";
    xiaozhi.greeting_template = "greeting_happy";
    xiaozhi.farewell_template = "farewell_sad";
    xiaozhi.main_color = 0x0088FF;  // 蓝色
    xiaozhi.personality = "聪明、好学、热情";
    RegisterDoll(xiaozhi);

    DollConfig pikachu;
    pikachu.id = "pikachu_001";
    pikachu.name = "皮卡丘";
    pikachu.greeting_template = "greeting_happy";
    pikachu.farewell_template = "farewell_sad";
    pikachu.main_color = 0xFFDD00;  // 黄色
    pikachu.personality = "活泼、可爱、电气十足";
    RegisterDoll(pikachu);

    ESP_LOGI(TAG, "Default doll configs loaded");
}

#endif // CONFIG_ENABLE_DOLL_INTERACTION

