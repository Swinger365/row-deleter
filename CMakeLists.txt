cmake_minimum_required(VERSION 3.20)
project(xlsx_red_cleaner VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(RED_CLEANER_BUILD_TESTS "Build unit tests" ON)

include(FetchContent)

# ---------------------------------------------------------------------------
# Dependencies: pugixml (XML), miniz (zip/deflate)
# ---------------------------------------------------------------------------
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    pugixml
    URL https://github.com/zeux/pugixml/releases/download/v1.14/pugixml-1.14.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(pugixml)

FetchContent_Declare(
    miniz
    URL https://github.com/richgel999/miniz/releases/download/3.0.2/miniz-3.0.2.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(miniz)
add_library(miniz_amalgamated STATIC ${miniz_SOURCE_DIR}/miniz.c)
target_include_directories(miniz_amalgamated SYSTEM PUBLIC ${miniz_SOURCE_DIR})
# MINIZ_NO_TIME: miniz otherwise calls mktime()/time() per entry, which touches global tz state from worker threads.
target_compile_definitions(miniz_amalgamated PUBLIC MINIZ_NO_ZLIB_COMPATIBLE_NAMES MINIZ_NO_TIME)
set_target_properties(miniz_amalgamated PROPERTIES POSITION_INDEPENDENT_CODE ON)

find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Core library (everything except main), shared by the app and the tests
# ---------------------------------------------------------------------------
add_library(red_cleaner_core STATIC
    src/cli.cpp
    src/cleaner.cpp
    src/color.cpp
    src/formula.cpp
    src/sheet_processor.cpp
    src/styles.cpp
    src/xlsx_package.cpp
    src/xml_utils.cpp
    src/zip_archive.cpp
)
target_include_directories(red_cleaner_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(red_cleaner_core
    PUBLIC  pugixml::pugixml Threads::Threads
    PRIVATE miniz_amalgamated)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(red_cleaner_core PRIVATE -Wall -Wextra -Wpedantic)
elseif(MSVC)
    target_compile_options(red_cleaner_core PRIVATE /W4 /utf-8)
endif()

add_executable(xlsx_red_cleaner src/main.cpp)
target_link_libraries(xlsx_red_cleaner PRIVATE red_cleaner_core)
if(WIN32)
    target_link_libraries(xlsx_red_cleaner PRIVATE shell32)
endif()

# ---------------------------------------------------------------------------
# Documentation: cmake --build <dir> --target docs  ->  docs/html/index.html
# ---------------------------------------------------------------------------
find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
    add_custom_target(docs
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating Doxygen documentation"
        VERBATIM)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(RED_CLEANER_BUILD_TESTS)
    enable_testing()

    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_executable(red_cleaner_tests
        tests/test_helpers.cpp
        tests/test_cleaner.cpp
        tests/test_cli.cpp
        tests/test_color.cpp
        tests/test_formula.cpp
        tests/test_sheet_processor.cpp
        tests/test_styles.cpp
        tests/test_thread_pool.cpp
        tests/test_zip_archive.cpp
    )
    target_link_libraries(red_cleaner_tests PRIVATE red_cleaner_core GTest::gtest_main)

    include(GoogleTest)
    gtest_discover_tests(red_cleaner_tests DISCOVERY_MODE PRE_TEST)
endif()
