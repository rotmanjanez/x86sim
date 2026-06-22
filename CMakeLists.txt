cmake_minimum_required(VERSION 3.20)

project(raspsim LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

cmake_host_system_information(RESULT buildhost QUERY FQDN)
include(GitRevision)

add_executable(raspsim)

file(GLOB raspsim_sources CONFIGURE_DEPENDS src/*.cpp)

target_sources(raspsim PRIVATE ${raspsim_sources})

target_include_directories(raspsim PRIVATE src)
target_compile_definitions(raspsim PRIVATE BUILDHOST=${buildhost} GIT_REVISION=${GIT_REVISION})
