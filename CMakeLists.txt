cmake_minimum_required(VERSION 3.20)
project(CmakeParser LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(CmakeParser
    CmakeParser.cpp "Includes/ThreadPoolService.h")

target_include_directories(CmakeParser PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Includes
    ${CMAKE_CURRENT_SOURCE_DIR}/../ProcessRunGuard
    ${CMAKE_CURRENT_SOURCE_DIR}/../ThreadPool
    ${CMAKE_CURRENT_SOURCE_DIR}/../HeaderHelpers
)

if(MSVC)
    target_compile_options(CmakeParser PRIVATE
        /utf-8
        $<$<CONFIG:Release>:/O2 /Ob2 /GL /DNDEBUG>
    )

    target_link_options(CmakeParser PRIVATE
        $<$<CONFIG:Release>:/LTCG /INCREMENTAL:NO>
    )
else()
    target_compile_options(CmakeParser PRIVATE
        -march=native
        -mtune=native
        -fno-exceptions
        -fno-rtti
        -Wall
        $<$<CONFIG:Release>:-O3 -DNDEBUG>
    )

    target_link_options(CmakeParser PRIVATE
        $<$<CONFIG:Release>:-O3>
    )
endif()


if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()