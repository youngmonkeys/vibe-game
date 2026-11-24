# Helpers shared by the root project and by test projects that configure on
# their own (for example `emcmake cmake -S tests/sim -B build/web-sim-tests`).
include_guard(GLOBAL)

option(VIBE_SANITIZE "Build project targets with -fsanitize=address,undefined" OFF)

# Settings for every target this repo owns. Fetched dependencies do not get
# them, so their warnings never break the build.
function(vibe_project_target target)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Werror
        # Emscripten builds have C++ exceptions off. Make that explicit so a
        # throw or try is a compile error instead of a runtime abort.
        $<$<COMPILE_LANGUAGE:CXX,OBJCXX>:-fno-exceptions>
        $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    # Dev builds: ImGui (plan 07), debug keys, verbose logs. Never in Release.
    target_compile_definitions(${target} PRIVATE
        $<$<CONFIG:Debug,RelWithDebInfo>:VIBE_DEV>)
    if(VIBE_SANITIZE)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
endfunction()

# Fails the configure if a flag that breaks bit-exact float math with the Java
# server reaches the target (research/simulation.md, Precision).
function(_vibe_check_sim_flags target)
    string(TOUPPER "${CMAKE_BUILD_TYPE}" config)
    get_target_property(compile_options ${target} COMPILE_OPTIONS)
    get_target_property(link_options ${target} LINK_OPTIONS)
    get_directory_property(directory_options COMPILE_OPTIONS)
    set(all_flags "${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_${config}} ${CMAKE_CXX_FLAGS} \
${CMAKE_CXX_FLAGS_${config}} ${compile_options} ${link_options} ${directory_options}")
    foreach(forbidden -ffast-math -Ofast -funsafe-math-optimizations -mrelaxed-simd)
        string(FIND "${all_flags}" "${forbidden}" position)
        if(NOT position EQUAL -1)
            message(FATAL_ERROR
                "${target}: ${forbidden} is not allowed on simulation code. It breaks "
                "bit-exact float math with the server (research/simulation.md).")
        endif()
    endforeach()
endfunction()

# Simulation code must round like the Java server: IEEE single precision, no
# fused multiply-add. Applied PUBLIC so inline math in sim headers compiled by
# a consumer gets the same flag. The check runs now and again at the end of
# the directory, so a forbidden flag added after this call is still caught.
function(vibe_sim_flags target)
    get_target_property(type ${target} TYPE)
    if(type STREQUAL "INTERFACE_LIBRARY")
        target_compile_options(${target} INTERFACE -ffp-contract=off)
    else()
        target_compile_options(${target} PUBLIC -ffp-contract=off)
    endif()
    _vibe_check_sim_flags(${target})
    cmake_language(EVAL CODE "
        cmake_language(DEFER DIRECTORY [[${CMAKE_CURRENT_SOURCE_DIR}]]
            CALL _vibe_check_sim_flags [[${target}]])")
endfunction()

# Links `target` as a Node program and registers it with CTest. The Emscripten
# toolchain sets CMAKE_CROSSCOMPILING_EMULATOR to the emsdk's Node, so CTest
# runs `node <target>.js`. NODERAWFS lets the test read files from the source
# tree by their real path; never use it for the page. EXIT_RUNTIME passes the
# return value of main() through as the process exit code.
#
#   vibe_node_test(sim_tests [WORKING_DIRECTORY <dir>])
function(vibe_node_test target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "WORKING_DIRECTORY" "")
    if(NOT arg_WORKING_DIRECTORY)
        set(arg_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    if(EMSCRIPTEN)
        target_link_options(${target} PRIVATE
            -sENVIRONMENT=node
            -sNODERAWFS=1
            -sEXIT_RUNTIME=1)
    endif()
    add_test(NAME ${target} COMMAND ${target} WORKING_DIRECTORY "${arg_WORKING_DIRECTORY}")
endfunction()
