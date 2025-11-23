#include "template_parser.h"

#ifdef CONFIG_ENABLE_DOLL_INTERACTION

#include <esp_log.h>

#define TAG "TemplateParser"

bool TemplateParser::Parse(const std::string& json_str, Template& out_template) {
    cJSON* root = cJSON_Parse(json_str.c_str());
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        return false;
    }

    // 解析基本信息
    cJSON* id = cJSON_GetObjectItem(root, "template_id");
    if (id && cJSON_IsString(id)) {
        out_template.id = id->valuestring;
    }

    cJSON* version = cJSON_GetObjectItem(root, "version");
    if (version && cJSON_IsString(version)) {
        out_template.version = version->valuestring;
    }

    cJSON* description = cJSON_GetObjectItem(root, "description");
    if (description && cJSON_IsString(description)) {
        out_template.description = description->valuestring;
    }

    cJSON* doll_id = cJSON_GetObjectItem(root, "doll_id");
    if (doll_id && cJSON_IsString(doll_id)) {
        out_template.doll_id = doll_id->valuestring;
    }

    // 解析动作序列
    cJSON* sequence = cJSON_GetObjectItem(root, "sequence");
    if (sequence && cJSON_IsArray(sequence)) {
        int count = cJSON_GetArraySize(sequence);
        for (int i = 0; i < count; i++) {
            cJSON* action_json = cJSON_GetArrayItem(sequence, i);
            TemplateAction action;
            if (ParseAction(action_json, action)) {
                out_template.sequence.push_back(action);
            }
        }
    }

    cJSON_Delete(root);
    
    ESP_LOGI(TAG, "Parsed template: %s (%d actions)", 
             out_template.id.c_str(), out_template.sequence.size());
    return true;
}

bool TemplateParser::ParseAction(const cJSON* json, TemplateAction& out_action) {
    if (!json || !cJSON_IsObject(json)) {
        return false;
    }

    // 解析类型
    cJSON* type = cJSON_GetObjectItem(json, "type");
    if (!type || !cJSON_IsString(type)) {
        ESP_LOGE(TAG, "Action type missing or invalid");
        return false;
    }

    out_action.type = StringToActionType(type->valuestring);

    // 解析参数（除了 type 和 actions 的其他字段）
    cJSON* item = json->child;
    while (item) {
        if (cJSON_IsString(item) && strcmp(item->string, "type") != 0) {
            out_action.params[item->string] = item->valuestring;
        } else if (cJSON_IsNumber(item)) {
            out_action.params[item->string] = std::to_string(item->valueint);
        } else if (cJSON_IsBool(item)) {
            out_action.params[item->string] = cJSON_IsTrue(item) ? "true" : "false";
        }
        item = item->next;
    }

    // 如果是并行动作，解析子动作
    if (out_action.type == TemplateActionType::kParallel) {
        cJSON* actions = cJSON_GetObjectItem(json, "actions");
        if (actions && cJSON_IsArray(actions)) {
            int count = cJSON_GetArraySize(actions);
            for (int i = 0; i < count; i++) {
                cJSON* sub_json = cJSON_GetArrayItem(actions, i);
                TemplateAction sub_action;
                if (ParseAction(sub_json, sub_action)) {
                    out_action.sub_actions.push_back(sub_action);
                }
            }
        }
    }

    return true;
}

TemplateActionType TemplateParser::StringToActionType(const std::string& type_str) {
    if (type_str == "motion") return TemplateActionType::kMotion;
    if (type_str == "led") return TemplateActionType::kLed;
    if (type_str == "tts") return TemplateActionType::kTts;
    if (type_str == "delay") return TemplateActionType::kDelay;
    if (type_str == "parallel") return TemplateActionType::kParallel;
    
    ESP_LOGW(TAG, "Unknown action type: %s, defaulting to delay", type_str.c_str());
    return TemplateActionType::kDelay;
}

#endif // CONFIG_ENABLE_DOLL_INTERACTION

