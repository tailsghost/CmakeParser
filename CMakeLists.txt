cmake_minimum_required(VERSION 3.60)
project(CmakeParser LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(INC_DIR ${ROOT_DIR}/Includes)

file(GLOB_RECURSE ALL_CPP CONFIGURE_DEPENDS "${ROOT_DIR}/*.cpp")

function(find_cpp_by_name out_var filename)
  set(found "")
  foreach(f IN LISTS ALL_CPP)
    get_filename_component(base ${f} NAME)
    if(base STREQUAL "${filename}")
      set(found "${f}")
      break()
    endif()
  endforeach()
  set(${out_var} "${found}" PARENT_SCOPE)
endfunction()

find_cpp_by_name(CMAKE_PARSER "CmakeParser.cpp")

if(NOT CMAKE_PARSER STREQUAL "")
  add_executable(CmakeParser ${CMAKE_PARSER})
  target_include_directories(CmakeParser PUBLIC ${ROOT_DIR} ${INC_DIR})
else()
  message(STATUS "CmakeParser.cpp not found -> CmakeParser target skipped")
endif()

target_compile_features(CmakeParser PRIVATE cxx_std_20)
target_include_directories(CmakeParser PRIVATE ${CMAKE_SOURCE_DIR}/nlohmann/json ${CMAKE_SOURCE_DIR}/Helpers/HeaderHelpers.h ${CMAKE_SOURCE_DIR}/Helpers/ProcessRunGuard.h)

if(MSVC)
  target_compile_options(CmakeParser PRIVATE /utf-8)
endif()

