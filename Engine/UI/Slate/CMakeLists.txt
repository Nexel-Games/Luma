# Luma::Slate - our own immediate-mode UI framework (drawn via the RHI).
# Uses stb_truetype (vendored) only for glyph rasterization.

add_library(LumaSlate ${LUMA_LIBRARY_TYPE}
    src/Font.cpp
    src/Image.cpp
    src/DrawList.cpp
    src/Theme.cpp
    src/Icons.cpp
    src/Context.cpp
    src/DragDrop.cpp
    src/Menu.cpp
    src/DockSpace.cpp
    src/TextEditState.cpp
    src/NumericText.cpp
    src/ColorPicker.cpp
    src/ColorPickerPopup.cpp)

add_library(Luma::Slate ALIAS LumaSlate)

target_include_directories(LumaSlate
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${PROJECT_SOURCE_DIR}/ThirdParty/stb)

target_link_libraries(LumaSlate PUBLIC Luma::Core Luma::RHI Luma::Platform Luma::Serialization)

set_target_properties(LumaSlate PROPERTIES FOLDER "Engine/UI")
