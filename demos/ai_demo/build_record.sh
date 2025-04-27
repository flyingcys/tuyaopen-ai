#!/bin/bash

# 设置必要的环境变量（示例值）
export TUYA_PRODUCT_ID="dummy_product_id"
export TUYA_OPENSDK_UUID="dummy_uuid"
export TUYA_OPENSDK_AUTHKEY="dummy_authkey"

# 创建构建目录
mkdir -p build
cd build

# 配置CMake
cmake ..

# 编译
make -j$(nproc)

echo "Build completed. Run with: ./ai_demo"
echo ""
echo "Audio recording controls:"
echo "  s - Start recording (16bit, 16kHz, mono)"
echo "  p - Stop recording"
echo "  q - Quit" 