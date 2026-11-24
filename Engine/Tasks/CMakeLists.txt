# Luma::Tasks - user-visible background operations (progress, status,
# cancellation, failures). Thread-safe; UI reads snapshots. Depends only on Core.

add_library(LumaTasks ${LUMA_LIBRARY_TYPE}
    src/TaskManager.cpp)

add_library(Luma::Tasks ALIAS LumaTasks)

target_include_directories(LumaTasks PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(LumaTasks PUBLIC Luma::Core)

set_target_properties(LumaTasks PROPERTIES FOLDER "Engine")
