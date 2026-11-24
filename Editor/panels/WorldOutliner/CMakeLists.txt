add_library(LumaPanelWorldOutliner ${LUMA_LIBRARY_TYPE}
    src/WorldOutliner.cpp)

add_library(Luma::PanelWorldOutliner ALIAS LumaPanelWorldOutliner)

target_include_directories(LumaPanelWorldOutliner
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../..)

target_link_libraries(LumaPanelWorldOutliner
    PUBLIC Luma::Core Luma::Scene Luma::Slate Luma::EditorFramework)

set_target_properties(LumaPanelWorldOutliner PROPERTIES FOLDER "Editor/Panels")
