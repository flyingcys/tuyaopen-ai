# 音频录音功能说明

## 功能描述

本代码实现了Ubuntu系统下的实时麦克风音频采集功能，支持：

- **音频格式**: 16bit, 16kHz, 单声道PCM
- **采集间隔**: 每100ms上传一次buffer (1600帧)
- **交互控制**: 键盘命令控制开始/停止录音

## 文件结构

```
demos/ai_demo/
├── audio/
│   ├── alsa.c          # ALSA音频设备操作
│   └── alsa.h          # ALSA接口头文件
├── main.c              # 主程序（已集成录音功能）
├── test_record.c       # 独立测试程序
├── Makefile.test       # 测试程序编译文件
└── README_RECORD.md    # 本说明文档
```

## 快速测试

### 1. 编译测试程序

```bash
cd demos/ai_demo
gcc -o test_record test_record.c audio/alsa.c -lasound -lpthread
```

### 2. 运行测试

```bash
./test_record
```

### 3. 操作命令

- **s**: 开始录音
- **p**: 停止录音  
- **q**: 退出程序

## 主程序集成

主程序 `main.c` 已经集成了录音功能，编译方法：

```bash
# 设置环境变量
export TUYA_PRODUCT_ID="your_product_id"
export TUYA_OPENSDK_UUID="your_uuid"
export TUYA_OPENSDK_AUTHKEY="your_authkey"

# 使用CMake编译
mkdir build && cd build
cmake ..
make
```

## 技术细节

### 音频参数
- **采样率**: 16000 Hz
- **位深度**: 16 bit
- **声道数**: 1 (单声道)
- **缓冲区**: 1600帧 (100ms)

### ALSA配置
- **录音设备**: "default"
- **周期大小**: 1600帧
- **缓冲区倍数**: 4倍

### 线程架构
- **主线程**: 键盘输入检测和程序控制
- **录音线程**: 音频数据采集和上传

## 自定义上传逻辑

在 `upload_audio_data()` 函数中可以实现自定义的音频数据处理：

```c
static void upload_audio_data(const int16_t *data, size_t frames)
{
    // 1. 音频数据编码 (如base64)
    // 2. 网络传输 (HTTP/WebSocket)
    // 3. 云端API调用 (语音识别等)
    // 4. 本地存储
}
```

## 故障排除

### 1. 权限问题
```bash
# 添加用户到audio组
sudo usermod -a -G audio $USER
# 重新登录生效
```

### 2. 设备占用
```bash
# 检查音频设备状态
aplay -l
arecord -l

# 停止可能占用设备的进程
pulseaudio --kill
```

### 3. 依赖安装
```bash
# Ubuntu/Debian
sudo apt-get install libasound2-dev

# CentOS/RHEL
sudo yum install alsa-lib-devel
```

## 性能监控

录音过程中会打印实时信息：
- 上传计数和时间戳
- 音频帧数和时长
- 样本数据预览
- 错误和异常状态

示例输出：
```
[Upload #1] 1600 frames (100.0ms) - samples: -23 156 -89 234 -45 ...
[Upload #2] 1600 frames (100.0ms) - samples: 67 -123 89 -156 78 ...
```

## 扩展功能

可以基于此框架实现：
- 实时语音识别
- 音频流传输
- 语音活动检测(VAD)
- 噪声抑制
- 回声消除 