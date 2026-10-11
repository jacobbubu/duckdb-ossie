cmake_minimum_required(VERSION 3.5)

# Set extension name here
set(TARGET_NAME ossie)

# DuckDB's extension distribution supports vcpkg: add dependencies to ./vcpkg.json and pull them in here with
# find_package. duckdb_ossie deliberately uses none. JSON parsing uses the yyjson DuckDB already vendors, and
# YAML support comes from fkYAML, pinned as a submodule (third_party/fkYAML) exactly the way duckdb and
# extension-ci-tools are. Keeping vcpkg off the critical path matters here: the code-quality workflow does not
# set up vcpkg at all, so a find_package dependency breaks the tidy job.

set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(${TARGET_NAME})

set(CMAKE_CXX_STANDARD "17" CACHE STRING "C++ standard to enforce")
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(src/include)
include_directories(third_party/fkYAML/include)

set(EXTENSION_SOURCES
    src/ossie_extension.cpp
    src/model/parser.cpp
    src/model/validate.cpp
    src/model/graph.cpp
    src/model/catalog.cpp
    src/model/dialect.cpp
    src/model/yaml.cpp
    src/compile/compile.cpp
    src/functions/load.cpp
    src/functions/compile.cpp
    src/functions/describe.cpp
    src/functions/query.cpp
    src/population/population.cpp
    src/population/relation_program.cpp)

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")

# A controlled cross-connection/state contract test, independent of DuckDB's
# ENABLE_UNITTEST_CPP_TESTS (extension-ci-tools defaults that option to FALSE).
if(BUILD_UNITTESTS AND NOT EXTENSION_TESTS_ONLY)
  add_executable(ossie_state_test EXCLUDE_FROM_ALL test/cpp/state_snapshot_test.cpp)
  target_link_libraries(ossie_state_test duckdb_static)
  link_extension_libraries(ossie_state_test "")
  add_executable(ossie_population_capture_test EXCLUDE_FROM_ALL test/cpp/population_capture_test.cpp)
  target_link_libraries(ossie_population_capture_test duckdb_static)
  link_extension_libraries(ossie_population_capture_test "")
  add_executable(ossie_metric_grain_test EXCLUDE_FROM_ALL test/cpp/metric_grain_test.cpp)
  target_link_libraries(ossie_metric_grain_test duckdb_static)
  link_extension_libraries(ossie_metric_grain_test "")
  add_executable(ossie_relation_program_test EXCLUDE_FROM_ALL
                 test/cpp/relation_program_test.cpp src/population/relation_program.cpp)
  target_link_libraries(ossie_relation_program_test duckdb_static)
  link_extension_libraries(ossie_relation_program_test "")

  # The upstream Windows workflow initializes MSVC only in its build step.
  # Compile these executables there; the later test step runs without vcvars.
  # Other toolchains keep the explicit build performed by our test Make targets.
  if(MSVC)
    set_target_properties(ossie_state_test ossie_population_capture_test ossie_metric_grain_test ossie_relation_program_test
                          PROPERTIES EXCLUDE_FROM_ALL FALSE)
  endif()
endif()
