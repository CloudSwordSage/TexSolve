set(TEXSOLVE_THIRD_PARTY_ROOT "${PROJECT_SOURCE_DIR}/third_party")

set(_texsolve_required_files
  boost/include/boost-1_86/boost/spirit/home/x3.hpp
  boost/lib/libboost_program_options-mgw16-mt-s-x64-1_86.a
  symengine/include/symengine/basic.h
  symengine/lib/libsymengine.a
  ginac/include/ginac/ginac.h
  ginac/lib/libginac.a
  gmp/include/gmp.h
  gmp/lib/libgmp.a
  mpfr/include/mpfr.h
  mpfr/lib/libmpfr.a
  eigen/include/eigen3/Eigen/Core
  armadillo/include/armadillo
  armadillo/lib/libarmadillo.a
  gsl/include/gsl/gsl_integration.h
  gsl/lib/libgsl.a
  gsl/lib/libgslcblas.a
  ceres/include/ceres/ceres.h
  ceres/lib/libceres.a
  nlopt/include/nlopt.h
  nlopt/lib/libnlopt.a
  sundials/include/cvode/cvode.h
  sundials/lib/libsundials_cvode.a
  sundials/lib/libsundials_nvecserial.a
  sundials/lib/libsundials_core.a
  openblas/include/openblas/cblas.h
  openblas/lib/libopenblas.a
  cln/include/cln/cln.h
  cln/lib/libcln.a)

set(_texsolve_missing_files)
foreach(_relative IN LISTS _texsolve_required_files)
  if(NOT EXISTS "${TEXSOLVE_THIRD_PARTY_ROOT}/${_relative}")
    list(APPEND _texsolve_missing_files "third_party/${_relative}")
  endif()
endforeach()
if(_texsolve_missing_files)
  list(JOIN _texsolve_missing_files "\n  " _texsolve_missing_text)
  message(FATAL_ERROR "TexSolve offline dependencies are incomplete:\n  ${_texsolve_missing_text}")
endif()

add_library(texsolve_third_party INTERFACE)
add_library(TexSolveThirdParty::all ALIAS texsolve_third_party)
target_include_directories(texsolve_third_party SYSTEM INTERFACE
  "${TEXSOLVE_THIRD_PARTY_ROOT}/boost/include/boost-1_86"
  "${TEXSOLVE_THIRD_PARTY_ROOT}/symengine/include"
  "${TEXSOLVE_THIRD_PARTY_ROOT}/ginac/include"
  "${TEXSOLVE_THIRD_PARTY_ROOT}/gmp/include"
  "${TEXSOLVE_THIRD_PARTY_ROOT}/mpfr/include"
  "${TEXSOLVE_THIRD_PARTY_ROOT}/eigen/include/eigen3"
  "${TEXSOLVE_THIRD_PARTY_ROOT}/armadillo/include"
  "${TEXSOLVE_THIRD_PARTY_ROOT}/gsl/include"
  "${TEXSOLVE_THIRD_PARTY_ROOT}/ceres/include"
  "${TEXSOLVE_THIRD_PARTY_ROOT}/nlopt/include"
  "${TEXSOLVE_THIRD_PARTY_ROOT}/sundials/include"
  "${TEXSOLVE_THIRD_PARTY_ROOT}/openblas/include/openblas"
  "${TEXSOLVE_THIRD_PARTY_ROOT}/cln/include")

function(texsolve_import_archive target relative_path)
  add_library(${target} STATIC IMPORTED GLOBAL)
  set_target_properties(${target} PROPERTIES IMPORTED_LOCATION "${TEXSOLVE_THIRD_PARTY_ROOT}/${relative_path}")
endfunction()

texsolve_import_archive(TexSolveThirdParty::boost_program_options boost/lib/libboost_program_options-mgw16-mt-s-x64-1_86.a)
texsolve_import_archive(TexSolveThirdParty::symengine symengine/lib/libsymengine.a)
texsolve_import_archive(TexSolveThirdParty::ginac ginac/lib/libginac.a)
texsolve_import_archive(TexSolveThirdParty::gmp gmp/lib/libgmp.a)
texsolve_import_archive(TexSolveThirdParty::mpfr mpfr/lib/libmpfr.a)
texsolve_import_archive(TexSolveThirdParty::armadillo armadillo/lib/libarmadillo.a)
texsolve_import_archive(TexSolveThirdParty::gsl gsl/lib/libgsl.a)
texsolve_import_archive(TexSolveThirdParty::gslcblas gsl/lib/libgslcblas.a)
texsolve_import_archive(TexSolveThirdParty::ceres ceres/lib/libceres.a)
texsolve_import_archive(TexSolveThirdParty::nlopt nlopt/lib/libnlopt.a)
texsolve_import_archive(TexSolveThirdParty::sundials_cvode sundials/lib/libsundials_cvode.a)
texsolve_import_archive(TexSolveThirdParty::sundials_nvecserial sundials/lib/libsundials_nvecserial.a)
texsolve_import_archive(TexSolveThirdParty::sundials_core sundials/lib/libsundials_core.a)
texsolve_import_archive(TexSolveThirdParty::openblas openblas/lib/libopenblas.a)
texsolve_import_archive(TexSolveThirdParty::cln cln/lib/libcln.a)

target_link_libraries(texsolve_third_party INTERFACE
  TexSolveThirdParty::boost_program_options
  TexSolveThirdParty::symengine TexSolveThirdParty::ginac TexSolveThirdParty::cln
  TexSolveThirdParty::mpfr TexSolveThirdParty::gmp
  TexSolveThirdParty::armadillo TexSolveThirdParty::openblas
  TexSolveThirdParty::gsl TexSolveThirdParty::gslcblas
  TexSolveThirdParty::ceres TexSolveThirdParty::nlopt
  TexSolveThirdParty::sundials_cvode TexSolveThirdParty::sundials_nvecserial TexSolveThirdParty::sundials_core)
