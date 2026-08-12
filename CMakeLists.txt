cmake_minimum_required(VERSION 3.25)

project(TexSolve VERSION 0.1.0 LANGUAGES C CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(TEXSOLVE_BUILD_TESTS "Build TexSolve tests" ${PROJECT_IS_TOP_LEVEL})
option(TEXSOLVE_BUILD_CLI "Build the TexSolve command-line program" ON)
option(TEXSOLVE_ENABLE_INSTALL "Generate TexSolve install rules" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/TexSolveDependencies.cmake)

add_library(texsolve_objects OBJECT src/texsolve.cpp src/parser.cpp)
set_target_properties(texsolve_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(texsolve_objects
  PUBLIC
    "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
  PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(texsolve_objects PRIVATE TexSolveThirdParty::all)
target_compile_options(texsolve_objects PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(texsolve_objects PRIVATE TEXSOLVE_BUILDING_DLL)

add_library(texsolve_shared SHARED $<TARGET_OBJECTS:texsolve_objects>)
add_library(texsolve_static STATIC $<TARGET_OBJECTS:texsolve_objects>)
add_library(TexSolve::shared ALIAS texsolve_shared)
add_library(TexSolve::static ALIAS texsolve_static)
set_target_properties(texsolve_shared PROPERTIES OUTPUT_NAME texsolve PREFIX "" EXPORT_NAME shared)
set_target_properties(texsolve_static PROPERTIES OUTPUT_NAME texsolve_static EXPORT_NAME static)
target_include_directories(texsolve_shared PUBLIC "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>" "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_include_directories(texsolve_static PUBLIC "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>" "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_compile_definitions(texsolve_static INTERFACE TEXSOLVE_STATIC)

if(TEXSOLVE_BUILD_CLI)
  add_executable(texsolve_cli src/cli.cpp)
  set_target_properties(texsolve_cli PROPERTIES OUTPUT_NAME texsolve)
  target_link_libraries(texsolve_cli PRIVATE TexSolve::shared)
  target_compile_options(texsolve_cli PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(TEXSOLVE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(TEXSOLVE_ENABLE_INSTALL)
  install(TARGETS texsolve_shared texsolve_static
    EXPORT TexSolveTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
  if(TEXSOLVE_BUILD_CLI)
    install(TARGETS texsolve_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  endif()
  install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(EXPORT TexSolveTargets NAMESPACE TexSolve:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TexSolve)
  configure_package_config_file(cmake/TexSolveConfig.cmake.in
    "${PROJECT_BINARY_DIR}/TexSolveConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TexSolve)
  write_basic_package_version_file("${PROJECT_BINARY_DIR}/TexSolveConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
  install(FILES "${PROJECT_BINARY_DIR}/TexSolveConfig.cmake" "${PROJECT_BINARY_DIR}/TexSolveConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TexSolve)
endif()
