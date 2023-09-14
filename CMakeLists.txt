cmake_minimum_required(VERSION 3.13)

# initialize the SDK based on PICO_SDK_PATH
include(pico_sdk_import.cmake)
include(pico_extras_import.cmake)

project(sd_stream_audio)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

add_subdirectory(audio_renderer)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

add_executable(sd_stream_audio
    main.c
)

target_include_directories(sd_stream_audio PRIVATE ${CMAKE_CURRENT_LIST_DIR})

add_subdirectory(./serialization)

# Add pico_stdlib library which aggregates commonly used features
target_link_libraries(sd_stream_audio
    pico_stdlib
    pico_audio_i2s
    pico_multicore
    hardware_interp
    audio_renderer
    serialization
    pico_sd_card
)

target_compile_definitions(sd_stream_audio PRIVATE
    PICO_AUDIO_I2S_MONO_INPUT=1
)

# Send Serial output somewhere
pico_enable_stdio_usb(sd_stream_audio 1)
pico_enable_stdio_uart(sd_stream_audio 0)

pico_add_extra_outputs(sd_stream_audio)

