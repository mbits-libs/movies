cmake_minimum_required(VERSION 3.19)
project(movies LANGUAGES CXX VERSION 0.1.0)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup(TARGETS)

if (MSVC)
    # https://github.com/microsoft/STL/issues/1814
    set(CMAKE_CXX_STANDARD 23)
else()
    set(CMAKE_CXX_STANDARD 20)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if (NOT TARGET json)
add_subdirectory(external)
endif()

list(APPEND CMAKE_PREFIX_PATH ${CMAKE_BINARY_DIR})

if (NOT TARGET fmt::fmt)
    find_package(fmt REQUIRED CONFIG)
endif()
if (NOT TARGET date::date)
    find_package(date REQUIRED CONFIG)
endif()

##################################################################
# IO
set(IO_SRCS
    inc/io/file.hpp
    src/io.cpp
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${IO_SRCS})

add_library(io STATIC ${IO_SRCS})
target_include_directories(io PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/inc")

##################################################################
# MOVIES
set(MOVIES_SRCS
    inc/movies/db_info.hpp
    inc/movies/diff.hpp
    inc/movies/fwd.hpp
    inc/movies/loader.hpp
    inc/movies/movie_info.hpp
    inc/movies/opt.hpp

    src/db_info.cpp
    src/diff.cpp
    src/difflib.hpp
    src/loader.cpp
    src/movie_info.cpp
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${MOVIES_SRCS})

add_library(movies STATIC ${MOVIES_SRCS})
target_include_directories(movies PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/inc")
target_link_libraries(movies PUBLIC json io fmt::fmt date::date)

if (TARGET nav)
    target_link_libraries(movies PUBLIC nav)
    target_compile_definitions(movies PUBLIC MOVIES_HAS_NAVIGATOR)
endif()
