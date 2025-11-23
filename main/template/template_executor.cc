#include "template_executor.h"

#ifdef CONFIG_ENABLE_DOLL_INTERACTION

#include <esp_log.h>
#include "motion/motion_engine.h"
#include "led/led.h"
#include "boards/common/board.h"
#include "application.h"

#define TAG "TemplateExecutor"

TemplateExecutor::TemplateExecutor()
    : running_(false),
      executing_(false),
      current_action_index_(0),
      execution_task_handle_(nullptr) {
}

TemplateExecutor::~TemplateExecutor() {
    Stop();
}

void TemplateExecutor::Start() {
    if (running_) {
        return;
    }

    ESP_LOGI(TAG, "Starting TemplateExecutor...");
    running_ = true;

    xTaskCreate(ExecutionTask, "tmpl_exec", 4096, this, 5, &execution_task_handle_);

    ESP_LOGI(TAG, "TemplateExecutor started");
}

void TemplateExecutor::Stop() {
    if (!running_) {
        return;
    }

    ESP_LOGI(TAG, "Stopping TemplateExecutor...");
    
    running_ = false;
    executing_ = false;

    if (execution_task_handle_) {
        vTaskDelete(execution_task_handle_);
        execution_task_handle_ = nullptr;
    }

    ESP_LOGI(TAG, "TemplateExecutor stopped");
}

void TemplateExecutor::Execute(const Template& tmpl, std::function<void()> on_complete) {
    if (!running_) {
        ESP_LOGW(TAG, "Executor not running");
        return;
    }

    if (executing_) {
        ESP_LOGW(TAG, "Already executing a template, stopping it first");
        executing_ = false;
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGI(TAG, "Executing template: %s", tmpl.id.c_str());
    
    current_template_ = tmpl;
    current_action_index_ = 0;
    on_complete_ = on_complete;
    executing_ = true;
}

void TemplateExecutor::ExecutionTask(void* param) {
    TemplateExecutor* executor = static_cast<TemplateExecutor*>(param);
    executor->RunExecutionLoop();
    vTaskDelete(nullptr);
}

void TemplateExecutor::RunExecutionLoop() {
    ESP_LOGI(TAG, "Execution loop started");

    while (running_) {
        if (!executing_) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // 执行当前动作
        if (current_action_index_ < current_template_.sequence.size()) {
            const TemplateAction& action = current_template_.sequence[current_action_index_];
            ExecuteAction(action);
            current_action_index_++;
        } else {
            // 模板执行完成
            ESP_LOGI(TAG, "Template execution completed: %s", current_template_.id.c_str());
            
            executing_ = false;
            current_action_index_ = 0;

            if (on_complete_) {
                on_complete_();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGI(TAG, "Execution loop stopped");
}

void TemplateExecutor::ExecuteAction(const TemplateAction& action) {
    ESP_LOGD(TAG, "Executing action type: %d", static_cast<int>(action.type));

    switch (action.type) {
        case TemplateActionType::kMotion: {
            // 执行动作
            std::string motion_name = action.GetParam("name");
            if (!motion_name.empty()) {
                MotionEngine::GetInstance().PlayMotion(motion_name);
                // 等待动作完成
                while (MotionEngine::GetInstance().IsPlaying()) {
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
            }
            break;
        }

        case TemplateActionType::kLed: {
            // TODO: 执行灯光效果
            std::string effect = action.GetParam("effect");
            int duration = action.GetParamInt("duration", 1000);
            ESP_LOGI(TAG, "LED effect: %s, duration: %d", effect.c_str(), duration);
            
            // 简单实现：等待指定时间
            vTaskDelay(pdMS_TO_TICKS(duration));
            break;
        }

        case TemplateActionType::kTts: {
            // 播放 TTS
            std::string text = action.GetParam("text");
            bool sync_led = action.GetParamBool("sync_led", false);
            
            ESP_LOGI(TAG, "TTS: %s (sync_led: %d)", text.c_str(), sync_led);
            
            // TODO: 集成到 Application 的 TTS 播放
            // Application::GetInstance().PlaySound(...);
            
            // 模拟：等待播放时间
            int estimated_duration = text.length() * 200;  // 粗略估算
            vTaskDelay(pdMS_TO_TICKS(estimated_duration));
            break;
        }

        case TemplateActionType::kDelay: {
            // 延迟
            int duration = action.GetParamInt("duration", 1000);
            ESP_LOGD(TAG, "Delay: %d ms", duration);
            vTaskDelay(pdMS_TO_TICKS(duration));
            break;
        }

        case TemplateActionType::kParallel: {
            // 并行执行
            ESP_LOGI(TAG, "Executing %d parallel actions", action.sub_actions.size());
            ExecuteParallelActions(action.sub_actions);
            break;
        }

        default:
            ESP_LOGW(TAG, "Unknown action type: %d", static_cast<int>(action.type));
            break;
    }
}

void TemplateExecutor::ExecuteParallelActions(const std::vector<TemplateAction>& actions) {
    // TODO: 实现真正的并行执行
    // 当前简化实现：依次触发，不等待完成

    for (const auto& action : actions) {
        if (action.type == TemplateActionType::kMotion) {
            std::string motion_name = action.GetParam("name");
            if (!motion_name.empty()) {
                MotionEngine::GetInstance().PlayMotion(motion_name);
            }
        } else if (action.type == TemplateActionType::kLed) {
            // 触发灯光效果（异步）
            ESP_LOGI(TAG, "LED effect (async): %s", action.GetParam("effect").c_str());
        }
        // 其他动作类型暂不支持并行
    }

    // 等待所有动作完成（简化：等待动作引擎完成）
    while (MotionEngine::GetInstance().IsPlaying()) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

#endif // CONFIG_ENABLE_DOLL_INTERACTION

