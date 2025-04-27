set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR riscv64)

set(CMAKE_CROSSCOMPILING TRUE)

set(CMAKE_C_COMPILER riscv64-unknown-linux-musl-gcc)
set(CMAKE_CXX_COMPILER riscv64-unknown-linux-musl-g++)
if(DEFINED ENV{BUILDROOT_OUTPUT_DIR})
    set(BUILDROOT_OUTPUT_DIR $ENV{BUILDROOT_OUTPUT_DIR})
else()
    set(BUILDROOT_OUTPUT_DIR "/home/share/samba/risc-v/sg200x/LicheeRV-Nano-Build/buildroot/output")
endif()

set(ALSA_LIBRARY ${BUILDROOT_OUTPUT_DIR}/per-package/alsa-lib/target/usr/lib/libasound.so)
set(ALSA_INCLUDE_DIR ${BUILDROOT_OUTPUT_DIR}/per-package/alsa-lib/target/usr/include)