# Luma::AssetImport - Interchange-style importing: translators (source file ->
# ImportScene), pipelines (settings -> asset requests), factories (requests ->
# .lmesh/.lmat/.png/.wav + .meta), and the async ImportSession with reimport.
# Sits above Luma::Asset, Luma::Material and Luma::Audio; nothing below it
# depends on it.

luma_require_assimp()

add_library(LumaAssetImport STATIC
    src/ImportScene.cpp
    src/ImportSettings.cpp
    src/ImportMetadata.cpp
    src/TranslatorRegistry.cpp
    src/ImportSession.cpp
    src/SourceWatch.cpp
    src/Translators/AxisConversion.cpp
    src/Translators/ModelTranslator.cpp
    src/Translators/ImageTranslator.cpp
    src/Translators/AudioTranslator.cpp
    src/Pipelines/ImportPlan.cpp
    src/Pipelines/CommonPipeline.cpp
    src/Pipelines/MeshPipeline.cpp
    src/Pipelines/MaterialPipeline.cpp
    src/Pipelines/TexturePipeline.cpp
    src/Pipelines/AudioPipeline.cpp
    src/Factories/FactoryCommon.cpp
    src/Factories/MeshFactory.cpp
    src/Factories/MaterialFactory.cpp
    src/Factories/TextureFactory.cpp
    src/Factories/SoundFactory.cpp)

add_library(Luma::AssetImport ALIAS LumaAssetImport)

target_include_directories(LumaAssetImport
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_SOURCE_DIR}/ThirdParty/stb)

target_link_libraries(LumaAssetImport
    PUBLIC Luma::Core Luma::Math Luma::Serialization Luma::Asset Luma::Material Luma::Audio
           Luma::Jobs Luma::Tasks
    PRIVATE assimp)

target_compile_options(LumaAssetImport PRIVATE /wd4100 /wd4189)

set_target_properties(LumaAssetImport PROPERTIES FOLDER "Engine")
