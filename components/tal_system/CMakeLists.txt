get_filename_component(COMPONENT_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)

file(GLOB_RECURSE 
    SRCS 
    "src/*.c"
    "utilities/src/*.c"
)

set(INCS 
    "include"
    "utilities/include"
    "common/include"
)

add_library(${COMPONENT_NAME} 
    STATIC
    ${SRCS}
)

target_include_directories(${COMPONENT_NAME}
    PUBLIC
    ${INCS}
)

target_link_libraries(${COMPONENT_NAME} PUBLIC port utilities tal_driver)
