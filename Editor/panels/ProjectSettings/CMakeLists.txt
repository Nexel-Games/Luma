add_library(LumaPanelProjectSettings ${LUMA_LIBRARY_TYPE}
    src/ProjectSettings.cpp
    src/SimulationPage.cpp
    src/ProjectSettingsStore.cpp)

add_library(Luma::PanelProjectSettings ALIAS LumaPanelProjectSettings)

target_include_directories(LumaPanelProjectSettings
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../..)

target_link_libraries(LumaPanelProjectSettings
    PUBLIC Luma::Core Luma::Math Luma::Scene Luma::Slate Luma::Serialization Luma::Project
           Luma::EditorFramework)

set_target_properties(LumaPanelProjectSettings PROPERTIES FOLDER "Editor/Panels")
