# Luma::PanelMaterialEditor - the Material Editor: one dockable tab per open
# Material / Material Instance (live preview + details), on top of
# MaterialEditModel (the library-backed editing operations).

add_library(LumaPanelMaterialEditor STATIC
    src/MaterialEditModel.cpp
    src/MaterialEditor.cpp)

add_library(Luma::PanelMaterialEditor ALIAS LumaPanelMaterialEditor)

target_include_directories(LumaPanelMaterialEditor
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(LumaPanelMaterialEditor
    PUBLIC Luma::Core Luma::Scene Luma::Slate Luma::Material Luma::Asset)

set_target_properties(LumaPanelMaterialEditor PROPERTIES FOLDER "Editor/Panels")
