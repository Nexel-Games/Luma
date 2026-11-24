add_library(LumaEditorPlugins ${LUMA_LIBRARY_TYPE}
    src/PluginsWindow.cpp)

add_library(Luma::EditorPlugins ALIAS LumaEditorPlugins)

target_include_directories(LumaEditorPlugins
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(LumaEditorPlugins
    PUBLIC Luma::Core Luma::Slate Luma::Plugins Luma::Project Luma::Scene Luma::EditorFramework
           Luma::Platform)

set_target_properties(LumaEditorPlugins PROPERTIES FOLDER "Editor")
