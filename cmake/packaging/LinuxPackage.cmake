find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)
find_package(CURL REQUIRED)

set(vibe_package_dir "${CMAKE_CURRENT_BINARY_DIR}/${VIBE_APP_NAME}")

target_link_libraries(${VIBE_TARGET} PRIVATE OpenSSL::Crypto CURL::libcurl)

set_target_properties(${VIBE_TARGET} PROPERTIES
    OUTPUT_NAME "${VIBE_APP_NAME}"
    RUNTIME_OUTPUT_DIRECTORY "${vibe_package_dir}")

add_custom_target(${VIBE_TARGET}Package ALL
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${vibe_package_dir}/assets" "${vibe_package_dir}/config"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${vibe_package_dir}/config"
    COMMAND "${CMAKE_COMMAND}" -E copy "${VIBE_CONFIG_PATH}" "${vibe_package_dir}/config/configs.json"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${VIBE_ASSETS_DIR}" "${vibe_package_dir}/assets"
    COMMAND "${CMAKE_COMMAND}" -E rm -f "${CMAKE_CURRENT_BINARY_DIR}/${VIBE_APP_NAME}-linux.tar.gz"
    COMMAND "${CMAKE_COMMAND}" -E chdir "${CMAKE_CURRENT_BINARY_DIR}"
        "${CMAKE_COMMAND}" -E tar czf "${VIBE_APP_NAME}-linux.tar.gz" "${VIBE_APP_NAME}"
    DEPENDS ${VIBE_TARGET}
    VERBATIM)
