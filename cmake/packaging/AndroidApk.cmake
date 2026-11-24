set(VIBE_ANDROID_MIN_SDK 24)
set(VIBE_ANDROID_TARGET_SDK 35)

set(VIBE_ANDROID_SDK_DEFAULT "$ENV{ANDROID_HOME}")
if(NOT VIBE_ANDROID_SDK_DEFAULT)
    cmake_path(GET ANDROID_NDK PARENT_PATH VIBE_ANDROID_NDK_PARENT)
    cmake_path(GET VIBE_ANDROID_NDK_PARENT PARENT_PATH VIBE_ANDROID_SDK_DEFAULT)
endif()
set(VIBE_ANDROID_SDK "${VIBE_ANDROID_SDK_DEFAULT}" CACHE PATH "Android SDK root (ANDROID_HOME)")
set(VIBE_ANDROID_BUILD_TOOLS "35.0.0" CACHE STRING "Android SDK build-tools version")
set(VIBE_ANDROID_PLATFORM "android-${VIBE_ANDROID_TARGET_SDK}" CACHE STRING "Android SDK platform to compile against")

set(VIBE_ANDROID_JAR "${VIBE_ANDROID_SDK}/platforms/${VIBE_ANDROID_PLATFORM}/android.jar")
set(VIBE_BUILD_TOOLS_DIR "${VIBE_ANDROID_SDK}/build-tools/${VIBE_ANDROID_BUILD_TOOLS}")
foreach(VIBE_REQUIRED "${VIBE_ANDROID_JAR}" "${VIBE_BUILD_TOOLS_DIR}/aapt2" "${VIBE_BUILD_TOOLS_DIR}/zipalign"
        "${VIBE_BUILD_TOOLS_DIR}/lib/d8.jar" "${VIBE_BUILD_TOOLS_DIR}/lib/apksigner.jar")
    if(NOT EXISTS "${VIBE_REQUIRED}")
        message(FATAL_ERROR "Android SDK file not found: ${VIBE_REQUIRED}. Install \"platforms;${VIBE_ANDROID_PLATFORM}\" "
            "and \"build-tools;${VIBE_ANDROID_BUILD_TOOLS}\" with sdkmanager, or set VIBE_ANDROID_SDK.")
    endif()
endforeach()

if(NOT VIBE_JAVA_HOME)
    set(VIBE_JAVA_HOME_DEFAULT "$ENV{JAVA_HOME}")
    if(CMAKE_HOST_APPLE)
        execute_process(COMMAND /usr/libexec/java_home -v 17+
            OUTPUT_VARIABLE VIBE_JAVA_HOME_FOUND OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE VIBE_JAVA_HOME_RESULT)
        if(VIBE_JAVA_HOME_RESULT EQUAL 0 AND VIBE_JAVA_HOME_FOUND)
            set(VIBE_JAVA_HOME_DEFAULT "${VIBE_JAVA_HOME_FOUND}")
        endif()
    endif()
    set(VIBE_JAVA_HOME "${VIBE_JAVA_HOME_DEFAULT}" CACHE PATH "JDK 17 or newer for javac, d8 and apksigner")
endif()
set(VIBE_JAVA "${VIBE_JAVA_HOME}/bin/java")
set(VIBE_JAVAC "${VIBE_JAVA_HOME}/bin/javac")
set(VIBE_JAR "${VIBE_JAVA_HOME}/bin/jar")
set(VIBE_KEYTOOL "${VIBE_JAVA_HOME}/bin/keytool")
if(NOT EXISTS "${VIBE_JAVAC}")
    message(FATAL_ERROR "javac not found in VIBE_JAVA_HOME (${VIBE_JAVA_HOME}). Set it to a JDK 17 or newer.")
endif()

set(VIBE_DEBUG_KEYSTORE "$ENV{HOME}/.android/debug.keystore" CACHE FILEPATH "Keystore that signs the dev APK")
if(NOT EXISTS "${VIBE_DEBUG_KEYSTORE}")
    cmake_path(GET VIBE_DEBUG_KEYSTORE PARENT_PATH VIBE_DEBUG_KEYSTORE_DIR)
    file(MAKE_DIRECTORY "${VIBE_DEBUG_KEYSTORE_DIR}")
    execute_process(COMMAND "${VIBE_KEYTOOL}" -genkeypair -keystore "${VIBE_DEBUG_KEYSTORE}"
        -storepass android -keypass android -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000
        -dname "CN=Android Debug,O=Android,C=US"
        RESULT_VARIABLE VIBE_KEYTOOL_RESULT)
    if(NOT VIBE_KEYTOOL_RESULT EQUAL 0)
        message(FATAL_ERROR "cannot create the debug keystore ${VIBE_DEBUG_KEYSTORE}")
    endif()
endif()

if(VIBE_DEV_BUILD)
    set(VIBE_CLEARTEXT "true")
else()
    set(VIBE_CLEARTEXT "false")
endif()
_vibe_platform_file(vibe_manifest "${VIBE_GAME_PLATFORM_DIR}" "android/AndroidManifest.xml.in")
configure_file("${vibe_manifest}"
    "${CMAKE_CURRENT_BINARY_DIR}/AndroidManifest.xml" @ONLY)

