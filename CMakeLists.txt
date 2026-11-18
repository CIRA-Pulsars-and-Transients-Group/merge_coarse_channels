cmake_minimum_required(VERSION 3.16)
project(merge_coarse_channels LANGUAGES CXX)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(MERGE_NATIVE_OPTIMIZATION "Optimize Release builds for the build machine's CPU" OFF)

set(MERGE_TARGET_PLATFORM "auto" CACHE STRING "Target platform policy: auto, linux, or macos")
set_property(CACHE MERGE_TARGET_PLATFORM PROPERTY STRINGS auto linux macos)

if(MERGE_TARGET_PLATFORM STREQUAL "auto")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(merge_platform "linux")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(merge_platform "macos")
    else()
        message(FATAL_ERROR
            "Unsupported target system '${CMAKE_SYSTEM_NAME}'. Set MERGE_TARGET_PLATFORM to linux or macos to override.")
    endif()
elseif(MERGE_TARGET_PLATFORM STREQUAL "linux" OR MERGE_TARGET_PLATFORM STREQUAL "macos")
    set(merge_platform "${MERGE_TARGET_PLATFORM}")
else()
    message(FATAL_ERROR "MERGE_TARGET_PLATFORM must be auto, linux, or macos")
endif()

if(merge_platform STREQUAL "linux")
    set(merge_fadvise_default ON)
else()
    set(merge_fadvise_default OFF)
endif()
option(MERGE_USE_POSIX_FADVISE "Enable POSIX sequential-read advice" ${merge_fadvise_default})

if(MERGE_USE_POSIX_FADVISE)
    include(CheckSymbolExists)
    check_symbol_exists(posix_fadvise "fcntl.h" MERGE_HAS_POSIX_FADVISE)
    check_symbol_exists(POSIX_FADV_SEQUENTIAL "fcntl.h" MERGE_HAS_POSIX_FADV_SEQUENTIAL)
    if(NOT MERGE_HAS_POSIX_FADVISE OR NOT MERGE_HAS_POSIX_FADV_SEQUENTIAL)
        message(FATAL_ERROR
            "Sequential-read advice is not available for this target. Configure with -DMERGE_USE_POSIX_FADVISE=OFF.")
    endif()
endif()

message(STATUS "Merge platform: ${merge_platform}; POSIX sequential-read advice: ${MERGE_USE_POSIX_FADVISE}")

add_executable(merge_coarse_channels
    src/merge_coarse_channels.cpp
    src/SigprocFile.cpp
    src/DataSource.cpp
    src/FileSplitter.cpp
)
target_compile_features(merge_coarse_channels PRIVATE cxx_std_20)
set_target_properties(merge_coarse_channels PROPERTIES CXX_EXTENSIONS OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(merge_coarse_channels PRIVATE _FILE_OFFSET_BITS=64)
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT merge_ipo_supported OUTPUT merge_ipo_error LANGUAGES CXX)
if(merge_ipo_supported)
    set_property(TARGET merge_coarse_channels PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    message(STATUS "Release link-time optimization enabled")
else()
    message(STATUS "Release link-time optimization unavailable: ${merge_ipo_error}")
endif()

if(MERGE_NATIVE_OPTIMIZATION)
    if(CMAKE_CROSSCOMPILING)
        message(FATAL_ERROR "MERGE_NATIVE_OPTIMIZATION cannot be used when cross-compiling")
    endif()
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("-march=native" merge_has_march_native)
    if(merge_has_march_native)
        set(merge_native_flag "-march=native")
    else()
        check_cxx_compiler_flag("-mcpu=native" merge_has_mcpu_native)
        if(NOT merge_has_mcpu_native)
            message(FATAL_ERROR "The C++ compiler does not support native CPU optimization")
        endif()
        set(merge_native_flag "-mcpu=native")
    endif()
    target_compile_options(merge_coarse_channels PRIVATE "$<$<CONFIG:Release>:${merge_native_flag}>")
    target_link_options(merge_coarse_channels PRIVATE "$<$<CONFIG:Release>:${merge_native_flag}>")
    message(STATUS "Release native CPU optimization: ${merge_native_flag}")
endif()

if(MERGE_USE_POSIX_FADVISE)
    target_compile_definitions(merge_coarse_channels PRIVATE HAVE_POSIX_FADVISE)
endif()

install(TARGETS merge_coarse_channels RUNTIME DESTINATION bin)

include(CTest)
if(BUILD_TESTING)
    # FileSplitter unit tests
    add_executable(test_filesplitter tests/test_filesplitter.cpp src/FileSplitter.cpp)
    target_compile_features(test_filesplitter PRIVATE cxx_std_20)
    set_target_properties(test_filesplitter PROPERTIES CXX_EXTENSIONS OFF)
    target_include_directories(test_filesplitter PRIVATE src)
    add_test(NAME filesplitter COMMAND test_filesplitter)

    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_executable(test_large_file tests/test_large_file.cpp src/DataSource.cpp)
        target_compile_features(test_large_file PRIVATE cxx_std_20)
        set_target_properties(test_large_file PROPERTIES CXX_EXTENSIONS OFF)
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            target_compile_definitions(test_large_file PRIVATE _FILE_OFFSET_BITS=64)
        endif()
        if(MERGE_USE_POSIX_FADVISE)
            target_compile_definitions(test_large_file PRIVATE HAVE_POSIX_FADVISE)
        endif()
        add_test(NAME merge_integration
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_merge.py"
                    "$<TARGET_FILE:merge_coarse_channels>" "$<TARGET_FILE:test_large_file>")
    endif()
endif()
