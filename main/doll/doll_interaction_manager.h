#ifndef DOLL_INTERACTION_MANAGER_H
#define DOLL_INTERACTION_MANAGER_H

#include <string>
#include <map>
#include <functional>

#ifdef CONFIG_ENABLE_DOLL_INTERACTION

#include "doll_service.h"

/**
 * @brief 手办交互管理器 - 业务逻辑编排层
 * 
 * 职责：
 * - 协调 DollService、TemplateManager、MotionEngine 等模块
 * - 处理手办事件并触发相应的交互流程
 * - 管理手办配置和状态
 * - 与服务器同步手办信息
 */
class DollInteractionManager {
public:
    struct DollConfig {
        std::string id;
        std::string name;
        std::string greeting_template;   // 欢迎模板 ID
        std::string farewell_template;   // 告别模板 ID
        uint32_t main_color;             // 主题色（RGB）
        std::string personality;         // 性格描述（可用于 AI 对话）
    };

    static DollInteractionManager& GetInstance();

    // 初始化
    void Initialize();
    void Start();
    void Stop();
    bool IsRunning() const { return running_; }

    // 手办管理
    void RegisterDoll(const DollConfig& config);
    bool HasDoll(const std::string& doll_id) const;
    DollConfig GetDollConfig(const std::string& doll_id) const;

    // 状态查询
    bool HasActiveDoll() const;
    std::string GetActiveDollId() const;
    std::string GetActiveDollName() const;

    // 手动触发（用于 MCP 工具测试）
    void SimulateDollPlaced(const std::string& doll_id);
    void SimulateDollRemoved();
    void SimulateTouch(TouchPosition pos);

    // 事件回调（供 Application 使用）
    void SetOnDollChangedCallback(std::function<void(const std::string& doll_id)> callback);

private:
    DollInteractionManager();
    ~DollInteractionManager();
    DollInteractionManager(const DollInteractionManager&) = delete;
    DollInteractionManager& operator=(const DollInteractionManager&) = delete;

    // 事件处理
    void OnDollPlaced(const std::string& doll_id);
    void OnDollRemoved();
    void OnDollTouched(TouchPosition pos);

    // 交互流程
    void PlayGreeting(const std::string& doll_id);
    void PlayFarewell();
    void PlayTouchResponse(TouchPosition pos);

    // 加载配置
    void LoadDollConfigs();
    void LoadDefaultDollConfigs();

    bool running_;
    std::string active_doll_id_;
    std::map<std::string, DollConfig> doll_configs_;

    std::function<void(const std::string&)> on_doll_changed_;
};

#endif // CONFIG_ENABLE_DOLL_INTERACTION

#endif // DOLL_INTERACTION_MANAGER_H

