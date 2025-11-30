#!/bin/bash

# 构建包含 Lottie 动画的 assets 并烧录的完整脚本

set -e

echo "=== 🎨 Xiaozhi ESP32 - 构建和烧录（包含 Lottie 动画）==="
echo ""

# 项目根目录
PROJECT_DIR="/Users/xionghao/Documents/GitHub/xiaozhi-esp32"
cd "$PROJECT_DIR"

# ============================================================================
# 步骤 1：检查动画文件
# ============================================================================
echo "📁 步骤 1/4：检查 Lottie 动画文件..."
if [ ! -d "assets_source/anim" ]; then
    echo "❌ 错误：未找到 assets_source/anim/ 目录"
    echo "请先运行: ./prepare_assets_animations.sh"
    exit 1
fi

ANIM_COUNT=$(find assets_source/anim -type f ! -name "*.json" | wc -l)
if [ "$ANIM_COUNT" -eq 0 ]; then
    echo "❌ 错误：没有找到动画文件（无后缀）"
    echo "请先运行: ./prepare_assets_animations.sh"
    exit 1
fi

echo "✅ 找到 $ANIM_COUNT 个动画文件"
ls -lh assets_source/anim/ | grep -v ".json" | tail -n +2 | awk '{print "   -", $9, "(" $5 ")"}'
echo ""

# ============================================================================
# 步骤 2：构建 assets.bin（包含动画）
# ============================================================================
echo "🔨 步骤 2/4：构建 assets.bin（包含 Lottie 动画）..."

SDKCONFIG="sdkconfig"
OUTPUT_DIR="build"
ASSETS_BIN="$OUTPUT_DIR/assets.bin"

mkdir -p "$OUTPUT_DIR"

# 调用项目的 assets 构建脚本
python3 scripts/build_default_assets.py \
    --sdkconfig "$SDKCONFIG" \
    --output "$ASSETS_BIN" \
    --extra_files "assets_source"

if [ $? -ne 0 ]; then
    echo "❌ 错误：构建 assets.bin 失败"
    exit 1
fi

if [ ! -f "$ASSETS_BIN" ]; then
    echo "❌ 错误：未生成 assets.bin"
    exit 1
fi

echo "✅ Assets 构建成功: $ASSETS_BIN"
ls -lh "$ASSETS_BIN"
echo ""

# ============================================================================
# 步骤 3：编译固件
# ============================================================================
echo "🔧 步骤 3/4：编译固件..."
idf.py build

if [ $? -ne 0 ]; then
    echo "❌ 错误：固件编译失败"
    exit 1
fi

echo "✅ 固件编译成功"
echo ""

# ============================================================================
# 步骤 4：烧录（固件 + assets）
# ============================================================================
echo "🚀 步骤 4/4：烧录固件和 assets..."

# 检查串口
PORT="/dev/ttyUSB0"
if [ ! -e "$PORT" ]; then
    PORT="/dev/ttyACM0"
    if [ ! -e "$PORT" ]; then
        echo "⚠️  警告：未找到串口设备"
        if [ -n "$1" ]; then
            PORT="$1"
        else
            echo "使用方法: $0 [串口路径]"
            echo "可用串口："
            ls -l /dev/tty* 2>/dev/null | grep -E "USB|ACM" || echo "  (未找到)"
            exit 1
        fi
    fi
fi

echo "🔌 使用串口: $PORT"
echo ""

# 读取 assets 分区地址
PARTITION_FILE="partitions/v2/16m.csv"
if [ ! -f "$PARTITION_FILE" ]; then
    echo "❌ 错误：分区表文件不存在: $PARTITION_FILE"
    exit 1
fi

ASSETS_OFFSET=$(grep "^assets," "$PARTITION_FILE" | awk -F',' '{print $4}' | tr -d ' ')
if [ -z "$ASSETS_OFFSET" ] || [ "$ASSETS_OFFSET" == "-" ]; then
    echo "⚠️  Assets 分区偏移为自动计算，使用默认值 0xa20000"
    ASSETS_OFFSET="0xa20000"
fi

echo "📍 Assets 分区偏移: $ASSETS_OFFSET"
echo ""

# 烧录固件
echo "烧录固件..."
idf.py flash -p "$PORT"

if [ $? -ne 0 ]; then
    echo "❌ 错误：固件烧录失败"
    exit 1
fi

echo "✅ 固件烧录成功"
echo ""

# 烧录 assets
echo "烧录 assets（包含动画）..."
python3 "$IDF_PATH/components/esptool_py/esptool/esptool.py" \
    --chip esp32s3 \
    --port "$PORT" \
    --baud 921600 \
    write_flash \
    $ASSETS_OFFSET \
    "$ASSETS_BIN"

if [ $? -ne 0 ]; then
    echo ""
    echo "❌ Assets 烧录失败！"
    echo ""
    echo "可能的原因："
    echo "  1. 串口被占用（关闭 idf.py monitor）"
    echo "  2. 串口路径错误"
    echo "  3. 设备未连接"
    exit 1
fi

echo ""
echo "🎉 烧录完成！"
echo ""
echo "=== 验证步骤 ==="
echo "1. 启动监视器: idf.py monitor -p $PORT"
echo ""
echo "2. 查看日志，应该看到："
echo "   I (xxx) Assets: The partition size is 5760 KB"
echo "   I (xxx) EmotionAssetsLoader: Loaded asset: anim/happy (16000 bytes)"
echo "   I (xxx) EmotionCoord: Emotion System initialized successfully"
echo ""
echo "3. 设备应该自动显示 Lottie 动画"
echo ""


