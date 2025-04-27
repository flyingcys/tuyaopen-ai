get_filename_component(COMPONENT_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)

file(GLOB SRCS
    "linux/*.c"
    "include/utilities/src/*.c"
)

set(INCS 
    "include/utilities/include"
    "include/system"
    "include/flash"
    "include/common"
    "include/rtc"
    "include/wired"
    "include/security"
    "include/wired"
    "include/uart"
    "include/media"
)

add_library(${COMPONENT_NAME} 
    STATIC
    ${SRCS}
)

target_include_directories(${COMPONENT_NAME}
    PUBLIC
    ${INCS}
)
