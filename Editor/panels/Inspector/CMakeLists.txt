add_library(LumaPanelInspector ${LUMA_LIBRARY_TYPE}
    src/AssetInspector.cpp
    src/Inspector.cpp
    src/MaterialSlots.cpp
    src/SlotParameters.cpp
    src/StaticMeshSlot.cpp)

add_library(Luma::PanelInspector ALIAS LumaPanelInspector)

target_include_directories(LumaPanelInspector
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../..)

target_link_libraries(LumaPanelInspector
    PUBLIC Luma::Core Luma::Scene Luma::Slate Luma::Material Luma::Asset Luma::EditorFramework Luma::MaterialEditors Luma::AssetImport Luma::Audio)

set_target_properties(LumaPanelInspector PROPERTIES FOLDER "Editor/Panels")
