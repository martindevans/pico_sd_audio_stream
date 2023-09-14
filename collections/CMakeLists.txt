add_library(collections INTERFACE)

target_sources(collections INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/circular_buffer.h
        ${CMAKE_CURRENT_LIST_DIR}/circular_buffer.c
)

target_include_directories(collections INTERFACE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(collections INTERFACE pico_base_headers)
