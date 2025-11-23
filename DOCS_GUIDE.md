# 文档管理指南

本项目已配置统一的文档管理规范。在后续使用 Cursor AI 助手交互时，生成的文档会自动保存到外部文档目录并按模块分类。

## 📁 文档存储位置

所有项目文档统一存放到：

```
/Users/xionghao/Documents/GitHub/docs/xiaozhi-esp32/
```

## 🗂️ 目录结构（大模块粒度）

文档按照功能模块组织，包含以下 11 个大模块：

| 模块 | 说明 | 示例 |
|------|------|------|
| `audio/` | 音频处理相关 | 编解码器、处理器、唤醒词 |
| `boards/` | 开发板配置和移植 | 开发板移植指南、硬件规格 |
| `bluetooth/` | 蓝牙和 BLE 配网 | BLE 配网、蓝牙服务 |
| `display/` | 显示相关 | LCD、OLED、LVGL |
| `led/` | LED 控制 | LED 驱动、灯效控制 |
| `mcp/` | MCP 协议 | MCP 服务器、协议规范 |
| `network/` | 网络通信 | WiFi、MQTT、WebSocket、UDP |
| `ota/` | OTA 升级 | 固件升级、版本管理 |
| `system/` | 系统相关 | 架构、配置、电源管理 |
| `troubleshooting/` | 故障排查 | 常见问题、调试指南 |
| `tutorials/` | 教程 | 入门、开发、部署教程 |

## 🤖 AI 助手自动文档管理

本项目已配置 `.cursorrules` 文件，在您与 Cursor AI 助手交互过程中：

### 自动处理流程

1. ✅ **自动识别模块** - AI 根据讨论内容识别文档所属模块
2. ✅ **自动保存位置** - 将文档保存到对应的模块目录
3. ✅ **自动应用模板** - 使用统一的文档模板和格式
4. ✅ **自动命名规范** - 遵循项目的命名规范

### 使用示例

您只需要自然地与 AI 对话：

```
"帮我写一个关于 OPUS 编解码的文档"
→ AI 会自动保存到 /Users/xionghao/Documents/GitHub/docs/xiaozhi-esp32/audio/

"写个蓝牙配网的故障排查指南"
→ AI 会自动保存到 /Users/xionghao/Documents/GitHub/docs/xiaozhi-esp32/bluetooth/

"创建开发板移植教程"
→ AI 会自动保存到 /Users/xionghao/Documents/GitHub/docs/xiaozhi-esp32/boards/
```

AI 会自动处理其余事项，无需手动指定路径。

## 📝 文档规范

### 命名规范
- ✅ 使用小写字母和连字符：`audio-codec-implementation.md`
- ✅ 使用描述性的名称：`ble-wifi-provisioning-guide.md`
- ❌ 避免使用空格：~~`audio codec.md`~~
- ❌ 避免使用特殊字符：~~`audio@codec.md`~~

### 代码规范
- C/C++ 代码遵循 Google C++ Style Guide
- Python 代码遵循 PEP 8
- 代码示例包含必要注释
- 示例代码应可直接运行

### 图片资源
- 图片放在同目录下的 `images/` 子目录
- 使用描述性的图片文件名
- 在文档中使用相对路径引用

## 🔗 相关链接

- [项目 README](./README.md)
- [Cursor Rules 配置](./.cursorrules)
- [文档目录](file:///Users/xionghao/Documents/GitHub/docs/xiaozhi-esp32/)
- [飞书教程](https://ccnphfhqs21z.feishu.cn/wiki/F5krwD16viZoF0kKkvDcrZNYnhb)
- [官方网站](https://xiaozhi.me)
- QQ 群：1011329060

---

**说明**: 本指南放在项目根目录方便查看，实际生成的文档会自动保存到外部文档目录。

