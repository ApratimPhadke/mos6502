cmake_minimum_required(VERSION 3.20)

project(mos6502
  VERSION 1.0.0
  DESCRIPTION "A small, fast, well-tested NMOS 6502 CPU emulator core"
  LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(MOS6502_BUILD_TESTS "Build the unit tests"            ON)
option(MOS6502_BUILD_APPS  "Build the run6502 command line"  ON)
option(MOS6502_BUILD_BENCH "Build the benchmark"             ON)
option(MOS6502_WERROR      "Treat compiler warnings as errors" ON)
option(MOS6502_SANITIZE    "Build with AddressSanitizer + UBSan" OFF)

# Only the top-level project sets global policy; when someone else consumes us
# through add_subdirectory()/FetchContent we must not change THEIR settings.
if(PROJECT_IS_TOP_LEVEL)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
  if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
  endif()
endif()

include(cmake/Warnings.cmake)

# ---------------------------------------------------------------------------
# Core library: CPU + bus. Nothing here allocates, throws or prints.
# ---------------------------------------------------------------------------
add_library(mos6502 STATIC
  src/bus.cpp
  src/cpu.cpp)
add_library(mos6502::mos6502 ALIAS mos6502)

target_include_directories(mos6502 PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(mos6502 PUBLIC cxx_std_20)
mos6502_enable_warnings(mos6502)
mos6502_enable_sanitizers(mos6502)

# ---------------------------------------------------------------------------
# Debug tooling: disassembler + trace buffer. Allowed to use std::string.
# ---------------------------------------------------------------------------
add_library(mos6502_debug STATIC src/disasm.cpp)
add_library(mos6502::debug ALIAS mos6502_debug)
# Make the exported (installed) name match the in-tree alias: mos6502::debug
set_target_properties(mos6502_debug PROPERTIES EXPORT_NAME debug)
target_link_libraries(mos6502_debug PUBLIC mos6502)
mos6502_enable_warnings(mos6502_debug)
mos6502_enable_sanitizers(mos6502_debug)

if(MOS6502_BUILD_APPS)
  add_executable(run6502 apps/run6502.cpp)
  target_link_libraries(run6502 PRIVATE mos6502 mos6502_debug)
  mos6502_enable_warnings(run6502)
  mos6502_enable_sanitizers(run6502)
endif()

if(MOS6502_BUILD_BENCH)
  add_executable(bench_cpu bench/bench_cpu.cpp)
  target_link_libraries(bench_cpu PRIVATE mos6502)
  mos6502_enable_warnings(bench_cpu)
endif()

if(MOS6502_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# Optional: compare against the original reference emulator (see tools/).
#   cmake -S . -B build -DMOS6502_REFERENCE_DIR=<...>/6502Emulator-master/6502/6502Lib/src
set(MOS6502_REFERENCE_DIR "" CACHE PATH "Path to the reference emulator's src/ directory")
if(MOS6502_REFERENCE_DIR)
  add_subdirectory(tools/reference_compare)
endif()

# ---------------------------------------------------------------------------
# Install + package config, so other projects can do:
#   find_package(mos6502 CONFIG REQUIRED)
#   target_link_libraries(app PRIVATE mos6502::mos6502)
# ---------------------------------------------------------------------------
if(PROJECT_IS_TOP_LEVEL)
  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)

  install(TARGETS mos6502 mos6502_debug
    EXPORT mos6502Targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
  install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  if(MOS6502_BUILD_APPS)
    install(TARGETS run6502 RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  endif()

  install(EXPORT mos6502Targets
    NAMESPACE mos6502::
    FILE mos6502Targets.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mos6502)

  configure_package_config_file(
    cmake/mos6502Config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/mos6502Config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mos6502)
  write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/mos6502ConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
  install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/mos6502Config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/mos6502ConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mos6502)
endif()
