# Luma::Audio — decode, waveform peaks, and one-shot preview.

luma_require_miniaudio()

add_library(LumaAudio STATIC
    src/AudioDecode.cpp
    src/AudioPreview.cpp)

add_library(Luma::Audio ALIAS LumaAudio)

target_include_directories(LumaAudio
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(LumaAudio
    PUBLIC Luma::Core
    PRIVATE luma_miniaudio)

set_target_properties(LumaAudio PROPERTIES FOLDER "Engine")
