# Luma::AssetWindows - asset editor windows: floating windows with
# minimize / maximize / close that dock into the window row above the
# toolbar (Home = the level editor).

add_library(LumaAssetWindows STATIC src/AssetWindows.cpp)
add_library(Luma::AssetWindows ALIAS LumaAssetWindows)

target_include_directories(LumaAssetWindows PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(LumaAssetWindows PUBLIC Luma::Core Luma::Slate)

set_target_properties(LumaAssetWindows PROPERTIES FOLDER "Editor")
