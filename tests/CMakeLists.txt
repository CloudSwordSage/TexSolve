add_executable(texsolve_abi_c abi_c_smoke.c)
target_link_libraries(texsolve_abi_c PRIVATE TexSolve::shared)
add_test(NAME abi_tests_c COMMAND texsolve_abi_c)
set_tests_properties(abi_tests_c PROPERTIES
  ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:texsolve_shared>")

add_executable(texsolve_abi_cpp abi_cpp_smoke.cpp)
target_link_libraries(texsolve_abi_cpp PRIVATE TexSolve::static)
add_test(NAME abi_tests_cpp COMMAND texsolve_abi_cpp)

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
add_test(NAME calculus_tests COMMAND texsolve_core_tests
  --run_test=limits_and_integrals_follow_analytic_then_numeric_policy,symbolic_backends_and_calculus_follow_operation_contract)

add_executable(texsolve_solver_tests solver_tests.cpp)
target_link_libraries(texsolve_solver_tests PRIVATE TexSolve::static TexSolveThirdParty::all)
add_test(NAME linear_tests COMMAND texsolve_solver_tests --run_test=linear_algebra_returns_structured_results)
add_test(NAME equation_tests COMMAND texsolve_solver_tests
  --run_test=polynomial_solver_returns_all_roots,linear_system_is_supported,local_transcendental_root_is_supported,symbolic_equations_use_general_solver_before_numeric_fallback)
add_test(NAME optimization_tests COMMAND texsolve_solver_tests
  --run_test=explicit_optimization_backend_capabilities_are_enforced,optimization_does_not_report_ceres_iteration_limit_as_success,nlopt_and_sundials_paths_report_structured_metadata)
add_test(NAME ode_tests COMMAND texsolve_solver_tests --run_test=high_order_and_system_ode_are_normalized)

add_executable(texsolve_limit_tests limit_tests.cpp)
target_link_libraries(texsolve_limit_tests PRIVATE TexSolve::static TexSolveThirdParty::all)
add_test(NAME limit_tests COMMAND texsolve_limit_tests)

add_test(NAME cli_tests
  COMMAND ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:texsolve_cli>
    -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/check_cli.cmake)
set_tests_properties(cli_tests PROPERTIES
  ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:texsolve_shared>")

add_test(NAME documentation_traceability
  COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/check_docs.cmake)
