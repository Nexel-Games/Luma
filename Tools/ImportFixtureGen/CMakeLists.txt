# Writes the centimetre, Z-up robot.fbx fixture from robot.gltf.
luma_require_assimp()

add_executable(ImportFixtureGen main.cpp)
target_link_libraries(ImportFixtureGen PRIVATE assimp)
set_target_properties(ImportFixtureGen PROPERTIES FOLDER "Tools")
