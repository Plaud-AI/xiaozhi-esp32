#ifndef TEMPLATE_TYPES_H
#define TEMPLATE_TYPES_H

#include <string>
#include <vector>
#include <map>

#ifdef CONFIG_ENABLE_DOLL_INTERACTION

/**
 * @brief 模板动作类型
 */
enum class TemplateActionType {
    kMotion,        // 动作
    kLed,           // 灯光
    kTts,           // 语音播报
    kDelay,         // 延迟
    kParallel,      // 并行执行
};

/**
 * @brief 模板动作
 */
struct TemplateAction {
    TemplateActionType type;
    std::map<std::string, std::string> params;  // 参数键值对
    std::vector<TemplateAction> sub_actions;    // 并行动作的子动作

    TemplateAction() : type(TemplateActionType::kDelay) {}
    TemplateAction(TemplateActionType t) : type(t) {}

    // 辅助方法
    std::string GetParam(const std::string& key, const std::string& default_value = "") const {
        auto it = params.find(key);
        return (it != params.end()) ? it->second : default_value;
    }

    int GetParamInt(const std::string& key, int default_value = 0) const {
        auto it = params.find(key);
        return (it != params.end()) ? std::stoi(it->second) : default_value;
    }

    bool GetParamBool(const std::string& key, bool default_value = false) const {
        auto it = params.find(key);
        if (it == params.end()) return default_value;
        return (it->second == "true" || it->second == "1");
    }
};

/**
 * @brief 模板定义
 */
struct Template {
    std::string id;                         // 模板 ID
    std::string version;                    // 版本号
    std::string description;                // 描述
    std::string doll_id;                    // 适用的手办 ID（"*" 表示通用）
    std::vector<TemplateAction> sequence;   // 动作序列

    Template() = default;
};

#endif // CONFIG_ENABLE_DOLL_INTERACTION

#endif // TEMPLATE_TYPES_H

