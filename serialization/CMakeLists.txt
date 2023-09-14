add_library(serialization INTERFACE)

target_sources(serialization INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/bytereader.h
        ${CMAKE_CURRENT_LIST_DIR}/bytereader.c
)

target_include_directories(serialization INTERFACE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(serialization INTERFACE pico_base_headers hardware_interp)
