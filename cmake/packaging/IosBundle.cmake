if(VIBE_DEV_BUILD)
    set(VIBE_ATS_ARBITRARY_LOADS "true")
else()
    set(VIBE_ATS_ARBITRARY_LOADS "false")
endif()
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "16.4")
endif()
if(CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
    set(VIBE_IOS_PLATFORM "iPhoneSimulator")
else()
    set(VIBE_IOS_PLATFORM "iPhoneOS")
endif()
_vibe_platform_file(vibe_plist "${VIBE_GAME_PLATFORM_DIR}" "ios/Info.plist.in")
configure_file("${vibe_plist}" "${CMAKE_CURRENT_BINARY_DIR}/Info.plist" @ONLY)

set_target_properties(${VIBE_TARGET} PROPERTIES
    OUTPUT_NAME "${VIBE_APP_NAME}"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2")
target_link_libraries(${VIBE_TARGET} PRIVATE "-framework Foundation" "-framework Security")

set(vibe_resources "$<TARGET_BUNDLE_DIR:${VIBE_TARGET}>")
add_custom_target(${VIBE_TARGET}Bundle ALL
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${vibe_resources}/assets" "${vibe_resources}/config"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${vibe_resources}/config"
    COMMAND "${CMAKE_COMMAND}" -E copy "${VIBE_CONFIG_PATH}" "${vibe_resources}/config/configs.json"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${VIBE_ASSETS_DIR}" "${vibe_resources}/assets"
    COMMAND codesign --force --sign - --timestamp=none "$<TARGET_BUNDLE_DIR:${VIBE_TARGET}>"
    DEPENDS ${VIBE_TARGET}
    VERBATIM)
