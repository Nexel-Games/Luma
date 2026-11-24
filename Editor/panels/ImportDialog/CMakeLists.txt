add_library(LumaPanelImportDialog ${LUMA_LIBRARY_TYPE}
    src/ImportDialog.cpp)

add_library(Luma::PanelImportDialog ALIAS LumaPanelImportDialog)

target_include_directories(LumaPanelImportDialog
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(LumaPanelImportDialog
    PUBLIC Luma::Slate Luma::AssetImport)

set_target_properties(LumaPanelImportDialog PROPERTIES FOLDER "Editor/Panels")
