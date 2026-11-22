# Luma::MaterialEditors - the node-graph Material Editor and the Material
# Instance Editor (Unreal layouts), built on Luma::GraphEditor. Rendering of
# previews and compile status come in through MaterialEditorServices so the
# module does not depend on the editor executable.

add_library(LumaMaterialEditors STATIC
    src/InstanceParameters.cpp
    src/MaterialInstanceEditor.cpp)

add_library(Luma::MaterialEditors ALIAS LumaMaterialEditors)

target_include_directories(LumaMaterialEditors PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(LumaMaterialEditors
    PUBLIC Luma::Core Luma::Math Luma::Slate Luma::Material Luma::MaterialGraph Luma::MaterialCompiler)

set_target_properties(LumaMaterialEditors PROPERTIES FOLDER "Editor")
