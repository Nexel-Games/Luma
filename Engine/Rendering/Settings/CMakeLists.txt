# Luma::GraphicsSettings - per-project display / frame pacing and rendering
# quality settings (Config/Graphics.json), shared by the editor and runtime.

add_library(LumaGraphicsSettings STATIC
    src/GraphicsSettings.cpp)

add_library(Luma::GraphicsSettings ALIAS LumaGraphicsSettings)

target_include_directories(LumaGraphicsSettings
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(LumaGraphicsSettings
    PUBLIC Luma::Core Luma::RHI Luma::Renderer Luma::Serialization)

set_target_properties(LumaGraphicsSettings PROPERTIES FOLDER "Engine/Rendering")
