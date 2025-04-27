# ALSA 配置说明

## 概述
本项目支持灵活的ALSA库配置，可以通过环境变量来自定义ALSA库的路径。

## 配置方式

### 方式1：使用环境变量（推荐）
设置 `BUILDROOT_OUTPUT_DIR` 环境变量来指定buildroot输出目录：

```bash
export BUILDROOT_OUTPUT_DIR="/path/to/your/buildroot/output"
```

### 方式2：使用默认路径
如果不设置环境变量，将使用默认路径：
```
/home/share/samba/risc-v/sg200x/LicheeRV-Nano-Build/buildroot/output
```

## 使用示例

### 交叉编译
```bash
# 设置buildroot输出目录
export BUILDROOT_OUTPUT_DIR="/your/custom/buildroot/output"

# 使用工具链编译
cmake -DCMAKE_TOOLCHAIN_FILE=../../boards/sg200x/toolchain_sg200x_rv64_musl.cmake ..
make
```

### 本地编译（Ubuntu）
```bash
# 本地编译会自动使用系统的ALSA库
cmake ..
make
```

## 路径结构
ALSA库文件的预期路径结构：
```
${BUILDROOT_OUTPUT_DIR}/
├── per-package/
│   └── alsa-lib/
│       └── target/
│           └── usr/
│               ├── lib/
│               │   └── libasound.so
│               └── include/
│                   └── alsa/
│                       └── *.h
```

## 验证配置
编译时会显示ALSA库的路径信息：
```
-- Cross-compiling: Using custom ALSA paths
-- ALSA_LIBRARY: /your/path/per-package/alsa-lib/target/usr/lib/libasound.so
-- ALSA_INCLUDE_DIR: /your/path/per-package/alsa-lib/target/usr/include
``` 