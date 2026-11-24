# Background Tasks: the status-bar indicator and expandable task panel that
# show Luma::Tasks::TaskManager operations (compiles, imports, ...).
add_library(LumaPanelBackgroundTasks ${LUMA_LIBRARY_TYPE}
    src/BackgroundTasks.cpp)

add_library(Luma::PanelBackgroundTasks ALIAS LumaPanelBackgroundTasks)

target_include_directories(LumaPanelBackgroundTasks
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(LumaPanelBackgroundTasks
    PUBLIC Luma::Core Luma::Slate Luma::Tasks)

set_target_properties(LumaPanelBackgroundTasks PROPERTIES FOLDER "Editor/Panels")
