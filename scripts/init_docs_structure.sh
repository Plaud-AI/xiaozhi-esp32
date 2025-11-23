#!/bin/bash

# 初始化 Xiaozhi ESP32 项目文档目录结构
# 此脚本会在 /Users/xionghao/Documents/GitHub/docs/xiaozhi-esp32/ 下创建标准目录结构

DOCS_BASE="/Users/xionghao/Documents/GitHub/docs/xiaozhi-esp32"

echo "开始创建文档目录结构..."
echo "基础路径: $DOCS_BASE"

# 创建主目录
mkdir -p "$DOCS_BASE"

# 创建各大模块目录（大模块粒度）
mkdir -p "$DOCS_BASE/audio"
mkdir -p "$DOCS_BASE/boards"
mkdir -p "$DOCS_BASE/bluetooth"
mkdir -p "$DOCS_BASE/display"
mkdir -p "$DOCS_BASE/led"
mkdir -p "$DOCS_BASE/mcp"
mkdir -p "$DOCS_BASE/network"
mkdir -p "$DOCS_BASE/ota"
mkdir -p "$DOCS_BASE/system"
mkdir -p "$DOCS_BASE/troubleshooting"
mkdir -p "$DOCS_BASE/tutorials"

# 创建根目录 README
cat > "$DOCS_BASE/README.md" << 'EOF'
# Xiaozhi ESP32 项目文档

欢迎来到小智 ESP32 AI 聊天机器人项目文档库。

## 📁 文档结构

本文档库按功能模块组织，包含以下大模块：

| 模块 | 说明 |
|------|------|
| 📢 [audio/](./audio/) | 音频处理相关（编解码、处理器、唤醒词） |
| 🔧 [boards/](./boards/) | 开发板配置和移植指南 |
| 📡 [bluetooth/](./bluetooth/) | 蓝牙和 BLE 配网相关 |
| 🖥️ [display/](./display/) | 显示相关（LCD、OLED、LVGL） |
| 💡 [led/](./led/) | LED 灯效控制 |
| 🔌 [mcp/](./mcp/) | MCP 协议相关 |
| 🌐 [network/](./network/) | 网络通信（WiFi、MQTT、WebSocket、UDP） |
| 🔄 [ota/](./ota/) | 固件 OTA 升级 |
| ⚙️ [system/](./system/) | 系统架构、配置、电源管理 |
| 🔍 [troubleshooting/](./troubleshooting/) | 故障排查和常见问题 |
| 📚 [tutorials/](./tutorials/) | 教程（入门、开发、部署） |

## 📝 文档规范

所有文档遵循统一的编写规范，详见项目根目录的 `.cursorrules` 文件。

文档命名规范：
- 使用小写字母和连字符：`example-document.md`
- 避免使用空格和特殊字符
- 使用描述性的文件名

## 🤖 自动文档管理

本项目配置了 Cursor Rules，AI 助手在生成文档时会：
- 自动识别文档所属模块
- 将文档保存到正确的目录
- 使用统一的文档模板
- 遵循项目规范

## 🔗 相关链接

- [项目仓库](https://github.com/78/xiaozhi-esp32)
- [官方网站](https://xiaozhi.me)
- [详细教程](https://ccnphfhqs21z.feishu.cn/wiki/F5krwD16viZoF0kKkvDcrZNYnhb)
- QQ 群：1011329060

---

**最后更新**: 2025-11-17  
**维护者**: 虾哥（78）
EOF

echo "✅ 文档目录结构创建完成！"
echo ""
echo "目录结构："
tree -L 2 "$DOCS_BASE" 2>/dev/null || find "$DOCS_BASE" -type d -maxdepth 2 | sort
echo ""
echo "📝 已创建 README.md 文件: $DOCS_BASE/README.md"
echo ""
echo "💡 提示: 现在可以开始在相应的模块目录下创建文档了！"

