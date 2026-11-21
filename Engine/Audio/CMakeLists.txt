# Luma::Audio — decode and one-shot preview. The public API arrives with the
# sound translator; this target exists so miniaudio is built with the engine.

luma_require_miniaudio()

add_library(LumaAudio STATIC
    src/AudioModule.cpp)

add_library(Luma::Audio ALIAS LumaAudio)

target_link_libraries(LumaAudio
    PUBLIC Luma::Core
    PRIVATE luma_miniaudio)

set_target_properties(LumaAudio PROPERTIES FOLDER "Engine")
