cmake_minimum_required(VERSION 3.10)

project(switch_demo)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ASAN (AddressSanitizer) option
option(ENABLE_ASAN "Enable AddressSanitizer for memory debugging" OFF)

if(ENABLE_ASAN)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fno-omit-frame-pointer -g")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer -g")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=address")
    message(STATUS "AddressSanitizer enabled")
endif()

set(CONFIG_ENABLE_WIRED "Enable wired" ON)

if(NOT DEFINED ENV{TUYA_PRODUCT_ID})
    message(FATAL_ERROR "Env variables TUYA_PRODUCT_ID must be set")
else()
    add_compile_definitions(TUYA_PRODUCT_ID="$ENV{TUYA_PRODUCT_ID}")
endif()

if(NOT DEFINED ENV{TUYA_OPENSDK_UUID})
    message(FATAL_ERROR "Env variables TUYA_OPENSDK_UUID must be set")
else()
    add_compile_definitions(TUYA_OPENSDK_UUID="$ENV{TUYA_OPENSDK_UUID}")
endif()

if(NOT DEFINED ENV{TUYA_OPENSDK_AUTHKEY})
    message(FATAL_ERROR "Env variables TUYA_OPENSDK_AUTHKEY must be set")
else()
    add_compile_definitions(TUYA_OPENSDK_AUTHKEY="$ENV{TUYA_OPENSDK_AUTHKEY}")
endif()

set(TUYAOPEN_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(SRCS 
    "main.c"
    "qrencode_print.c"
)

list(APPEND SRCS 
    "libqrencode/qrencode.c"
    "libqrencode/qrinput.c"
    "libqrencode/bitstream.c"
    "libqrencode/qrspec.c"
    "libqrencode/rsecc.c"
    "libqrencode/split.c"
    "libqrencode/mask.c"
    "libqrencode/mqrspec.c"
    "libqrencode/mmask.c"
)

add_executable(${PROJECT_NAME} 
    ${SRCS}
)

target_include_directories(${PROJECT_NAME} PUBLIC . "libqrencode")

add_definitions(-DSTATIC_IN_RELEASE=static)
add_definitions(-DMAJOR_VERSION=4 -DMINOR_VERSION=1 -DMICRO_VERSION=1 -DVERSION=\"4.1.1\")

set(COMPONENT_LIBS "")

add_subdirectory(${TUYAOPEN_ROOT_DIR}/components ${CMAKE_CURRENT_BINARY_DIR}/components_build)
add_subdirectory(${TUYAOPEN_ROOT_DIR}/port ${CMAKE_CURRENT_BINARY_DIR}/port_build)

foreach(COMPONENT IN LISTS COMPONENT_LIBS)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${COMPONENT})
endforeach()
