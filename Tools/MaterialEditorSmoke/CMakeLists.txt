# MaterialEditorSmoke - opens the Material Instance Editor and the node-graph
# Material Editor on a generated material in a real window and captures each
# to a PNG (visual smoke test; no project needed).
#   MaterialEditorSmoke <instance.png> <graph.png>

add_executable(MaterialEditorSmoke main.cpp)
target_link_libraries(MaterialEditorSmoke PRIVATE
    Luma::Core Luma::Platform Luma::RenderVulkan Luma::Slate Luma::Asset Luma::Material
    Luma::MaterialEditors Luma::GraphEditor)
target_compile_definitions(MaterialEditorSmoke PRIVATE LUMA_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
set_target_properties(MaterialEditorSmoke PROPERTIES FOLDER "Tools")
