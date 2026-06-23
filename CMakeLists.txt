cmake_minimum_required(VERSION 3.25)

project(raspsim
  DESCRIPTION "Cycle-accurate x86-64 virtual CPU core and RASPsim command-line simulator"
  LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

cmake_host_system_information(RESULT buildhost QUERY FQDN)
include(GNUInstallDirs)
include(GitRevision)

file(GLOB vcore_sources CONFIGURE_DEPENDS src/vcore/*.cpp)

set(build_info_definitions BUILDHOST=${buildhost} GIT_REVISION=${GIT_REVISION})

add_library(vcore)
add_library(vcore::vcore ALIAS vcore)
target_sources(vcore
  PRIVATE
    ${vcore_sources}
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS include
    FILES
      include/vcore/logging.h
      include/vcore/vcore.hpp
)
target_include_directories(vcore
  PRIVATE
    src/vcore
)
target_compile_definitions(vcore PRIVATE ${build_info_definitions})

add_library(vcore_defaults)
add_library(vcore::defaults ALIAS vcore_defaults)
target_sources(vcore_defaults
  PRIVATE
    src/vcore-support/raspsim-defaults.cpp
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS include
    FILES
      include/vcore-support/defaults.hpp
)
target_link_libraries(vcore_defaults PUBLIC vcore::vcore)
set_target_properties(vcore_defaults PROPERTIES EXPORT_NAME defaults)

add_executable(raspsim
  src/raspsim/config.cpp
  src/raspsim/raspsim.cpp
)
target_link_libraries(raspsim PRIVATE vcore::defaults)
target_include_directories(raspsim PRIVATE src/vcore)
target_compile_definitions(raspsim PRIVATE ${build_info_definitions})

install(TARGETS vcore vcore_defaults raspsim
  EXPORT vcoreTargets
  FILE_SET HEADERS
)
install(EXPORT vcoreTargets
  NAMESPACE vcore::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/vcore"
)
