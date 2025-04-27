get_filename_component(COMPONENT_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)

set(SRCS 
    "tdl_audio/src/tdl_audio_manage.c"
)

set(INCS 
    "tdl_audio/include"
)

add_library(${COMPONENT_NAME} 
    STATIC
    ${SRCS}
)

target_include_directories(${COMPONENT_NAME}
    PUBLIC
    ${INCS}
)

target_link_libraries(${COMPONENT_NAME} PUBLIC tal_system)

