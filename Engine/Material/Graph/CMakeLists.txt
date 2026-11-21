# Luma::MaterialGraph - node-graph data model of materials and the catalogue
# of material expressions (pins, properties, categories). Pure data: no
# compiler, no renderer.

add_library(LumaMaterialGraph STATIC
    src/Graph.cpp
    src/ExpressionRegistry.cpp
    src/Expressions/ConstantsParameters.cpp
    src/Expressions/Math.cpp
    src/Expressions/TextureCoordinates.cpp
    src/Expressions/UtilityFunctionsOutput.cpp)

add_library(Luma::MaterialGraph ALIAS LumaMaterialGraph)

target_include_directories(LumaMaterialGraph PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(LumaMaterialGraph PUBLIC Luma::Core Luma::Math)

set_target_properties(LumaMaterialGraph PROPERTIES FOLDER "Engine/Material")
