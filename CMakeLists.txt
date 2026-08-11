cmake_minimum_required(VERSION 3.20)
project(soc_validation_framework VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # clang-tidy reads this

option(SOC_BUILD_TESTS   "Build GoogleTest suite" ON)
option(SOC_BUILD_PYTHON  "Build pybind11 module" OFF)
option(SOC_SANITIZE      "Enable sanitizers (address|undefined|address,undefined)" "")

# Warnings are errors. On a validation framework a sign-compare or an
# uninitialised read is not style - it is the class of bug the framework exists
# to find in someone else's code.
add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wshadow
                    -Wnon-virtual-dtor -Wold-style-cast -Werror)

if(SOC_SANITIZE)
  message(STATUS "sanitizers: ${SOC_SANITIZE}")
  add_compile_options(-fsanitize=${SOC_SANITIZE} -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=${SOC_SANITIZE})
endif()

# ---------------------------------------------------------------- code generation
# The generated header is a build artefact, not a committed file: committing it
# invites someone editing the header instead of the YAML, and then the databook
# and the code disagree.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(SOC_REGMAP    ${CMAKE_CURRENT_SOURCE_DIR}/regmaps/soc.yaml)
set(SOC_GEN_DIR   ${CMAKE_BINARY_DIR}/generated)
set(SOC_GEN_HDR   ${SOC_GEN_DIR}/soc/soc_registers.hpp)

add_custom_command(
  OUTPUT ${SOC_GEN_HDR}
  COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_registers.py
          ${SOC_REGMAP} -o ${SOC_GEN_HDR}
  DEPENDS ${SOC_REGMAP} ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_registers.py
  COMMENT "Generating C++ register layer from ${SOC_REGMAP}"
  VERBATIM)
add_custom_target(soc_generate DEPENDS ${SOC_GEN_HDR})

# ---------------------------------------------------------------------- library
add_library(soc_core
  src/emulated_device.cpp
  src/sequencer.cpp
  src/transports.cpp
  src/txn_log.cpp)
add_dependencies(soc_core soc_generate)
target_include_directories(soc_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${SOC_GEN_DIR})
find_package(Threads REQUIRED)
target_link_libraries(soc_core PUBLIC Threads::Threads)

# --------------------------------------------------------------------- benchmark
add_executable(soc_benchmark benchmarks/bench_main.cpp)
target_link_libraries(soc_benchmark PRIVATE soc_core)

# ------------------------------------------------------------------------- tests
if(SOC_BUILD_TESTS)
  find_package(GTest REQUIRED)
  enable_testing()
  add_executable(soc_tests tests/test_all.cpp)
  target_link_libraries(soc_tests PRIVATE soc_core GTest::gtest GTest::gtest_main)
  add_test(NAME soc_tests COMMAND soc_tests)
endif()

# ---------------------------------------------------------------------- bindings
if(SOC_BUILD_PYTHON)
  # pip-installed pybind11 does not land on CMake's default search path.
  execute_process(
    COMMAND ${Python3_EXECUTABLE} -m pybind11 --cmakedir
    OUTPUT_VARIABLE PYBIND11_CMAKE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  find_package(pybind11 CONFIG REQUIRED PATHS ${PYBIND11_CMAKE_DIR})
  pybind11_add_module(soc_val python/bindings.cpp)
  target_link_libraries(soc_val PRIVATE soc_core)
  # pybind11 headers trip -Wold-style-cast; not our code to fix.
  target_compile_options(soc_val PRIVATE -Wno-old-style-cast -Wno-conversion)
endif()
