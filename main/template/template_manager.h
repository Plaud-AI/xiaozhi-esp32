#ifndef TEMPLATE_MANAGER_H
#define TEMPLATE_MANAGER_H

#include <string>
#include <map>
#include <memory>
#include <functional>

#ifdef CONFIG_ENABLE_DOLL_INTERACTION

#include "template_types.h"

class TemplateParser;
class TemplateExecutor;

/**
 * @brief 模板管理器 - 负责模板的加载、管理和执行
 * 
 * 职责：
 * - 模板文件加载（本地/云端）
 * - 模板解析和验证
 * - 模板缓存管理
 * - 模板执行调度
 */
class TemplateManager {
public:
    static TemplateManager& GetInstance();

    // 初始化
    void Initialize();
    void Start();
    void Stop();
    bool IsRunning() const { return running_; }

    // 模板加载
    bool LoadTemplate(const std::string& template_json);
    bool LoadTemplateFromFile(const std::string& filepath);
    bool LoadTemplatesFromServer(const std::string& doll_id);
    void LoadDefaultTemplates();

    // 模板执行
    void ExecuteTemplate(const std::string& template_id);
    void StopCurrentTemplate();

    // 模板管理
    bool HasTemplate(const std::string& template_id) const;
    std::vector<std::string> GetTemplateList() const;
    bool RemoveTemplate(const std::string& template_id);
    void ClearAllTemplates();

    // 状态查询
    bool IsExecuting() const;
    std::string GetCurrentTemplate() const;

    // 回调
    void SetOnTemplateCompleteCallback(std::function<void()> callback);
    void SetOnTemplateErrorCallback(std::function<void(const std::string&)> callback);

private:
    TemplateManager();
    ~TemplateManager();
    TemplateManager(const TemplateManager&) = delete;
    TemplateManager& operator=(const TemplateManager&) = delete;

    bool ValidateTemplate(const Template& tmpl);

    bool running_;
    std::map<std::string, Template> templates_;
    std::string current_template_id_;

    std::unique_ptr<TemplateParser> parser_;
    std::unique_ptr<TemplateExecutor> executor_;

    std::function<void()> on_template_complete_;
    std::function<void(const std::string&)> on_template_error_;
};

#endif // CONFIG_ENABLE_DOLL_INTERACTION

#endif // TEMPLATE_MANAGER_H

