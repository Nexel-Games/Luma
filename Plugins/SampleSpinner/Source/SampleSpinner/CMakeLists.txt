luma_add_module(SampleSpinner
    PLUGIN SampleSpinner
    TYPE Runtime
    SOURCES
        Private/SampleSpinnerComponent.cpp
        Private/SampleSpinnerModule.cpp
    PUBLIC_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/Public
    DEPENDS Luma::Plugins Luma::Scene Luma::Math Luma::Serialization)

add_library(Luma::SampleSpinner ALIAS SampleSpinner)
if(TARGET SampleSpinnerDll)
    add_library(Luma::SampleSpinnerDll ALIAS SampleSpinnerDll)
endif()
