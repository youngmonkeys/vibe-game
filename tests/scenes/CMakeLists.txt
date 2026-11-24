add_executable(scene-tests scene_tests.cpp "${VIBE_LIB_DIR}/vibe/core/SceneStack.cpp")
vibe_project_target(scene-tests)
target_include_directories(scene-tests PRIVATE "${VIBE_LIB_DIR}")
vibe_node_test(scene-tests)
