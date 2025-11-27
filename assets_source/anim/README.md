# Lottie 动画资源目录

## 📋 目录说明

此目录存放 Lottie 表情动画文件，会在编译时**自动打包**到 `assets` 分区。

## 🎯 重要规则

### ⚠️ 文件命名

- **必须移除 `.json` 后缀**（Assets 系统要求）
- 使用英文小写和下划线：`happy`, `sad`, `excited`
- 避免使用中文和特殊字符

### ✅ 正确示例

```
anim/
├── happy           ✅ 正确（无后缀）
├── sad             ✅ 正确
├── excited         ✅ 正确
```

### ❌ 错误示例

```
anim/
├── happy.json      ❌ 错误（有后缀）
├── 开心            ❌ 错误（中文）
├── Happy           ❌ 不推荐（大写）
```

---

## 📦 当前动画列表

| 文件名 | 中文名 | 情感类型 | 大小 | 用途 |
|--------|--------|---------|------|------|
| `happy` | 开心 | HAPPY | 16KB | 表达开心、愉快的情绪 |
| `sad` | 悲伤 | SAD | 13KB | 表达悲伤、难过的情绪 |
| `excited` | 小骄傲 | EXCITED | 17KB | 表达兴奋、骄傲的情绪 |
| `calm` | 满足 | CALM | 11KB | 表达平静、满足的情绪 |
| `sleepy` | 困倦 | SLEEPY | 10KB | 表达困倦、疲惫的情绪 |
| `surprised` | 好奇 | SURPRISED | 13KB | 表达惊讶、好奇的情绪 |
| `listening` | 听音乐 | LISTENING | 30KB | 聆听、专注状态 |
| `singing` | 唱歌 | SINGING | 25KB | 表演、唱歌状态 |
| `speaking` | 说话 | SPEAKING | - | 说话、讲述状态 |
| `thinking` | 思考 | THINKING | - | 思考、处理状态 |
| `disdain` | 不屑 | - | 8KB | 表达不屑、轻视的情绪 |
| `disgust` | 吐 | - | 21KB | 表达厌恶、吐槽的情绪 |
| `champion` | NBA冠军戒指 | - | 18KB | 表达成功、庆祝 |

---

## 🔧 添加新动画

### 步骤 1：准备文件

```bash
# 从原始 JSON 文件复制并移除后缀
cp /path/to/新表情.json assets_source/anim/新表情
```

### 步骤 2：重新编译

```bash
cd /path/to/xiaozhi-esp32
idf.py build flash
```

### 步骤 3：验证

查看设备启动日志，确认动画已加载：

```
I (xxx) EmotionAssetsLoader: Registered 12 animations from assets
```

---

## 🏗️ 技术细节

### 自动打包流程

```
编译开始
  ↓
CMakeLists.txt 检测到 DEFAULT_ASSETS_EXTRA_FILES
  ↓
调用 build_default_assets.py --extra_files assets_source
  ↓
复制 assets_source/anim/* 到临时目录
  ↓
生成 assets.bin（包含动画）
  ↓
烧录到 assets 分区 @ 0xa20000
```

### 运行时访问

```cpp
// 代码中通过 Assets API 访问
Assets::GetInstance().GetAssetData("anim/happy", ptr, size);
  ↓
返回 memory-mapped 指针（零拷贝）
  ↓
lv_lottie_set_src_data(obj, ptr, size);
```

---

## 🐛 常见问题

### 问题 1：动画不显示

**检查步骤：**

1. 确认文件**无 `.json` 后缀**
2. 查看编译日志是否有 `Copied: assets_source/anim/xxx`
3. 查看运行日志是否有 "Registered X animations"

### 问题 2：编译时未包含动画

**解决方案：**

```bash
# 清理构建缓存
idf.py fullclean

# 重新编译
idf.py build flash
```

### 问题 3：文件名包含特殊字符

**解决方案：**

重命名文件，只使用英文字母、数字和下划线：

```bash
mv "新表情" "new_emotion"
```

---

## 📚 相关文档

- **完整集成指南**: `/README-Lottie动画集成.md`
- **情感系统文档**: `/docs/xiaozhi-esp32/display/emotion-system.md`
- **Assets 系统**: `components/esp_mmap_assets/README.md`

---

## 🔄 更新历史

- **2024-11-28**: 集成到自动构建流程（CMakeLists.txt）
- **2024-11-27**: 创建目录，添加初始动画

---

## 💡 提示

- 动画文件会被打包到 Flash，**不占用 RAM**
- 使用 memory-mapped 访问，**零拷贝**，性能高
- 支持热更新：只需替换文件并重新编译
- 推荐将大型动画优化到 30KB 以下

---

**维护者**: Xiaozhi ESP32 Team  
**最后更新**: 2024-11-28

