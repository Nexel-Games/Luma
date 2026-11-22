# Luma::Profiling - frame profiler (CPU scopes, GPU pass timings, render
# counters) and the frame-rate limiter. Depends only on Luma::Core.

add_library(LumaProfiling STATIC
    src/Profiler.cpp
    src/FrameLimiter.cpp)

add_library(Luma::Profiling ALIAS LumaProfiling)

target_include_directories(LumaProfiling
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(LumaProfiling PUBLIC Luma::Core)

set_target_properties(LumaProfiling PROPERTIES FOLDER "Engine")
