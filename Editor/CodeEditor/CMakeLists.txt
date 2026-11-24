# Luma::CodeEditor - the in-engine code editor window (C++, Lua, Python).

add_library(LumaCodeEditor ${LUMA_LIBRARY_TYPE}
    src/CodeLanguage.cpp
    src/CodeDocument.cpp
    src/CodeEditorWindow.cpp)

add_library(Luma::CodeEditor ALIAS LumaCodeEditor)

target_include_directories(LumaCodeEditor
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(LumaCodeEditor
    PUBLIC Luma::Core Luma::Slate Luma::EditorFramework Luma::Platform)

set_target_properties(LumaCodeEditor PROPERTIES FOLDER "Editor")
