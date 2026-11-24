_vibe_platform_file(vibe_shell_html_in "${VIBE_GAME_PLATFORM_DIR}" "web/shell.html")
_vibe_platform_file(vibe_shell_js_in "${VIBE_GAME_PLATFORM_DIR}" "web/shell.js")
_vibe_platform_file(vibe_idbfs_js "${VIBE_GAME_PLATFORM_DIR}" "web/idbfs.js")
set(vibe_shell_html "${CMAKE_CURRENT_BINARY_DIR}/shell.in.html")
configure_file("${vibe_shell_html_in}" "${vibe_shell_html}" @ONLY)

if(VIBE_DEV_BUILD)
    set(vibe_exports _main _vibe_dev_ui_rect ${G_WEB_DEV_EXPORTS})
    list(JOIN vibe_exports "," vibe_exports_text)
    target_link_options(${VIBE_TARGET} PRIVATE
        "-sEXPORTED_FUNCTIONS=${vibe_exports_text}"
        "-sEXPORTED_RUNTIME_METHODS=ccall")
endif()

set_target_properties(${VIBE_TARGET} PROPERTIES
    OUTPUT_NAME "${VIBE_APP_NAME}"
    SUFFIX ".html"
    LINK_DEPENDS "${vibe_shell_html};${vibe_idbfs_js};${VIBE_CONFIG_PATH}")

target_link_options(${VIBE_TARGET} PRIVATE
    -sMIN_WEBGL_VERSION=2
    -sMAX_WEBGL_VERSION=2
    -sALLOW_MEMORY_GROWTH=1
    -sENVIRONMENT=web
    -sEXIT_RUNTIME=1
    "SHELL:--shell-file ${vibe_shell_html}"
    "SHELL:--preload-file ${VIBE_CONFIG_PATH}@/config/configs.json"
    -sFETCH=1
    -lwebsocket.js
    -lidbfs.js
    "SHELL:--pre-js ${vibe_idbfs_js}"
    "-sDEFAULT_LIBRARY_FUNCS_TO_INCLUDE=$stringToNewUTF8,$UTF8ToString"
    $<$<CONFIG:Release>:-sASSERTIONS=0>
    $<$<CONFIG:Release>:--emit-symbol-map>)

file(GLOB_RECURSE vibe_asset_files LIST_DIRECTORIES false CONFIGURE_DEPENDS "${VIBE_ASSETS_DIR}/*")
list(FILTER vibe_asset_files EXCLUDE REGEX "/\\.gitkeep$")
if(vibe_asset_files)
    set_property(TARGET ${VIBE_TARGET} APPEND PROPERTY LINK_DEPENDS ${vibe_asset_files})
    target_link_options(${VIBE_TARGET} PRIVATE "SHELL:--preload-file ${VIBE_ASSETS_DIR}@/assets")
endif()

set(VIBE_DEV_SHELL ${VIBE_DEV_BUILD})
configure_file("${vibe_shell_js_in}" "${CMAKE_CURRENT_BINARY_DIR}/shell.js" @ONLY)
_vibe_platform_file(vibe_manifest_in "${VIBE_GAME_PLATFORM_DIR}" "web/manifest.webmanifest")
configure_file("${vibe_manifest_in}" "${CMAKE_CURRENT_BINARY_DIR}/manifest.webmanifest" @ONLY)
foreach(vibe_web_icon icon-192.png icon-512.png apple-touch-icon.png)
    _vibe_platform_file(vibe_icon "${VIBE_GAME_PLATFORM_DIR}" "web/${vibe_web_icon}")
    configure_file("${vibe_icon}" "${CMAKE_CURRENT_BINARY_DIR}/${vibe_web_icon}" COPYONLY)
endforeach()
