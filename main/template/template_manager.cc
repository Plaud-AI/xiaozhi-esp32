#include "template_manager.h"

#ifdef CONFIG_ENABLE_DOLL_INTERACTION

#include <esp_log.h>
#include "template_parser.h"
#include "template_executor.h"

#define TAG "TemplateManager"

TemplateManager& TemplateManager::GetInstance() {
    static TemplateManager instance;
    return instance;
}

TemplateManager::TemplateManager()
    : running_(false) {
}

TemplateManager::~TemplateManager() {
    Stop();
}

void TemplateManager::Initialize() {
    ESP_LOGI(TAG, "Initializing TemplateManager...");

    parser_ = std::make_unique<TemplateParser>();
    executor_ = std::make_unique<TemplateExecutor>();

    // 加载默认模板
    LoadDefaultTemplates();

    ESP_LOGI(TAG, "TemplateManager initialized with %d templates", templates_.size());
}

void TemplateManager::Start() {
    if (running_) {
        ESP_LOGW(TAG, "TemplateManager already running");
        return;
    }

    ESP_LOGI(TAG, "Starting TemplateManager...");
    running_ = true;

    executor_->Start();

    ESP_LOGI(TAG, "TemplateManager started");
}

void TemplateManager::Stop() {
    if (!running_) {
        return;
    }

    ESP_LOGI(TAG, "Stopping TemplateManager...");
    
    StopCurrentTemplate();
    running_ = false;

    if (executor_) {
        executor_->Stop();
    }

    ESP_LOGI(TAG, "TemplateManager stopped");
}

bool TemplateManager::LoadTemplate(const std::string& template_json) {
    if (!parser_) {
        ESP_LOGE(TAG, "Parser not initialized");
        return false;
    }

    Template tmpl;
    if (!parser_->Parse(template_json, tmpl)) {
        ESP_LOGE(TAG, "Failed to parse template");
        return false;
    }

    if (!ValidateTemplate(tmpl)) {
        ESP_LOGE(TAG, "Template validation failed: %s", tmpl.id.c_str());
        return false;
    }

    templates_[tmpl.id] = tmpl;
    ESP_LOGI(TAG, "Loaded template: %s", tmpl.id.c_str());
    return true;
}

bool TemplateManager::LoadTemplateFromFile(const std::string& filepath) {
    ESP_LOGI(TAG, "Loading template from file: %s", filepath.c_str());

    // TODO: 实现文件读取
    // 1. 打开文件
    // 2. 读取 JSON 内容
    // 3. 调用 LoadTemplate()

    ESP_LOGW(TAG, "File loading not implemented yet");
    return false;
}

bool TemplateManager::LoadTemplatesFromServer(const std::string& doll_id) {
    ESP_LOGI(TAG, "Loading templates for doll: %s", doll_id.c_str());

    // TODO: 实现云端模板下载
    // 1. 构建请求 URL
    // 2. 发送 HTTP 请求
    // 3. 解析响应
    // 4. 批量加载模板

    ESP_LOGW(TAG, "Server loading not implemented yet");
    return false;
}

void TemplateManager::LoadDefaultTemplates() {
    ESP_LOGI(TAG, "Loading default templates...");

    // 欢迎模板
    const char* welcome_json = R"({
        "template_id": "greeting_happy",
        "version": "1.0",
        "description": "开心的欢迎",
        "doll_id": "*",
        "sequence": [
            {
                "type": "parallel",
                "actions": [
                    {"type": "motion", "name": "welcome"},
                    {"type": "led", "effect": "rainbow", "duration": 2000}
                ]
            },
            {
                "type": "tts",
                "text": "你好呀，主人！",
                "sync_led": true
            },
            {"type": "delay", "duration": 500}
        ]
    })";

    LoadTemplate(welcome_json);

    // 告别模板
    const char* goodbye_json = R"({
        "template_id": "farewell_sad",
        "version": "1.0",
        "description": "伤心的告别",
        "doll_id": "*",
        "sequence": [
            {"type": "motion", "name": "goodbye"},
            {
                "type": "tts",
                "text": "再见，主人~",
                "sync_led": true
            },
            {"type": "led", "effect": "fade_out", "duration": 1000}
        ]
    })";

    LoadTemplate(goodbye_json);

    ESP_LOGI(TAG, "Default templates loaded");
}

void TemplateManager::ExecuteTemplate(const std::string& template_id) {
    if (!running_) {
        ESP_LOGW(TAG, "TemplateManager not running");
        return;
    }

    auto it = templates_.find(template_id);
    if (it == templates_.end()) {
        ESP_LOGW(TAG, "Template not found: %s", template_id.c_str());
        if (on_template_error_) {
            on_template_error_("Template not found: " + template_id);
        }
        return;
    }

    ESP_LOGI(TAG, "Executing template: %s", template_id.c_str());
    current_template_id_ = template_id;

    if (executor_) {
        executor_->Execute(it->second, [this]() {
            // 完成回调
            ESP_LOGI(TAG, "Template completed: %s", current_template_id_.c_str());
            current_template_id_.clear();
            if (on_template_complete_) {
                on_template_complete_();
            }
        });
    }
}

void TemplateManager::StopCurrentTemplate() {
    if (current_template_id_.empty()) {
        return;
    }

    ESP_LOGI(TAG, "Stopping template: %s", current_template_id_.c_str());

    if (executor_) {
        executor_->Stop();
    }

    current_template_id_.clear();
}

bool TemplateManager::HasTemplate(const std::string& template_id) const {
    return templates_.find(template_id) != templates_.end();
}

std::vector<std::string> TemplateManager::GetTemplateList() const {
    std::vector<std::string> list;
    for (const auto& pair : templates_) {
        list.push_back(pair.first);
    }
    return list;
}

bool TemplateManager::RemoveTemplate(const std::string& template_id) {
    auto it = templates_.find(template_id);
    if (it == templates_.end()) {
        return false;
    }

    templates_.erase(it);
    ESP_LOGI(TAG, "Removed template: %s", template_id.c_str());
    return true;
}

void TemplateManager::ClearAllTemplates() {
    templates_.clear();
    ESP_LOGI(TAG, "Cleared all templates");
}

bool TemplateManager::IsExecuting() const {
    return !current_template_id_.empty();
}

std::string TemplateManager::GetCurrentTemplate() const {
    return current_template_id_;
}

void TemplateManager::SetOnTemplateCompleteCallback(std::function<void()> callback) {
    on_template_complete_ = callback;
}

void TemplateManager::SetOnTemplateErrorCallback(std::function<void(const std::string&)> callback) {
    on_template_error_ = callback;
}

bool TemplateManager::ValidateTemplate(const Template& tmpl) {
    if (tmpl.id.empty()) {
        ESP_LOGE(TAG, "Template ID is empty");
        return false;
    }

    if (tmpl.version.empty()) {
        ESP_LOGW(TAG, "Template version is empty");
    }

    if (tmpl.sequence.empty()) {
        ESP_LOGE(TAG, "Template sequence is empty");
        return false;
    }

    // TODO: 更详细的验证
    // 1. 检查每个 action 的类型是否有效
    // 2. 检查必需参数是否存在
    // 3. 检查参数值是否合法

    return true;
}

#endif // CONFIG_ENABLE_DOLL_INTERACTION

