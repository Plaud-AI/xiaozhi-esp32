#!/bin/bash

# 将 Lottie 动画打包并烧录到 assets 分区的脚本

set -e  # 遇到错误立即退出

echo "=== 🎨 Lottie 动画打包和烧录工具 ==="
echo ""

# 检查 IDF 环境
if [ -z "$IDF_PATH" ]; then
    echo "❌ 错误：IDF_PATH 未设置"
    echo "请先运行: . \$HOME/esp/esp-idf/export.sh"
    exit 1
fi

# 检查动画文件
if [ ! -d "assets_source/anim" ]; then
    echo "❌ 错误：assets_source/anim 目录不存在"
    exit 1
fi

echo "📁 检查动画文件..."
ANIM_COUNT=$(ls assets_source/anim/*.json 2>/dev/null | wc -l)
if [ "$ANIM_COUNT" -eq 0 ]; then
    echo "❌ 错误：没有找到动画文件 (.json)"
    exit 1
fi
echo "✅ 找到 $ANIM_COUNT 个动画文件"
ls -lh assets_source/anim/*.json | awk '{print "   -", $9, "(" $5 ")"}'
echo ""

# 从分区表获取 assets 分区信息
echo "📊 读取分区表..."
PARTITION_FILE="partitions/v2/16m.csv"
if [ ! -f "$PARTITION_FILE" ]; then
    echo "❌ 错误：分区表文件不存在: $PARTITION_FILE"
    exit 1
fi

# 读取 assets 分区大小
ASSETS_SIZE=$(grep "^assets," "$PARTITION_FILE" | awk -F',' '{print $5}' | tr -d ' ')
if [ -z "$ASSETS_SIZE" ]; then
    echo "❌ 错误：无法从分区表读取 assets 大小"
    exit 1
fi

# 转换大小（支持 K/M 后缀）
if [[ $ASSETS_SIZE == *"K" ]]; then
    SIZE_BYTES=$((${ASSETS_SIZE%K} * 1024))
elif [[ $ASSETS_SIZE == *"M" ]]; then
    SIZE_BYTES=$((${ASSETS_SIZE%M} * 1024 * 1024))
else
    SIZE_BYTES=$ASSETS_SIZE
fi

echo "✅ Assets 分区大小: $ASSETS_SIZE ($SIZE_BYTES bytes)"
echo ""

# 生成 SPIFFS 镜像
echo "🔨 生成 SPIFFS 镜像..."
SPIFFS_IMAGE="build/assets_spiffs.bin"

python3 "$IDF_PATH/components/spiffs/spiffsgen.py" \
    $SIZE_BYTES \
    assets_source \
    "$SPIFFS_IMAGE" \
    --page-size 256 \
    --obj-name-len 32 \
    --meta-len 4 \
    --use-magic \
    --use-magic-len

if [ $? -ne 0 ]; then
    echo "❌ 错误：生成 SPIFFS 镜像失败"
    exit 1
fi

echo "✅ SPIFFS 镜像生成成功: $SPIFFS_IMAGE"
ls -lh "$SPIFFS_IMAGE"
echo ""

# 读取 assets 分区地址
ASSETS_OFFSET=$(grep "^assets," "$PARTITION_FILE" | awk -F',' '{print $4}' | tr -d ' ')
if [ -z "$ASSETS_OFFSET" ] || [ "$ASSETS_OFFSET" == "-" ]; then
    # 如果是 "-"，需要计算
    echo "⚠️  Assets 分区偏移为自动计算，使用默认值 0xa20000"
    ASSETS_OFFSET="0xa20000"
fi

echo "📍 Assets 分区偏移: $ASSETS_OFFSET"
echo ""

# 检查串口
PORT="/dev/ttyUSB0"
if [ ! -e "$PORT" ]; then
    PORT="/dev/ttyACM0"
    if [ ! -e "$PORT" ]; then
        echo "⚠️  警告：未找到串口设备，请手动指定"
        echo "使用方法: $0 /dev/ttyUSB0"
        if [ -n "$1" ]; then
            PORT="$1"
        else
            echo ""
            echo "可用串口："
            ls -l /dev/tty* 2>/dev/null | grep -E "USB|ACM" || echo "  (未找到)"
            exit 1
        fi
    fi
fi

echo "🔌 使用串口: $PORT"
echo ""

# 烧录
echo "🚀 烧录 SPIFFS 到 assets 分区..."
echo "   地址: $ASSETS_OFFSET"
echo "   文件: $SPIFFS_IMAGE"
echo ""

python3 "$IDF_PATH/components/esptool_py/esptool/esptool.py" \
    --chip esp32s3 \
    --port "$PORT" \
    --baud 921600 \
    write_flash \
    $ASSETS_OFFSET \
    "$SPIFFS_IMAGE"

if [ $? -ne 0 ]; then
    echo ""
    echo "❌ 烧录失败！"
    echo ""
    echo "可能的原因："
    echo "  1. 串口被占用（关闭 idf.py monitor）"
    echo "  2. 串口路径错误"
    echo "  3. 设备未连接"
    exit 1
fi

echo ""
echo "🎉 烧录成功！"
echo ""
echo "=== 验证步骤 ==="
echo "1. 重启设备: idf.py monitor"
echo "2. 查看日志，应该看到："
echo "   I AssetsSPIFFS: SPIFFS: Total=... KB, Used=... KB"
echo "   I AssetsSPIFFS: ✅ Test file access successful: /assets/anim/happy.json"
echo "   I EmotionCoord: Emotion System initialized successfully"
echo ""
echo "3. 设备应该自动显示 Lottie 动画"
echo ""

