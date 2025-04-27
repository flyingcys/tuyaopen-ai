# tuyaopen-embedded-core

[English](./README.md) | 简体中文

## 概述
本项目是从 [https://github.com/tuya/tuyaopen](https://github.com/tuya/tuyaopen) fork 而来，将连接 tuya 云相关核心组件重新组织，可快速嵌入至各种嵌入式平台使用。


使用 tuyaopen 可通过 WiFi、有线以太网、CAT-1、4G 等多种方式接入涂鸦云，实现设备远程控制、OTA 等功能。

### 已支持 tuyaopen 核心功能包括：
- [ ] 配网
    - [ ] WiFi AP 配网
    - [ ] 蓝牙配网
- [x] 设备激活
- [x] 扫码绑定
- [x] 远程控制
- [x] 局域网控制
- [x] OTA


### 已支持的嵌入式平台包括：
- [x] Ubuntu/Debian
- [x] Linux buildroot

### 待支持的嵌入式平台包括：
- [ ] esp32
- [ ] bl616/bl618
- [ ] RT-Thread
- [ ] zephyr



## 下载
```shell
$ git clone https://github.com/ai-embedded/tuyaopen-embedded-core
$ git submodule update --init --recursive 
```

本项目采用 submodule 管理子仓库，执行时请使用 `--recursive` 参数。


## 示例

`switch_demo` 演示一个简单的，跨平台、跨系统、支持多种连接的开关示例，通过涂鸦 APP、涂鸦云服务，可以对这个开关进行远程控制。

tuya 云接入依赖 PID/UUID/Authkey，需要根据 tuya 云平台申请。


1. 创建产品并获取产品的 PID：

参考文档 [https://developer.tuya.com/cn/docs/iot-device-dev/application-creation?id=Kbxw7ket3aujc](https://developer.tuya.com/cn/docs/iot-device-dev/application-creation?id=Kbxw7ket3aujc) 在 [https://iot.tuya.com](https://iot.tuya.com) 下创建产品，并获取到创建产品的 PID 。

该 PID 对应 `TUYA_PRODUCT_ID`，采用环境变量形式加入编译。
```shell
$ export TUYA_PRODUCT_ID=qhivvyqawogv04e4
```

2. `TUYA_OPENSDK_UUID` 与 `TUYA_OPENSDK_AUTHKEY`

与 [https://github.com/tuya/tuyaopen](https://github.com/tuya/tuyaopen) 一样采用 TuyaOpen 专用授权码，使用其他授权码无法正常连接涂鸦云。


请根据 [https://github.com/tuya/tuyaopen/blob/master/apps/tuya_cloud/README_zh.md](https://github.com/tuya/tuyaopen/blob/master/apps/tuya_cloud/README_zh.md) 介绍方式获取 `TUYA_OPENSDK_UUID` 与 `TUYA_OPENSDK_AUTHKEY`。

`TUYA_OPENSDK_UUID` 与 `TUYA_OPENSDK_AUTHKEY` 采用环境变量形式加入编译：
```shell
$ export TUYA_OPENSDK_UUID=uuidxxxxxxxxxxxxxxxx
$ export TUYA_OPENSDK_AUTHKEY=keyxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
```

> 请使用正确的 PID/UUID/Authkey，否则无法正常连接涂鸦云。

## 编译与运行

### 依赖安装
1. Ubuntu/Debian

```shell
$ sudo apt-get install -y cmake ninja-build
```

2. MacOS
```shell
$ brew install cmake ninja
```

### Ubuntu/Debian
1. 编译
```shell
$ cd demos/switch_demo
$ cmake -S . -B build -G "Ninja"
$ ninja -C build
```

2.  运行
```shell
$ cd demos/switch_demo
$ ./build/switch_demo
```

示例采用二维码扫码激活方式，首次运行时设备未激活，会自动弹出二维码，请使用涂鸦 APP 扫码激活设备。

### Linux buildroot 交叉编译

```shell
$ cd demos/switch_demo
$ cmake -DCMAKE_TOOLCHAIN_FILE="../../boards/sg200x/toolchain_sg200x_rv64_musl.cmake" -S . -B build -G "Ninja"
$ ninja -C build
```

- 开启 asan
cmake -DENABLE_ASAN=ON ..

目前已运行过的平台包括：

- [x] 算能 SG200X RV64： 
    - [Milk-V Duo/Duo256/Duos](https://milkv.io/zh/docs/duo/overview)
    - [LicheeRV Nano](https://wiki.sipeed.com/hardware/zh/lichee/RV_Nano/1_intro.html)
