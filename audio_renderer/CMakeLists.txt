add_library(audio_renderer INTERFACE)

target_sources(audio_renderer INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/audio_renderer.h
        ${CMAKE_CURRENT_LIST_DIR}/audio_renderer.c
)

target_include_directories(audio_renderer INTERFACE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(audio_renderer INTERFACE pico_base_headers hardware_interp)
