#ifndef TEMPLATE_EXECUTOR_H
#define TEMPLATE_EXECUTOR_H

#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "template_types.h"

#ifdef CONFIG_ENABLE_DOLL_INTERACTION

/**
 * @brief 模板执行器 - 负责执行模板动作序列
 */
class TemplateExecutor {
public:
    TemplateExecutor();
    ~TemplateExecutor();

    void Start();
    void Stop();

    // 执行模板
    void Execute(const Template& tmpl, std::function<void()> on_complete);

private:
    static void ExecutionTask(void* param);
    void RunExecutionLoop();
    void ExecuteAction(const TemplateAction& action);
    void ExecuteParallelActions(const std::vector<TemplateAction>& actions);

    bool running_;
    bool executing_;
    Template current_template_;
    size_t current_action_index_;
    std::function<void()> on_complete_;
    TaskHandle_t execution_task_handle_;
};

#endif // CONFIG_ENABLE_DOLL_INTERACTION

#endif // TEMPLATE_EXECUTOR_H

