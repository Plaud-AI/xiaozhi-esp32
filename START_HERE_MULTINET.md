# 🎉 MultiNet Only 改造 - 从这里开始！

## ✅ 改造已完成！

您的小智 ESP32 项目已成功改造为 **MultiNet Only 模式**（直接使用 MultiNet 作为唤醒模型，禁用 WakeNet）。

---

## 📦 改造内容

### 已修改的文件

✅ **`main/audio/wake_words/custom_wake_word.cc`** - 核心实现（已备份原文件）  
✅ **英文音素唤醒词** - 与参考项目完全一致  
✅ **直接检测模式** - 无需 WakeNet，MultiNet 持续检测

### 默认唤醒词

- 🎤 **"Hi Plaud"** (音素: `hi PLeD`)
- 🎤 **"Hi Nicebuild"** (音素: `hi NgSgBcLD`)

---

## 🚀 立即开始（3 步）

### 步骤 1: 配置 sdkconfig ⚙️

打开终端，运行：

```bash
cd /Users/xionghao/Documents/plaud/GitHub/xiaozhi-esp32
idf.py menuconfig
```

**导航并选择:**

1. **XiaoZhi AI Configuration** → **Wake Word Configuration**  
   选择: `(*) Multinet model (Custom Wake Word)` ✓

2. **Component config** → **ESP Speech Recognition**  
   → **Speech Recognition Model Configuration**  
   选择: `[*] Select MultiNet5 Quantized (English)` ✓

3. 按 `S` 保存，按 `Q` 退出

---

### 步骤 2: 编译烧录 🔨

```bash
# 清理旧构建
idf.py fullclean

# 重新配置
idf.py reconfigure

# 编译
idf.py build

# 烧录并监控
idf.py flash monitor
```

**预期日志（关键部分）:**
```
✓ Found MultiNet model: mn5q8_en (language: en)
✓ Adding 2 wake word commands:
  [0] command="hi PLeD", text="hi plaud", action="wake"
  [1] command="hi NgSgBcLD", text="hi nicebuild", action="wake"
✓ Successfully added 2 commands to MultiNet
✓ CustomWakeWord initialized successfully (MultiNet only mode)
```

---

### 步骤 3: 测试唤醒 🎙️

1. **等待设备进入待机** → 串口显示 `STATE: idle`

2. **清晰地说出唤醒词:**
   - 🗣️ **"Hi Plaud"** (英文发音)
   - 🗣️ **"Hi Nicebuild"** (英文发音)

3. **观察日志:**
```
✓ Detected command ID: 0, prob: 0.85
✓ Wake word detected: hi plaud
*** Wake word detected: hi plaud ***
Opening audio channel...
STATE: connecting
```

**成功标志:** 设备开始连接服务器，进入对话模式 🎉

---

## 📚 详细文档

| 文档 | 说明 | 何时查看 |
|-----|------|---------|
| **`START_HERE_MULTINET.md`** | 本文档 - 快速开始 | 📍 **立即查看** |
| **`MULTINET_QUICK_START.md`** | 快速开始指南 | 需要配置和测试步骤 |
| **`MULTINET_ONLY_MIGRATION.md`** | 详细改造方案 | 需要了解实现细节 |
| **`CHANGES_MULTINET.md`** | 文件变更总结 | 需要了解哪些文件被修改 |

---

## 🎨 自定义（可选）

### 添加更多唤醒词

编辑 `main/audio/wake_words/custom_wake_word.cc` 第 101-105 行：

```cpp
// 现有的
commands_.push_back({"hi PLeD", "hi plaud", "wake"});
commands_.push_back({"hi NgSgBcLD", "hi nicebuild", "wake"});

// 添加新的（示例）
commands_.push_back({"hi TpG", "hi espressif", "wake"});
```

然后重新编译：`idf.py build && idf.py flash`

### 调整灵敏度

编辑 `main/audio/wake_words/custom_wake_word.cc` 第 99 行：

