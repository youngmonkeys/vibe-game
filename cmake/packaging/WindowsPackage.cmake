set(vibe_package_dir "${CMAKE_CURRENT_BINARY_DIR}/${VIBE_APP_NAME}")

target_compile_definitions(${VIBE_TARGET} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_compile_options(${VIBE_TARGET} PRIVATE -Wno-format)
target_link_libraries(${VIBE_TARGET} PRIVATE ws2_32 winhttp bcrypt crypt32 psapi)
target_link_options(${VIBE_TARGET} PRIVATE -static -static-libgcc -static-libstdc++)

set_target_properties(${VIBE_TARGET} PROPERTIES
    OUTPUT_NAME "${VIBE_APP_NAME}"
    RUNTIME_OUTPUT_DIRECTORY "${vibe_package_dir}")
if(NOT VIBE_DEV_BUILD)
    set_target_properties(${VIBE_TARGET} PROPERTIES WIN32_EXECUTABLE ON)
endif()

add_custom_target(${VIBE_TARGET}Package ALL
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${vibe_package_dir}/assets" "${vibe_package_dir}/config"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${vibe_package_dir}/config"
    COMMAND "${CMAKE_COMMAND}" -E copy "${VIBE_CONFIG_PATH}" "${vibe_package_dir}/config/configs.json"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${VIBE_ASSETS_DIR}" "${vibe_package_dir}/assets"
    COMMAND "${CMAKE_COMMAND}" -E rm -f "${CMAKE_CURRENT_BINARY_DIR}/${VIBE_APP_NAME}-windows.zip"
    COMMAND "${CMAKE_COMMAND}" -E chdir "${CMAKE_CURRENT_BINARY_DIR}"
        "${CMAKE_COMMAND}" -E tar cf "${VIBE_APP_NAME}-windows.zip" --format=zip "${VIBE_APP_NAME}"
    DEPENDS ${VIBE_TARGET}
    VERBATIM)
