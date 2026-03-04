# 唤醒词推理测试 - 服务端 API 文档

## 概述

设备端在检测到唤醒词时，会将本次检测过程中的原始音频和各模型推理概率上传到测试服务器，用于离线对比分析和模型调优。

**Base URL**: `http://<server>:7007`

当前配置：`http://115.190.161.149:7007`（见 `inference_test_uploader.h` 中 `server_url_`）

## 当前唤醒模型

| 模型名称 | model 参数值 | 说明 |
|----------|-------------|------|
| Hey Plaud V4 | `hey plaud` | Plaud AI 定制唤醒词模型 |
| Okay Nabu | `okay nabu` | ESPHome 官方 v2 参考模型 |

两个模型共享同一段音频输入，并行推理，概率分别记录和上传。

## 上传时序

每次检测到唤醒词后，设备按以下顺序发起请求：

```
1. POST /upload/bytes                      ← 上传 PCM 音频（共享）
2. POST /save/bytes                        ← 通知服务端保存 PCM 文件
3. POST /upload/text?model=hey plaud       ← 上传 hey plaud 推理概率
4. POST /save/text?model=hey plaud         ← 通知服务端保存 hey plaud 概率文件
5. POST /upload/text?model=okay nabu       ← 上传 okay nabu 推理概率
6. POST /save/text?model=okay nabu         ← 通知服务端保存 okay nabu 概率文件
```

总计 `2 + 2N` 次请求（N = 模型数量，当前 N=2，共 6 次）。

## 接口详情

### 1. `POST /upload/bytes` — 上传 PCM 音频

上传本次检测过程的原始音频数据，所有模型共享同一段音频。

| 项目 | 说明 |
|------|------|
| **Content-Type** | `application/octet-stream` |
| **Body** | 原始二进制 PCM 数据 |
| **音频格式** | int16_t, 16kHz, 单声道, little-endian |
| **最大数据量** | ~656 KB（21 秒，336000 samples × 2 bytes） |
| **成功响应** | `200 OK` |

**示例**:
```
POST /upload/bytes HTTP/1.1
Content-Type: application/octet-stream
Content-Length: 672000

<binary PCM data>
```

### 2. `POST /save/bytes` — 保存 PCM 到文件

通知服务端将之前 `/upload/bytes` 接收到的数据持久化保存。

| 项目 | 说明 |
|------|------|
| **Body** | 无（空 POST） |
| **建议文件名** | 按时间戳命名，如 `20260304_143022.pcm` |
| **成功响应** | `200 OK` |

### 3. `POST /upload/text?model=<model_name>` — 上传模型推理概率

上传指定模型在本次检测过程中每次推理的原始输出概率。

| 项目 | 说明 |
|------|------|
| **Query 参数** | `model` — 模型名称 |
| **Content-Type** | `text/plain; charset=utf-8` |
| **Body** | 逗号分隔的浮点数序列 |
| **值范围** | 0.0000 ~ 1.0000（4 位小数） |
| **最大值数量** | ~2100 个（覆盖 21 秒，每 ~10ms 一个值） |
| **成功响应** | `200 OK` |

**Body 示例**:
```
0.0000,0.0000,0.0000,0.0039,0.0118,0.0549,0.2353,0.5412,0.8235,0.9216
```

**每个值的含义**:
- 模型单次推理的原始输出概率（滑动窗口平均前的值）
- 由 TFLite 模型 uint8 输出 (0~255) 除以 255.0 转换而来
- 值间时间间隔约 10ms（取决于 `feature_step_size` 配置）

**`model` 参数取值**:

| 值 | URL 示例 |
|----|----------|
| `hey plaud` | `/upload/text?model=hey%20plaud` 或 `/upload/text?model=hey+plaud` |
| `okay nabu` | `/upload/text?model=okay%20nabu` 或 `/upload/text?model=okay+nabu` |

### 4. `POST /save/text?model=<model_name>` — 保存概率到文件

通知服务端将之前 `/upload/text` 接收到的对应模型的概率数据持久化保存。

| 项目 | 说明 |
|------|------|
| **Query 参数** | `model` — 模型名称（与上传时一致） |
| **Body** | 无（空 POST） |
| **建议文件名** | 用模型名区分，如 `20260304_143022_hey_plaud.csv`、`20260304_143022_okay_nabu.csv` |
| **成功响应** | `200 OK` |

## 数据对齐关系

```
时间轴:  0ms ─────────────────────────────────── 21000ms
         │                                        │
PCM:     [========= 16kHz int16 音频 ===========]
         │                                        │
hey plaud 概率: [p0, p1, p2, ..., p2099]  (每 ~10ms 一个)
okay nabu 概率: [p0, p1, p2, ..., p2099]  (每 ~10ms 一个)
```

- PCM 音频和所有模型的概率序列在时间上是对齐的
- 概率序列的第 i 个值对应音频中 `i × 10ms` 位置的推理结果
- 可用于离线回放音频并对比各模型的响应差异

## 注意事项

1. **超时**：所有 HTTP 请求超时 15 秒
2. **队列**：设备端非阻塞提交，队列最多缓存 10 个数据包；队列满时丢弃最新数据
3. **URL 编码**：`model` 参数值包含空格，服务端需正确处理 `%20` 或 `+` 编码
4. **错误处理**：服务端应对所有接口返回 `200 OK` 表示成功，非 200 状态码会被设备端视为失败并记录日志
5. **存储建议**：每次检测建议将 PCM 和各模型概率保存到同一目录或使用相同的时间戳前缀，便于后续对比分析
