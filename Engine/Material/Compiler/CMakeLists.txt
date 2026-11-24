# Luma::MaterialCompiler - material graph -> typed code chunks -> GLSL, with
# constant folding, uniform expressions (preshaders), static-switch
# permutations, errors and stats. Produces a backend-neutral CompiledMaterial;
# knows nothing about Vulkan.

add_library(LumaMaterialCompiler ${LUMA_LIBRARY_TYPE}
    src/CodeChunk.cpp
    src/Translator.cpp
    src/PassAssembly.cpp
    src/Preshader.cpp
    src/ExpressionCompilers.cpp
    src/Expressions/Collections.cpp
    src/Expressions/Common.cpp
    src/Expressions/Functions.cpp
    src/Expressions/ConstantsParameters.cpp
    src/Expressions/Math.cpp
    src/Expressions/TextureCoordinates.cpp
    src/Expressions/Utility.cpp)

add_library(Luma::MaterialCompiler ALIAS LumaMaterialCompiler)

target_include_directories(LumaMaterialCompiler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(LumaMaterialCompiler PUBLIC Luma::Core Luma::Math Luma::MaterialGraph Luma::Material)

set_target_properties(LumaMaterialCompiler PROPERTIES FOLDER "Engine/Material")
