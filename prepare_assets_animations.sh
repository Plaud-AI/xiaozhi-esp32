#!/bin/bash

# 准备 Lottie 动画文件到 assets 格式

set -e

echo "=== 准备动画文件到 Assets 格式 ==="
echo ""

# 创建 assets_source 目录（如果不存在）
mkdir -p assets_source/anim

# 复制动画文件并移除 .json 后缀（符合 assets 命名规范）
echo "📁 处理动画文件..."

for json_file in /Users/xionghao/Downloads/emojo/*.json; do
    if [ -f "$json_file" ]; then
        filename=$(basename "$json_file" .json)
        
        # 映射中文名到英文名
        case "$filename" in
            "开心")
                cp "$json_file" "assets_source/anim/happy"
                echo "✅ $filename -> happy"
                ;;
            "悲伤")
                cp "$json_file" "assets_source/anim/sad"
                echo "✅ $filename -> sad"
                ;;
            "小骄傲")
                cp "$json_file" "assets_source/anim/excited"
                echo "✅ $filename -> excited"
                ;;
            "满足")
                cp "$json_file" "assets_source/anim/calm"
                echo "✅ $filename -> calm"
                ;;
            "困倦")
                cp "$json_file" "assets_source/anim/sleepy"
                echo "✅ $filename -> sleepy"
                ;;
            "好奇")
                cp "$json_file" "assets_source/anim/surprised"
                echo "✅ $filename -> surprised"
                ;;
            "听音乐")
                cp "$json_file" "assets_source/anim/listening"
                echo "✅ $filename -> listening"
                ;;
            "5-唱歌")
                cp "$json_file" "assets_source/anim/singing"
                echo "✅ $filename -> singing"
                ;;
            "3-不屑")
                cp "$json_file" "assets_source/anim/disdain"
                echo "✅ $filename -> disdain"
                ;;
            "1-吐")
                cp "$json_file" "assets_source/anim/disgust"
                echo "✅ $filename -> disgust"
                ;;
            "4-NBA冠军戒指")
                cp "$json_file" "assets_source/anim/champion"
                echo "✅ $filename -> champion"
                ;;
            *)
                echo "⚠️  跳过: $filename"
                ;;
        esac
    fi
done

echo ""
echo "📊 动画文件列表："
ls -lh assets_source/anim/ 2>/dev/null | tail -n +2 | awk '{print "   ", $9, "(" $5 ")"}'

echo ""
echo "✅ 准备完成！"
echo ""
echo "📋 Assets 分区使用说明："
echo ""
echo "你的项目使用 Memory-Mapped 方式访问 assets 分区："
echo "  - Assets::GetAssetData(\"anim/happy\", ptr, size)"
echo "  - 返回内存指针，零拷贝，性能最高"
echo "  - 使用 lv_thorvg_set_src_data(obj, ptr, size) 加载"
echo ""
echo "优势："
echo "  ✅ 不需要文件系统（无冲突）"
echo "  ✅ 零拷贝，性能最佳"
echo "  ✅ 沿用项目原有架构"
echo ""


