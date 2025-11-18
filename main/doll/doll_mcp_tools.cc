#include "doll_mcp_tools.h"

#include <esp_log.h>

#ifdef CONFIG_ENABLE_DOLL_INTERACTION

#include "mcp_server.h"
#include "doll_interaction_manager.h"
#include "motion/motion_engine.h"
#include "template/template_manager.h"

#define TAG "DollMcpTools"

void RegisterDollMcpTools() {
    ESP_LOGI(TAG, "Registering doll interaction MCP tools...");

    auto& mcp = McpServer::GetInstance();

    // 1. 模拟手办放置
    mcp.AddTool(
        "test_doll_placed",
        "模拟手办放置事件（用于测试）",
        PropertyList({
            Property("doll_id", kPropertyTypeString, std::string("default"))
        }),
        [](const PropertyList& props) -> ReturnValue {
            std::string doll_id = props["doll_id"].value<std::string>();
            DollInteractionManager::GetInstance().SimulateDollPlaced(doll_id);
            return "Simulated doll placed: " + doll_id;
        }
    );

    // 2. 模拟手办移除
    mcp.AddTool(
        "test_doll_removed",
        "模拟手办移除事件（用于测试）",
        PropertyList(std::vector<Property>{}),
        [](const PropertyList& props) -> ReturnValue {
            DollInteractionManager::GetInstance().SimulateDollRemoved();
            return "Simulated doll removed";
        }
    );

    // 3. 模拟触摸
    mcp.AddTool(
        "test_touch",
        "模拟触摸事件（用于测试）",
        PropertyList({
            Property("position", kPropertyTypeInteger, 1, 0, 4)  // 0=None, 1=Top, 2=Left, 3=Right, 4=Bottom
        }),
        [](const PropertyList& props) -> ReturnValue {
            int pos = props["position"].value<int>();
            TouchPosition touch_pos = static_cast<TouchPosition>(pos);
            DollInteractionManager::GetInstance().SimulateTouch(touch_pos);
            return "Simulated touch at position: " + std::to_string(pos);
        }
    );

    // 4. 测试动作
    mcp.AddTool(
        "test_motion",
        "播放指定的动作",
        PropertyList({
            Property("motion_name", kPropertyTypeString)
        }),
        [](const PropertyList& props) -> ReturnValue {
            std::string motion_name = props["motion_name"].value<std::string>();
            
            if (!MotionEngine::GetInstance().HasMotion(motion_name)) {
                return "Motion not found: " + motion_name;
            }
            
            MotionEngine::GetInstance().PlayMotion(motion_name);
            return "Playing motion: " + motion_name;
        }
    );

    // 5. 测试模板
    mcp.AddTool(
        "test_template",
        "执行指定的模板",
        PropertyList({
            Property("template_id", kPropertyTypeString)
        }),
        [](const PropertyList& props) -> ReturnValue {
            std::string template_id = props["template_id"].value<std::string>();
            
            if (!TemplateManager::GetInstance().HasTemplate(template_id)) {
                return "Template not found: " + template_id;
            }
            
            TemplateManager::GetInstance().ExecuteTemplate(template_id);
            return "Executing template: " + template_id;
        }
    );

    // 6. 列出已注册的手办
    mcp.AddTool(
        "list_dolls",
        "列出所有已注册的手办配置",
        PropertyList(std::vector<Property>{}),
        [](const PropertyList& props) -> ReturnValue {
            // TODO: 实现获取手办列表的接口
            return "Doll list:\n- default\n- xiaozhi_001\n- pikachu_001";
        }
    );

    // 7. 列出可用的动作
    mcp.AddTool(
        "list_motions",
        "列出所有可用的动作预设",
        PropertyList(std::vector<Property>{}),
        [](const PropertyList& props) -> ReturnValue {
            // 获取动作列表
            std::string result = "Available motions:\n";
            
            // TODO: 从 MotionEngine 获取动作列表
            result += "- welcome\n";
            result += "- goodbye\n";
            result += "- thinking\n";
            result += "- happy\n";
            result += "- angry\n";
            
            return result;
        }
    );

    // 8. 列出可用的模板
    mcp.AddTool(
        "list_templates",
        "列出所有可用的模板",
        PropertyList(std::vector<Property>{}),
        [](const PropertyList& props) -> ReturnValue {
            auto template_list = TemplateManager::GetInstance().GetTemplateList();
            
            std::string result = "Available templates:\n";
            for (const auto& tmpl_id : template_list) {
                result += "- " + tmpl_id + "\n";
            }
            
            if (template_list.empty()) {
                result = "No templates available";
            }
            
            return result;
        }
    );

    // 9. 获取当前状态
    mcp.AddTool(
        "get_doll_status",
        "获取手办交互系统当前状态",
        PropertyList(std::vector<Property>{}),
        [](const PropertyList& props) -> ReturnValue {
            auto& mgr = DollInteractionManager::GetInstance();
            
            std::string result = "Doll Interaction Status:\n";
            result += "- Running: " + std::string(mgr.IsRunning() ? "yes" : "no") + "\n";
            result += "- Has Active Doll: " + std::string(mgr.HasActiveDoll() ? "yes" : "no") + "\n";
            
            if (mgr.HasActiveDoll()) {
                result += "- Active Doll ID: " + mgr.GetActiveDollId() + "\n";
                result += "- Active Doll Name: " + mgr.GetActiveDollName() + "\n";
            }
            
            result += "- Motion Playing: " + std::string(MotionEngine::GetInstance().IsPlaying() ? "yes" : "no") + "\n";
            result += "- Template Executing: " + std::string(TemplateManager::GetInstance().IsExecuting() ? "yes" : "no") + "\n";
            
            return result;
        }
    );

    ESP_LOGI(TAG, "Doll interaction MCP tools registered (9 tools)");
}

#else // CONFIG_ENABLE_DOLL_INTERACTION

// Stub implementation when doll interaction is disabled
void RegisterDollMcpTools() {
    // Do nothing - doll interaction feature is disabled
}

#endif // CONFIG_ENABLE_DOLL_INTERACTION

