luma_add_module(SampleSpinnerEditor
    PLUGIN SampleSpinner
    TYPE Editor
    SOURCES Private/SampleSpinnerEditorModule.cpp
    DEPENDS Luma::Plugins Luma::EditorFramework Luma::Slate Luma::Scene SampleSpinner)

add_library(Luma::SampleSpinnerEditor ALIAS SampleSpinnerEditor)
if(TARGET SampleSpinnerEditorDll)
    add_library(Luma::SampleSpinnerEditorDll ALIAS SampleSpinnerEditorDll)
endif()
