add_executable(texsolve_abi_c abi_c_smoke.c)
target_link_libraries(texsolve_abi_c PRIVATE TexSolve::shared)
add_test(NAME abi_c_smoke COMMAND texsolve_abi_c)
set_tests_properties(abi_c_smoke PROPERTIES
  ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:texsolve_shared>")

add_executable(texsolve_abi_cpp abi_cpp_smoke.cpp)
target_link_libraries(texsolve_abi_cpp PRIVATE TexSolve::static)
add_test(NAME abi_cpp_smoke COMMAND texsolve_abi_cpp)
