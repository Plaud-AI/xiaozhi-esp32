# 本地组件目录

本目录包含项目所需的本地组件，使项目可以独立编译，不依赖外部项目。

## 已包含的组件

### 1. esp-tflite-micro
- **描述**: TensorFlow Lite Micro for ESP-IDF
- **来源**: https://github.com/espressif/tflite-micro-esp-examples
- **版本**: 本地副本
- **大小**: ~8.7 MB
- **用途**: 在 ESP32 上运行轻量级机器学习模型

**目录结构**:
```
esp-tflite-micro/
├── CMakeLists.txt           # CMake 构建配置
├── idf_component.yml        # IDF 组件清单
├── tensorflow/              # TensorFlow Lite 核心代码
│   └── lite/
│       ├── micro/           # Micro 版本实现
│       ├── kernels/         # 算子实现
│       └── schema/          # 模型格式定义
├── signal/                  # 信号处理库
│   ├── micro/
│   └── src/
└── third_party/             # 第三方依赖
    ├── flatbuffers/         # FlatBuffers 序列化库
    ├── gemmlowp/            # 低精度矩阵乘法
    ├── kissfft/             # FFT 实现
    └── ruy/                 # 矩阵乘法库
```

### 2. esp-nn
- **描述**: 优化的神经网络函数库
- **来源**: https://github.com/espressif/esp-nn
- **版本**: 1.1.2
- **大小**: ~756 KB
- **用途**: 为 TensorFlow Lite Micro 提供硬件加速的 NN 运算

**目录结构**:
```
esp-nn/
├── CMakeLists.txt           # CMake 构建配置
├── idf_component.yml        # IDF 组件清单
├── include/                 # 公共头文件
│   └── esp_nn.h
├── src/                     # 源代码
│   ├── activation_functions/  # 激活函数
│   ├── convolution/          # 卷积运算
│   ├── fully_connected/      # 全连接层
│   └── pooling/              # 池化运算
└── Kconfig.projbuild        # 配置选项
```

## 组件使用方式

### 自动发现

ESP-IDF 会自动发现 `components/` 目录下的所有组件，无需在 `idf_component.yml` 中显式声明。

构建系统会按以下顺序搜索组件：
1. 项目根目录下的 `components/`（本目录）
2. `main/` 目录
3. ESP-IDF 的 `components/`
4. `managed_components/`（通过组件管理器下载）

### 在代码中使用

在 `main/CMakeLists.txt` 中添加依赖：

```cmake
idf_component_register(
    SRCS 
        # ... 你的源文件
    PRIV_REQUIRES 
        # ... 其他依赖
        esp-tflite-micro  # 添加 TFLite Micro
        esp-nn            # 添加 ESP-NN（可选，TFLite 会自动使用）
    INCLUDE_DIRS ""
)
```

在代码中引入头文件：

```cpp
// TensorFlow Lite Micro
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

// ESP-NN（通常不需要直接使用，TFLite 会自动调用）
#include "esp_nn.h"
```

## 组件更新

如果需要更新组件版本：

### 方式 1: 手动替换
```bash
cd /path/to/xiaozhi-esp32/components
rm -rf esp-tflite-micro
cp -r /path/to/new/esp-tflite-micro ./esp-tflite-micro
```

### 方式 2: 使用 Git 子模块（推荐）

如果想要更方便地更新，可以考虑将组件转换为 Git 子模块：

```bash
cd /path/to/xiaozhi-esp32
rm -rf components/esp-tflite-micro
git submodule add https://github.com/espressif/tflite-micro-esp-examples.git components/esp-tflite-micro
git submodule update --init --recursive
```

## 组件依赖关系

```
xiaozhi-esp32
└── components/
    ├── esp-tflite-micro
    │   └── 依赖: esp-nn, idf (>=4.4)
    └── esp-nn
        └── 依赖: idf (>=4.2)
```

## 磁盘空间

- **esp-tflite-micro**: ~8.7 MB
- **esp-nn**: ~756 KB
- **总计**: ~9.5 MB

## 编译验证

验证组件是否正确加载：

```bash
cd /path/to/xiaozhi-esp32
idf.py reconfigure
```

查看输出中是否包含：
```
-- Component: esp-tflite-micro
-- Component: esp-nn
```

查看组件依赖树：
```bash
idf.py dependencies-tree
```

应该看到：
```
xiaozhi
└── main
    ├── esp-tflite-micro (local: components/esp-tflite-micro)
    │   └── esp-nn (local: components/esp-nn)
    └── ... (其他依赖)
```

## 许可证信息

- **esp-tflite-micro**: Apache License 2.0
- **esp-nn**: Apache License 2.0
- **TensorFlow**: Apache License 2.0

详见各组件目录下的 `LICENSE` 文件。

## 清理策略

以下文件/目录已被清理以减小体积：
- `esp-tflite-micro/build/` - 构建产物
- `esp-tflite-micro/examples/` - 示例程序
- `esp-nn/test_app/` - 测试应用
- `esp-nn/tests/` - 单元测试

如需这些文件，请参考原始仓库：
- https://github.com/espressif/tflite-micro-esp-examples
- https://github.com/espressif/esp-nn

## 故障排除

### 问题 1: 找不到组件

**症状**: 编译时提示找不到 `esp-tflite-micro` 或 `esp-nn`

**解决方案**:
```bash
# 检查组件目录是否存在
ls components/esp-tflite-micro
ls components/esp-nn

# 检查组件配置文件
cat components/esp-tflite-micro/idf_component.yml
cat components/esp-nn/idf_component.yml

# 重新配置项目
idf.py fullclean
idf.py reconfigure
```

### 问题 2: 编译错误

**症状**: TensorFlow 相关的编译错误

**解决方案**:
```cmake
# 在 main/CMakeLists.txt 中添加编译选项
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-maybe-uninitialized
    -Wno-missing-field-initializers
    -Wno-error=sign-compare
    -Wno-error=double-promotion
    -Wno-type-limits
)
```

### 问题 3: 内存不足

**症状**: 运行时内存分配失败

**解决方案**:
- 使用 PSRAM 分配 tensor arena
- 减小模型大小
- 使用 INT8 量化模型

参见 [../docs/TensorFlow_Lite_Micro_Integration.md](../docs/TensorFlow_Lite_Micro_Integration.md) 获取详细的内存优化指南。

---

**创建时间**: 2025-10-27  
**维护**: 项目团队  
**更新频率**: 根据需要手动更新