```cpp
threshold_ = 0.5;  // 范围: 0.0-1.0
// 0.3 = 更敏感（容易触发）
// 0.5 = 平衡（推荐）
// 0.7 = 更严格（不容易误触发）
```

---

## ⚠️ 故障排除

### ❌ 编译错误: 找不到 MultiNet 模型

**解决:**
```bash
idf.py menuconfig
# 确保: CONFIG_SR_MN5Q8_EN=y
```

### ❌ 无法检测到唤醒词

**检查:**
1. 麦克风是否工作（日志显示音频电平不为 0）
2. 发音是否清晰（英文发音）
3. 环境是否安静

**调试:**
查看日志中的音频电平：
```
Audio level check: avg=XXX, max=XXX
```
如果 `avg=0, max=0` → 麦克风问题！

---

## 🔄 回滚到原版本

如果需要恢复到 WakeNet 模式：

```bash
cd /Users/xionghao/Documents/plaud/GitHub/xiaozhi-esp32

# 恢复原文件
cp main/audio/wake_words/custom_wake_word.cc.backup \
   main/audio/wake_words/custom_wake_word.cc

# 修改配置
idf.py menuconfig
# 选择: Wake Word Configuration → Wakenet model with AFE

# 重新编译
idf.py fullclean
idf.py build
```

---

## 📖 参考项目对比

### esp-sr-multinet（参考项目）

- 📁 路径: `/Users/xionghao/Documents/plaud/GitHub/esp-sr-multinet`
- 📄 核心文件: `main/blink_example_main.c`
- ✅ 唤醒词: "hi plaud", "hi nicebuild"
- ✅ 模式: MultiNet only（禁用 WakeNet）

### xiaozhi-esp32（改造后）

- 📁 路径: `/Users/xionghao/Documents/plaud/GitHub/xiaozhi-esp32`
- 📄 核心文件: `main/audio/wake_words/custom_wake_word.cc`
- ✅ 唤醒词: "hi plaud", "hi nicebuild"（完全一致）
- ✅ 模式: MultiNet only（禁用 WakeNet）
- ✅ 检测逻辑: 与参考项目一致

---

## ✅ 检查清单

完成以下步骤以确保改造成功：

- [ ] 已运行 `idf.py menuconfig` 并选择 CustomWakeWord
- [ ] 已选择 MultiNet 英文模型 (CONFIG_SR_MN5Q8_EN=y)
- [ ] 已运行 `idf.py fullclean && idf.py build`
- [ ] 编译成功，看到关键日志
- [ ] 已烧录固件 (`idf.py flash`)
- [ ] 设备启动，进入待机状态
- [ ] 说 "Hi Plaud" 能成功唤醒
- [ ] 说 "Hi Nicebuild" 能成功唤醒
- [ ] 唤醒后能正常对话

---

## 🎯 下一步

### 如果测试成功 ✅

恭喜！您的 MultiNet Only 改造已完成。可以：
- 添加自定义唤醒词（中文或英文）
- 调整检测参数（阈值、超时）
- 集成到您的应用中

### 如果遇到问题 ❌

1. 查看 **`MULTINET_QUICK_START.md`** 的故障排除部分
2. 检查日志中的错误信息
3. 确认 sdkconfig 配置正确

### 需要更多信息 📖

- **实现细节**: 查看 `MULTINET_ONLY_MIGRATION.md`
- **文件变更**: 查看 `CHANGES_MULTINET.md`
- **ESP-SR 文档**: https://docs.espressif.com/projects/esp-sr/

---

## 💬 反馈

如果改造成功，日志应显示：

```
✅ CustomWakeWord initialized successfully (MultiNet only mode)
✅ ✓ Wake word detected: hi plaud
✅ *** Wake word detected: hi plaud ***
✅ Opening audio channel...
```

---

**改造版本:** v1.0  
**完成日期:** 2025-10-10  
**测试板型:** ESP32-S3-Korvo-2 V3.0  
**参考项目:** esp-sr-multinet

🚀 **祝您改造顺利！**

