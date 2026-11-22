# Luma::GraphEditor - a generic node graph editor (Unreal's GraphEditor
# module): canvas, nodes, typed pins, bezier wires, selection, undo,
# clipboard and a searchable action menu, driven by an abstract IGraphModel.
# Knows nothing about materials; the material graph is one adapter.

add_library(LumaGraphEditor STATIC
    src/GraphMath.cpp
    src/Selection.cpp
    src/GraphController.cpp)

add_library(Luma::GraphEditor ALIAS LumaGraphEditor)

target_include_directories(LumaGraphEditor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(LumaGraphEditor PUBLIC Luma::Core Luma::Slate)

set_target_properties(LumaGraphEditor PROPERTIES FOLDER "Editor")
