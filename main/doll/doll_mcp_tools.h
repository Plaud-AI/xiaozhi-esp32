#ifndef DOLL_MCP_TOOLS_H
#define DOLL_MCP_TOOLS_H

/**
 * @brief 注册手办交互相关的 MCP 工具
 * 
 * 当 CONFIG_ENABLE_DOLL_INTERACTION 启用时，注册以下工具：
 * - test_nfc: 模拟 NFC 读取
 * - test_doll_placed: 模拟手办放置
 * - test_doll_removed: 模拟手办移除
 * - test_touch: 模拟触摸
 * - test_motion: 测试动作
 * - test_template: 测试模板
 * - list_dolls: 列出已注册的手办
 * - list_motions: 列出可用的动作
 * - list_templates: 列出可用的模板
 * 
 * 当功能禁用时，此函数为空操作。
 */
void RegisterDollMcpTools();

#endif // DOLL_MCP_TOOLS_H

