cmake_minimum_required(VERSION 3.19)
project(movies LANGUAGES CXX VERSION 0.1.1)

set(MOVIES_INSTALL OFF CACHE BOOL "Install the library")
set(MOVIES_IGNORE_CONAN OFF CACHE BOOL "Ignore conanbuildinfo")

if (NOT MOVIES_IGNORE_CONAN)
    if (EXISTS ${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
        include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
        conan_basic_setup(TARGETS)
    elseif(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        message(FATAL_ERROR "Missing file\n  ${CMAKE_BINARY_DIR}/conanbuildinfo.cmake\nRun `conan install .. --build missing` before cmake")
    endif()
endif()

if (MSVC)
    # https://github.com/microsoft/STL/issues/1814
    set(CMAKE_CXX_STANDARD 23)
else()
    set(CMAKE_CXX_STANDARD 20)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

list(APPEND CMAKE_PREFIX_PATH ${CMAKE_BINARY_DIR})

if (NOT TARGET fmt::fmt)
    find_package(fmt REQUIRED CONFIG)
endif()
if (NOT TARGET date::date)
    find_package(date REQUIRED CONFIG)
endif()
if (TARGET json)
    set(JSON_TGT json)
elseif(TARGET mbits-json::json)
    set(JSON_TGT mbits-json::json)
elseif (NOT TARGET mbits-json::json)
    find_package(mbits-json REQUIRED CONFIG)
    set(JSON_TGT mbits-json::json)
endif()
find_package(Python3 COMPONENTS Interpreter REQUIRED)

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
    inc/movies/types.hpp
    inc/movies/opt.hpp

    src/db_info.cpp
    src/diff.cpp
    src/difflib.hpp
    src/loader.cpp
    src/movie_info/impl_array.inl
    src/movie_info/impl_translatable.inl
    src/movie_info/impl.cpp
    src/movie_info/impl.hpp
    src/movie_info/movie_info.cpp
    src/movie_info/offline_images.cpp
    src/movie_info/person_info.hpp
)

set(GEN_SRCS
    ${CMAKE_CURRENT_BINARY_DIR}/inc/movies/movie_info.hpp
    ${CMAKE_CURRENT_BINARY_DIR}/src/movie_info_generated.cpp
)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${MOVIES_SRCS})
source_group(TREE ${CMAKE_CURRENT_BINARY_DIR} FILES ${GEN_SRCS} PREFIX gen)


add_custom_command(
    OUTPUT
        ${CMAKE_CURRENT_BINARY_DIR}/inc/movies/movie_info.hpp
        ${CMAKE_CURRENT_BINARY_DIR}/src/movie_info_generated.cpp
    COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen-api.py ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/idl/movie_info.idl ${CMAKE_CURRENT_SOURCE_DIR}/idl/movie_info_cpp.idl
    DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen-api.py
        ${CMAKE_CURRENT_SOURCE_DIR}/idl/movie_info.idl
        ${CMAKE_CURRENT_SOURCE_DIR}/idl/movie_info_cpp.idl
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/widl/printers/cpp/code.py
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/widl/printers/cpp/header.py
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/widl/printers/cpp/common.py
    VERBATIM)


add_library(movies STATIC ${MOVIES_SRCS} ${GEN_SRCS})
target_include_directories(movies
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/inc"
        "${CMAKE_CURRENT_BINARY_DIR}/inc"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(movies PUBLIC ${JSON_TGT} io fmt::fmt date::date)

if (TARGET nav)
    target_link_libraries(movies PUBLIC nav)
    target_compile_definitions(movies PUBLIC MOVIES_HAS_NAVIGATOR)
elseif(TARGET tangle)
    target_link_libraries(movies PUBLIC tangle)
    target_compile_definitions(movies PUBLIC MOVIES_HAS_NAVIGATOR)
elseif(TARGET tangle::tangle)
    target_link_libraries(movies PUBLIC tangle::tangle)
    target_compile_definitions(movies PUBLIC MOVIES_HAS_NAVIGATOR)
endif()

##################################################################
##  INSTALL
##################################################################

if (MOVIES_INSTALL)
  install(TARGETS movies io)
  install(DIRECTORY inc/movies DESTINATION include)
  install(DIRECTORY inc/io DESTINATION include)
endif()
