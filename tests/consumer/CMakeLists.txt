cmake_minimum_required(VERSION 3.25)
project(TexSolveConsumer LANGUAGES C)

enable_testing()
find_package(TexSolve 0.1 CONFIG REQUIRED)
get_filename_component(TEXSOLVE_PREFIX "${TexSolve_DIR}/../../.." ABSOLUTE)

add_executable(shared_consumer consumer.c)
target_link_libraries(shared_consumer PRIVATE TexSolve::shared)
add_test(NAME shared_consumer COMMAND shared_consumer)
set_tests_properties(shared_consumer PROPERTIES
  ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:TexSolve::shared>")
add_test(NAME shared_consumer_i18n COMMAND shared_consumer --i18n)
set_tests_properties(shared_consumer_i18n PROPERTIES
  ENVIRONMENT "LANG=zh_CN.UTF-8;LC_ALL=zh_CN.UTF-8;TEXSOLVE_LOCALE_DIR=${TEXSOLVE_PREFIX}/share/locale"
  ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:TexSolve::shared>")

add_executable(static_consumer consumer.c)
target_link_libraries(static_consumer PRIVATE TexSolve::static)
add_test(NAME static_consumer COMMAND static_consumer)
add_test(NAME static_consumer_i18n COMMAND static_consumer --i18n)
set_tests_properties(static_consumer_i18n PROPERTIES
  ENVIRONMENT "LANG=zh_CN.UTF-8;LC_ALL=zh_CN.UTF-8;TEXSOLVE_LOCALE_DIR=${TEXSOLVE_PREFIX}/share/locale")
