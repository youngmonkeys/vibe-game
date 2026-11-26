# Pinned third-party code. Versions are recorded in docs/research/dependencies.md.
# Change a tag here and in that file in the same change.
include(FetchContent)

# SDL3, built from a release tag (not the Emscripten port) so the version is
# the one recorded in docs/research/dependencies.md.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
# Decision D15: one thread, no -pthread, on the web.
if(EMSCRIPTEN)
    set(SDL_PTHREADS OFF CACHE BOOL "" FORCE)
endif()
if(APPLE)
    set(VIBE_SDL3_SYSTEM "")
else()
    set(VIBE_SDL3_SYSTEM SYSTEM)
endif()
FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.18
    GIT_SHALLOW TRUE
    ${VIBE_SDL3_SYSTEM})

# nlohmann/json (decision D13), from the release tarball rather than the repo.
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    SYSTEM)

# stb_vorbis (decision D4, plan 10) decodes the Ogg music. nothings/stb has no
# release tags, so this is the copy in SDL_mixer's release tag: stb_vorbis
# v1.22 as one header, with SDL_mixer's fixes; its SDL hooks stay off unless
# STB_VORBIS_SDL is defined. One file, pinned by hash.
FetchContent_Declare(stb_vorbis
    URL https://raw.githubusercontent.com/libsdl-org/SDL_mixer/release-3.2.4/src/stb_vorbis/stb_vorbis.h
    URL_HASH SHA256=e0ddb968df34374d4253fa9d7b4a8bb757cba8e08a91fd0dfc6faa4abedcda2a
    DOWNLOAD_NO_EXTRACT TRUE
    SYSTEM)

FetchContent_MakeAvailable(SDL3 nlohmann_json stb_vorbis)
if(APPLE)
    target_compile_options(SDL3-static INTERFACE "--system-header-prefix=SDL3/")
endif()

# SDL3_ttf (decision D5, plan 11) draws the shipping UI's text. Its release
# tarball has no vendored FreeType (the repo has it as a git submodule), so
# FreeType comes from its own release tag and is unpacked where SDL3_ttf's
# vendored build looks for it. SOURCE_SUBDIR names a directory that does not
# exist, so MakeAvailable only downloads; add_subdirectory follows once both
# are in place. No HarfBuzz (Latin text needs no shaping) and no plutosvg.
FetchContent_Declare(sdl3_ttf
    URL https://github.com/libsdl-org/SDL_ttf/releases/download/release-3.2.2/SDL3_ttf-3.2.2.tar.gz
    URL_HASH SHA256=63547d58d0185c833213885b635a2c0548201cc8f301e6587c0be1a67e1e045d
    SOURCE_SUBDIR vibe-populate-only)
FetchContent_MakeAvailable(sdl3_ttf)
FetchContent_Declare(freetype
    URL https://github.com/freetype/freetype/archive/refs/tags/VER-2-15-0.tar.gz
    URL_HASH SHA256=87c7f1f8699b91e89f773f7dcfa505b044e1dae67f62a55d4e29c3afdcac4fb8
    SOURCE_DIR "${sdl3_ttf_SOURCE_DIR}/external/freetype"
    SOURCE_SUBDIR vibe-populate-only)
FetchContent_MakeAvailable(freetype)
set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
set(SDLTTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
set(SDLTTF_PLUTOSVG OFF CACHE BOOL "" FORCE)
set(FT_DISABLE_HVF ON CACHE BOOL "" FORCE)
set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDLTTF_INSTALL OFF CACHE BOOL "" FORCE)
set(SDLTTF_STRICT ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory("${sdl3_ttf_SOURCE_DIR}" "${sdl3_ttf_BINARY_DIR}" EXCLUDE_FROM_ALL SYSTEM)

if(NOT EMSCRIPTEN)
    FetchContent_Declare(zlib
        URL https://github.com/madler/zlib/releases/download/v1.3.2/zlib-1.3.2.tar.gz
        URL_HASH SHA256=bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16
        SOURCE_SUBDIR vibe-populate-only)
    FetchContent_MakeAvailable(zlib)
    set(VIBE_ZLIB_SOURCES)
    foreach(name adler32 compress crc32 deflate infback inffast inflate inftrees trees uncompr zutil)
        list(APPEND VIBE_ZLIB_SOURCES "${zlib_SOURCE_DIR}/${name}.c")
    endforeach()
    add_library(vibe_zlib STATIC ${VIBE_ZLIB_SOURCES})
    set_target_properties(vibe_zlib PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_include_directories(vibe_zlib SYSTEM PUBLIC "${zlib_SOURCE_DIR}")
endif()

# SYSTEM, so the decoder's warnings never trip this repo's -Werror.
# lib/vibe/audio/Vorbis.cpp compiles the implementation.
add_library(vibe_stb_vorbis INTERFACE)
target_include_directories(vibe_stb_vorbis SYSTEM INTERFACE "${stb_vorbis_SOURCE_DIR}")

# Dear ImGui (decision D5, plan 07), dev builds only: Release does not fetch or
# link it. It has no CMake project, so the core files and the two backends
# this client uses become one static library: imgui_impl_sdl3 and
# imgui_impl_sdlrenderer3 (decision D1 rules out imgui_impl_opengl3). From the
# release tarball, pinned by hash.
if(VIBE_DEV_BUILD)
    FetchContent_Declare(imgui
        URL https://github.com/ocornut/imgui/archive/refs/tags/v1.93.0.tar.gz
        URL_HASH SHA256=f7bf6bc76e5d79d40d751096663e6c5d18daf08129dd52e7f7b09e4251c8eeee
        SYSTEM)
    FetchContent_MakeAvailable(imgui)
    add_library(vibe_imgui STATIC
        "${imgui_SOURCE_DIR}/imgui.cpp"
        "${imgui_SOURCE_DIR}/imgui_draw.cpp"
        "${imgui_SOURCE_DIR}/imgui_tables.cpp"
        "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp")
    # SYSTEM, so ImGui's headers never trip this repo's -Werror.
    target_include_directories(vibe_imgui SYSTEM PUBLIC "${imgui_SOURCE_DIR}" "${imgui_SOURCE_DIR}/backends")
    target_compile_definitions(vibe_imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
    target_link_libraries(vibe_imgui PUBLIC SDL3::SDL3)
endif()
