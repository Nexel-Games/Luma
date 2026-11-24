# Modular vs monolithic plugin modules (PLUGIN_ARCHITECTURE.md §10).
#
# ON  (default): engine and editor libraries are SHARED (LumaCore.dll, …).
#     Plugin modules build as
#     Plugins/<Name>/Binaries/Win64/<Config>/LumaEditor-<Module>.dll and are
#     loaded with DllModuleBackend. LumaEngine still whole-archives Runtime
#     static modules.
# OFF: everything static (previous behaviour).

option(LUMA_MODULAR "Build engine libraries as DLLs and load editor plugin modules as DLLs" ON)

if(LUMA_MODULAR)
    set(LUMA_LIBRARY_TYPE SHARED)
    target_compile_definitions(luma_flags INTERFACE LUMA_MODULAR=1)
    message(STATUS "Luma: LUMA_MODULAR=ON (engine DLLs, plugin DLLs)")
else()
    set(LUMA_LIBRARY_TYPE STATIC)
    message(STATUS "Luma: LUMA_MODULAR=OFF (static plugin modules)")
endif()

# Plugin DLLs and engine DLLs share one CRT heap.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

function(luma_configure_modular target)
    if(NOT TARGET ${target})
        return()
    endif()
    get_target_property(_type ${target} TYPE)
    if(_type STREQUAL "SHARED_LIBRARY" OR _type STREQUAL "MODULE_LIBRARY")
        set_target_properties(${target} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
        if(target STREQUAL "LumaCore")
            target_compile_definitions(${target} PRIVATE LUMA_CORE_BUILDING)
        elseif(target STREQUAL "LumaScene")
            target_compile_definitions(${target} PRIVATE LUMA_SCENE_BUILDING)
        elseif(target STREQUAL "LumaPlugins")
            target_compile_definitions(${target} PRIVATE LUMA_PLUGINS_BUILDING)
        endif()
    endif()
endfunction()

function(luma_configure_modular_tree dir)
    get_property(_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_t ${_targets})
        luma_configure_modular(${_t})
    endforeach()
    get_property(_subs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(_s ${_subs})
        luma_configure_modular_tree("${_s}")
    endforeach()
endfunction()
