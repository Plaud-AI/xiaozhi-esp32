# 模板示例文件

这个目录包含了一些示例模板文件，用于展示模板系统的使用方法。

## 模板格式说明

每个模板是一个 JSON 文件，包含以下字段：

- `template_id`: 模板唯一标识符
- `version`: 版本号
- `description`: 模板描述
- `doll_id`: 适用的手办 ID（"*" 表示通用模板）
- `sequence`: 动作序列数组

## 动作类型

### 1. motion（动作）
```json
{
  "type": "motion",
  "name": "welcome"
}
```

可用动作名称：
- `welcome`: 欢迎动作（点头）
- `goodbye`: 告别动作（摇晃）
- `thinking`: 思考动作（轻微点头）
- `happy`: 开心动作（快速点头）
- `angry`: 生气动作（激烈摇晃）

### 2. led（灯光）
```json
{
  "type": "led",
  "effect": "rainbow",
  "duration": 2000
}
```

参数：
- `effect`: 灯光效果名称（rainbow, fade_out, sparkle 等）
- `duration`: 持续时间（毫秒）

### 3. tts（语音播报）
```json
{
  "type": "tts",
  "text": "你好呀，主人！",
  "sync_led": true
}
```

参数：
- `text`: 要播报的文本
- `sync_led`: 是否同步显示灯光效果（true/false）

### 4. delay（延迟）
```json
{
  "type": "delay",
  "duration": 1000
}
```

参数：
- `duration`: 延迟时间（毫秒）

### 5. parallel（并行执行）
```json
{
  "type": "parallel",
  "actions": [
    {"type": "motion", "name": "welcome"},
    {"type": "led", "effect": "rainbow", "duration": 2000}
  ]
}
```

参数：
- `actions`: 子动作数组，这些动作会同时执行

## 示例模板说明

### greeting_happy.json
开心的欢迎模板，包含：
1. 并行执行欢迎动作和彩虹灯效
2. 播放欢迎语音
3. 短暂延迟

### farewell_sad.json
伤心的告别模板，包含：
1. 告别动作（摇晃）
2. 播放告别语音
3. 灯光渐灭效果

### touch_response.json
触摸响应模板，包含：
1. 并行执行开心动作和闪烁灯效
2. 播放俏皮的回应语音

## 如何使用

### 通过 MCP 工具测试
```
test_template template_id="greeting_happy"
```

### 在代码中加载
```cpp
TemplateManager::GetInstance().LoadTemplateFromFile("template/examples/greeting_happy.json");
```

### 自定义模板
1. 复制一个示例文件
2. 修改 `template_id` 和 `description`
3. 调整 `sequence` 中的动作序列
4. 使用 `test_template` 工具测试

## 注意事项

1. 模板 ID 必须唯一
2. 所有动作名称必须在 MotionEngine 中已注册
3. TTS 文本长度建议不超过 50 字
4. 灯光效果的具体实现取决于硬件
5. 并行动作不要包含互斥的操作（如多个 motion）

