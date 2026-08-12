add_executable(texsolve_abi_c abi_c_smoke.c)
target_link_libraries(texsolve_abi_c PRIVATE TexSolve::shared)
add_test(NAME abi_c_smoke COMMAND texsolve_abi_c)
set_tests_properties(abi_c_smoke PROPERTIES
  ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:texsolve_shared>")

add_executable(texsolve_abi_cpp abi_cpp_smoke.cpp)
target_link_libraries(texsolve_abi_cpp PRIVATE TexSolve::static)
add_test(NAME abi_cpp_smoke COMMAND texsolve_abi_cpp)

add_executable(texsolve_parser_tests parser_tests.cpp)
target_include_directories(texsolve_parser_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(texsolve_parser_tests PRIVATE TexSolve::static TexSolveThirdParty::all)
add_test(NAME parser_tests COMMAND texsolve_parser_tests)

add_executable(texsolve_context_tests context_tests.cpp)
target_link_libraries(texsolve_context_tests PRIVATE TexSolve::static TexSolveThirdParty::all)
add_test(NAME core_context_tests COMMAND texsolve_context_tests)

add_executable(texsolve_core_tests core_tests.cpp)
target_link_libraries(texsolve_core_tests PRIVATE TexSolve::static TexSolveThirdParty::all)
add_test(NAME core_tests COMMAND texsolve_core_tests)
