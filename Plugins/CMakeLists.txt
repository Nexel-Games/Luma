# Engine and project plugins.

file(GLOB _luma_plugin_entries RELATIVE "${CMAKE_SOURCE_DIR}/Plugins" "${CMAKE_SOURCE_DIR}/Plugins/*")
foreach(_p ${_luma_plugin_entries})
    if(IS_DIRECTORY "${CMAKE_SOURCE_DIR}/Plugins/${_p}"
       AND EXISTS "${CMAKE_SOURCE_DIR}/Plugins/${_p}/${_p}.lplugin")
        luma_add_plugin(${_p})
        luma_plugin_link_hosts(${_p})
    endif()
endforeach()

if(LUMA_PROJECT_DIR)
    file(GLOB _luma_proj_plugins RELATIVE "${LUMA_PROJECT_DIR}/Plugins" "${LUMA_PROJECT_DIR}/Plugins/*")
    foreach(_p ${_luma_proj_plugins})
        if(IS_DIRECTORY "${LUMA_PROJECT_DIR}/Plugins/${_p}"
           AND EXISTS "${LUMA_PROJECT_DIR}/Plugins/${_p}/${_p}.lplugin")
            luma_add_plugin(${_p} DIR "${LUMA_PROJECT_DIR}/Plugins/${_p}")
            luma_plugin_link_hosts(${_p})
        endif()
    endforeach()
endif()

luma_assert_game_has_no_editor_modules()
