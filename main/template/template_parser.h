#ifndef TEMPLATE_PARSER_H
#define TEMPLATE_PARSER_H

#include <string>
#include "template_types.h"

#ifdef CONFIG_ENABLE_DOLL_INTERACTION

#include <cJSON.h>

/**
 * @brief 模板解析器 - 负责 JSON 到 Template 的转换
 */
class TemplateParser {
public:
    TemplateParser() = default;
    ~TemplateParser() = default;

    // 解析模板 JSON
    bool Parse(const std::string& json_str, Template& out_template);

private:
    bool ParseAction(const cJSON* json, TemplateAction& out_action);
    TemplateActionType StringToActionType(const std::string& type_str);
};

#endif // CONFIG_ENABLE_DOLL_INTERACTION

#endif // TEMPLATE_PARSER_H

