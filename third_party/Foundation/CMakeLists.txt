cmake_minimum_required(VERSION 3.10)
project(foundation VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Options ----
option(FOUNDATION_BUILD_SHARED   "Build as shared library"  OFF)
option(FOUNDATION_BUILD_TESTS    "Build tests"              ON)
option(FOUNDATION_BUILD_EXAMPLES "Build examples"           ON)

# ---- Library type ----
if(FOUNDATION_BUILD_SHARED)
    set(LIB_TYPE SHARED)
else()
    set(LIB_TYPE STATIC)
endif()

# ---- Sources ----
set(FOUNDATION_SOURCES
    src/base/Assert.cpp
    src/base/ErrorCode.cpp
    src/log/Logger.cpp
    src/strings/StringUtils.cpp
    src/filesystem/PathUtils.cpp
    src/filesystem/FileUtils.cpp
    src/time/TimeUtils.cpp
    src/config/ConfigValue.cpp
    src/config/ConfigReader.cpp
    src/concurrent/ThreadPool.cpp
    src/perf/ScopedTimer.cpp
    src/perf/PerfStats.cpp
    src/plugin/DynamicLibrary.cpp
)

# ---- Library target ----
add_library(foundation ${LIB_TYPE} ${FOUNDATION_SOURCES})
add_library(foundation::foundation ALIAS foundation)

target_include_directories(foundation
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_definitions(foundation PRIVATE FOUNDATION_BUILDING_LIBRARY)
if(FOUNDATION_BUILD_SHARED)
    target_compile_definitions(foundation PUBLIC FOUNDATION_SHARED_LIBRARY)
    set_target_properties(foundation PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
endif()

if(MSVC)
    add_compile_options(/utf-8)
endif()

if(MSVC)
    target_compile_options(foundation PRIVATE /W4 /EHsc)
else()
    target_compile_options(foundation PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(foundation PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

# ---- Platform link dependencies ----
find_package(Threads REQUIRED)
target_link_libraries(foundation PUBLIC Threads::Threads)
if(UNIX AND NOT APPLE)
    target_link_libraries(foundation PRIVATE dl rt)
endif()
if(WIN32)
    target_link_libraries(foundation PRIVATE ws2_32)
endif()

# ---- Install ----
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS foundation
    EXPORT foundationTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/foundation DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT foundationTargets
    FILE foundationTargets.cmake
    NAMESPACE foundation::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/foundation
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/foundationConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/foundationConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/foundation
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/foundationConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/foundationConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/foundationConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/foundation
)

# ---- Sub-projects ----
if(FOUNDATION_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(FOUNDATION_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
