# MaterialEditorSmoke - opens the Material Instance Editor and the node-graph
# Material Editor on a generated material in a real window and captures each
# to a PNG (visual smoke test; no project needed).
#   MaterialEditorSmoke <instance.png> <graph.png>

# Renders previews through the editor's real MaterialPreview (HDRI backdrop +
# M_GridBox floor), compiled in from Editor/LumaEditor.
add_executable(MaterialEditorSmoke main.cpp ${CMAKE_SOURCE_DIR}/Editor/LumaEditor/MaterialPreview.cpp)
target_include_directories(MaterialEditorSmoke PRIVATE
    ${CMAKE_SOURCE_DIR}/Editor/LumaEditor
    ${CMAKE_SOURCE_DIR}/Engine/Rendering/Vulkan/include
    ${CMAKE_SOURCE_DIR}/Engine/Rendering/Vulkan/src
    ${CMAKE_SOURCE_DIR}/ThirdParty/stb)
target_link_libraries(MaterialEditorSmoke PRIVATE
    Luma::Core Luma::Platform Luma::RenderVulkan Luma::Slate Luma::Asset Luma::Material
    Luma::MaterialEditors Luma::GraphEditor Luma::MaterialBridge Luma::Scene Luma::Renderer Vulkan::Vulkan)
target_compile_definitions(MaterialEditorSmoke PRIVATE LUMA_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
set_target_properties(MaterialEditorSmoke PROPERTIES FOLDER "Tools")
