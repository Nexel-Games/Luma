add_library(LumaPanelPreferences STATIC
    src/Preferences.cpp
    src/PreferencesPanel.cpp)
add_library(Luma::PanelPreferences ALIAS LumaPanelPreferences)
target_include_directories(LumaPanelPreferences PUBLIC include)
target_link_libraries(LumaPanelPreferences PUBLIC Luma::PanelViewport PRIVATE shell32 ole32)
set_target_properties(LumaPanelPreferences PROPERTIES FOLDER "Editor/Panels")
