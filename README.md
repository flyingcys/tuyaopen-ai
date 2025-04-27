# tuyaopen-embedded-core

English | [简体中文](./README_zh.md)

## Introduction

This project is forked from [https://github.com/tuya/tuyaopen](https://github.com/tuya/tuyaopen), reorganizing the core components related to connecting to the Tuya cloud, making it easy to integrate into various embedded platforms.

Using tuyaopen, you can connect to the Tuya cloud via WiFi, wired Ethernet, CAT-1, 4G, and other methods, enabling features such as remote control and OTA updates.

### Supported Core Features of tuyaopen:
- [ ] Network connection
    - [ ] WiFi AP
    - [ ] Bluetooth
- [x] Device Activation
- [x] QR Code Binding
- [x] Remote Control
- [x] Local Network Control
- [x] OTA

### Supported Embedded Platforms:
- [x] Ubuntu/Debian
- [x] Linux buildroot

### Platforms to be Supported:
- [ ] esp32
- [ ] bl616/bl618
- [ ] RT-Thread
- [ ] zephyr

## Download
```shell
$ git clone https://github.com/ai-embedded/tuyaopen-embedded-core
$ git submodule update --init --recursive 
```
This project uses submodules to manage sub-repositories. Please use the `--recursive` parameter when executing the commands.

## Example

`switch_demo` demonstrates a simple cross-platform, cross-system switch example that supports multiple connection methods. Through the Tuya APP and Tuya cloud services, this switch can be remotely controlled.

Connecting to the Tuya cloud depends on PID/UUID/Authkey, which need to be obtained from the Tuya cloud platform.

1. Create a product and obtain its PID:

Refer to the documentation [https://developer.tuya.com/cn/docs/iot-device-dev/application-creation?id=Kbxw7ket3aujc](https://developer.tuya.com/cn/docs/iot-device-dev/application-creation?id=Kbxw7ket3aujc) on [https://iot.tuya.com](https://iot.tuya.com) to create a product and get the PID for your product.

This PID corresponds to `TUYA_PRODUCT_ID` and is added to the compilation using an environment variable.

```shell
$ export TUYA_PRODUCT_ID=qhivvyqawogv04e4
```

2. `TUYA_OPENSDK_UUID` and `TUYA_OPENSDK_AUTHKEY`

Similar to [https://github.com/tuya/tuyaopen](https://github.com/tuya/tuyaopen), it uses a dedicated authorization code for TuyaOpen. Using other authorization codes will not allow normal connection to the Tuya cloud.

Please refer to [https://github.com/tuya/tuyaopen/blob/master/apps/tuya_cloud/README_zh.md](https://github.com/tuya/tuyaopen/blob/master/apps/tuya_cloud/README_zh.md) for instructions on obtaining `TUYA_OPENSDK_UUID` and `TUYA_OPENSDK_AUTHKEY`.

Currently, `TUYA_OPENSDK_UUID` and `TUYA_OPENSDK_AUTHKEY` are added to the compilation using environment variables:
```shell
$ export TUYA_OPENSDK_UUID=uuidxxxxxxxxxxxxxxxx
$ export TUYA_OPENSDK_AUTHKEY=keyxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
```

> Please use the correct PID/UUID/Authkey; otherwise, it will not connect to the Tuya cloud properly.

## Compilation and Execution

### Dependency Installation
1. Ubuntu/Debian
```shell
$ sudo apt-get install -y cmake ninja-build
```

2. MacOS
```shell
$ brew install cmake ninja
```

### Ubuntu/Debian
1. Compilation
```shell
$ cd demos/switch_demo
$ cmake -S . -B build -G "Ninja"
$ ninja -C build
```

2. Execution
```shell
$ cd demos/switch_demo
$ ./build/switch_demo
```

The example uses a QR code scanning activation method. The first time you run it, the device is not activated, so it will automatically pop up a QR code. Please scan the QR code with the Tuya APP to activate the device.

### Cross-Compilation for Linux buildroot

```shell
$ cd demos/switch_demo
$ cmake -DCMAKE_TOOLCHAIN_FILE="../../boards/sg200x/toolchain_sg200x_rv64_musl.cmake" -S . -B build -G "Ninja"
$ ninja -C build
```

The platforms on which this has been successfully run include:

- [x] Sophgo SG200X RV64:
    - [Milk-V Duo/Duo256/Duos](https://milkv.io/en/docs/duo/overview)
    - [LicheeRV Nano](https://wiki.sipeed.com/hardware/en/lichee/RV_Nano/1_intro.html)
