# Luma::Jobs - the engine's job system: worker threads, priorities,
# dependencies, ParallelFor and a main-thread queue. Depends only on Core.

add_library(LumaJobs STATIC
    src/JobSystem.cpp)

add_library(Luma::Jobs ALIAS LumaJobs)

target_include_directories(LumaJobs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(LumaJobs PUBLIC Luma::Core)

set_target_properties(LumaJobs PROPERTIES FOLDER "Engine")