set_target_properties(${VIBE_TARGET} PROPERTIES OUTPUT_NAME main)
target_link_libraries(${VIBE_TARGET} PRIVATE android log)
target_link_options(${VIBE_TARGET} PRIVATE "-Wl,--undefined=JNI_OnLoad" "-Wl,--undefined=SDL_main")

file(GLOB_RECURSE VIBE_ANDROID_JAVA CONFIGURE_DEPENDS
    "${sdl3_SOURCE_DIR}/android-project/app/src/main/java/*.java"
    "${VIBE_GAME_DIR}/platform/android/java/*.java"
    "${VIBE_GAME_PLATFORM_DIR}/android/java/*.java")

file(GLOB_RECURSE VIBE_ASSET_FILES LIST_DIRECTORIES false CONFIGURE_DEPENDS
    "${VIBE_ASSETS_DIR}/*")
_vibe_platform_file(vibe_launcher_icon "${VIBE_GAME_PLATFORM_DIR}" "web/icon-512.png")

set(VIBE_APK_WORK "${CMAKE_CURRENT_BINARY_DIR}/apk")
set(VIBE_APK "${CMAKE_CURRENT_BINARY_DIR}/${VIBE_APP_NAME}.apk")
set(VIBE_APK_LIB "${VIBE_APK_WORK}/root/lib/${ANDROID_ABI}/libmain.so")
if(VIBE_DEV_BUILD)
    set(VIBE_APK_STRIP "${CMAKE_COMMAND}" -E true)
else()
    set(VIBE_APK_STRIP "${CMAKE_STRIP}" --strip-unneeded "${VIBE_APK_LIB}")
endif()

add_custom_command(OUTPUT "${VIBE_APK}"
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${VIBE_APK_WORK}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${VIBE_APK_WORK}/classes" "${VIBE_APK_WORK}/root/lib/${ANDROID_ABI}"
        "${VIBE_APK_WORK}/assets/config" "${VIBE_APK_WORK}/res/mipmap-xxxhdpi"
    COMMAND "${VIBE_JAVAC}" -source 8 -target 8 -Xlint:-options -nowarn -encoding UTF-8
        -bootclasspath "${VIBE_ANDROID_JAR}" -d "${VIBE_APK_WORK}/classes" ${VIBE_ANDROID_JAVA}
    COMMAND "${VIBE_JAR}" cf "${VIBE_APK_WORK}/classes.jar" -C "${VIBE_APK_WORK}/classes" .
    COMMAND "${VIBE_JAVA}" -cp "${VIBE_BUILD_TOOLS_DIR}/lib/d8.jar" com.android.tools.r8.D8
        --min-api ${VIBE_ANDROID_MIN_SDK} --lib "${VIBE_ANDROID_JAR}" --output "${VIBE_APK_WORK}/root"
        "${VIBE_APK_WORK}/classes.jar"
    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:${VIBE_TARGET}>" "${VIBE_APK_LIB}"
    COMMAND ${VIBE_APK_STRIP}
    COMMAND "${CMAKE_COMMAND}" -E copy "${VIBE_CONFIG_PATH}" "${VIBE_APK_WORK}/assets/config/configs.json"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${VIBE_ASSETS_DIR}" "${VIBE_APK_WORK}/assets/assets"
    COMMAND "${CMAKE_COMMAND}" -E copy "${vibe_launcher_icon}"
        "${VIBE_APK_WORK}/res/mipmap-xxxhdpi/ic_launcher.png"
    COMMAND "${VIBE_BUILD_TOOLS_DIR}/aapt2" compile --dir "${VIBE_APK_WORK}/res" -o "${VIBE_APK_WORK}/res.zip"
    COMMAND "${VIBE_BUILD_TOOLS_DIR}/aapt2" link -o "${VIBE_APK_WORK}/unsigned.apk"
        -I "${VIBE_ANDROID_JAR}" --manifest "${CMAKE_CURRENT_BINARY_DIR}/AndroidManifest.xml"
        -A "${VIBE_APK_WORK}/assets" "${VIBE_APK_WORK}/res.zip"
    COMMAND "${CMAKE_COMMAND}" -E chdir "${VIBE_APK_WORK}/root" zip -q -r ../unsigned.apk classes.dex lib
    COMMAND "${VIBE_BUILD_TOOLS_DIR}/zipalign" -f 4 "${VIBE_APK_WORK}/unsigned.apk" "${VIBE_APK_WORK}/aligned.apk"
    COMMAND "${VIBE_JAVA}" -jar "${VIBE_BUILD_TOOLS_DIR}/lib/apksigner.jar" sign
        --ks "${VIBE_DEBUG_KEYSTORE}" --ks-pass pass:android --key-pass pass:android --ks-key-alias androiddebugkey
        --out "${VIBE_APK}" "${VIBE_APK_WORK}/aligned.apk"
    DEPENDS ${VIBE_TARGET} ${VIBE_ANDROID_JAVA} "${CMAKE_CURRENT_BINARY_DIR}/AndroidManifest.xml" "${VIBE_CONFIG_PATH}"
        ${VIBE_ASSET_FILES}
    VERBATIM)
add_custom_target(${VIBE_TARGET}Apk ALL DEPENDS "${VIBE_APK}")
