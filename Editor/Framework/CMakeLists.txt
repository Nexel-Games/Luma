# Luma::EditorFramework - the editor's extension registries (Inspector drawers,
# menus, panels, Project Settings pages). Built-in editor features register
# through these exactly like plugins do.

add_library(LumaEditorFramework STATIC
    src/EditorIcons.cpp
    src/InspectorDrawers.cpp)

add_library(Luma::EditorFramework ALIAS LumaEditorFramework)

target_include_directories(LumaEditorFramework
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/Editor/panels)  # PanelContext.h

target_link_libraries(LumaEditorFramework
    PUBLIC Luma::Core Luma::Math Luma::Scene Luma::Serialization Luma::Slate)

set_target_properties(LumaEditorFramework PROPERTIES FOLDER "Editor")
