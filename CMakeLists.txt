cmake_minimum_required(VERSION 3.16)
project(module_context_core LANGUAGES CXX)

include(FetchContent)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    if(MSVC_VERSION GREATER_EQUAL 1910)
        add_compile_options(/utf-8)
    else()
        add_compile_options(/wd4819)
    endif()
endif()

option(MC_BUILD_TESTS "Build test targets" ON)
option(MC_BUILD_RABBITMQ_MODULE "Build RabbitMQ message bus module" ON)
option(MC_BUILD_HTTP_TRANSPORT_MODULE "Build HTTP transfer module" ON)
option(MC_BUILD_SEMIPLUGIN_MANAGER_MODULE "Build SEMI algorithm plugin manager module" ON)
option(MC_BUILD_TASK_FLOW_E2E "Build Windows task-flow end-to-end test targets" OFF)
option(MC_BUILD_TASK_FLOW_QT_DEMO "Build Qt5 task-flow master desktop demo" OFF)
set(MC_SEMIPLUGIN_VENDOR_INCLUDE_DIRS "" CACHE STRING
    "Optional semicolon-separated include directories required by SEMIPLUGIN public algorithm headers")
set(MC_TGV_ETCHING_VENDOR_LIBRARIES "" CACHE STRING
    "Optional semicolon-separated libraries required by the tgv_etching SEMIPLUGIN implementation")
set(MC_TGV_ETCHING_VENDOR_RUNTIME_DLLS "" CACHE STRING
    "Optional semicolon-separated runtime DLLs copied next to the tgv_etching SEMIPLUGIN")

set(MC_FOUNDATION_SOURCE_DIR
    ""
    CACHE PATH
    "Optional local foundation source directory. Can point to the repo root or the directory that contains CMakeLists.txt.")
set(MC_FOUNDATION_GIT_REPOSITORY
    "https://github.com/yuqing112256313-pixel/Foundation.git"
    CACHE STRING
    "Git repository used when foundation must be fetched automatically.")
set(MC_FOUNDATION_GIT_TAG
    "main"
    CACHE STRING
    "Git ref used when foundation must be fetched automatically.")
set(MC_AMQP_CPP_SOURCE_DIR
    ""
    CACHE PATH
    "Optional local AMQP-CPP source directory. Can point to the repo root or the directory that contains CMakeLists.txt.")
set(MC_AMQP_CPP_GIT_REPOSITORY
    "https://github.com/yuqing112256313-pixel/AMQP-CPP-CXX11.git"
    CACHE STRING
    "Git repository used when AMQP-CPP must be fetched automatically.")
set(MC_AMQP_CPP_GIT_TAG
    "main"
    CACHE STRING
    "Git ref used when AMQP-CPP must be fetched automatically.")
set(MC_CPP_HTTPLIB_SOURCE_DIR
    ""
    CACHE PATH
    "Optional local cpp-httplib source directory. Expected to contain httplib.h.")

function(mc_prepare_foundation_build_options)
    set(FOUNDATION_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(FOUNDATION_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(FOUNDATION_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
endfunction()

function(mc_patch_text_file file_path search_text replace_text patch_label)
    if(NOT EXISTS "${file_path}")
        return()
    endif()

    file(READ "${file_path}" _file_content)
    string(REPLACE
        "${search_text}"
        "${replace_text}"
        _patched_file_content
        "${_file_content}")

    if(NOT _patched_file_content STREQUAL _file_content)
        file(WRITE "${file_path}" "${_patched_file_content}")
        message(STATUS "${patch_label}")
    endif()
endfunction()

function(mc_normalize_foundation_source_dir input_dir output_var)
    if(NOT input_dir)
        set(${output_var} "" PARENT_SCOPE)
        return()
    endif()

    if(EXISTS "${input_dir}/CMakeLists.txt")
        set(${output_var} "${input_dir}" PARENT_SCOPE)
        return()
    endif()

    if(EXISTS "${input_dir}/foundation/CMakeLists.txt")
        set(${output_var} "${input_dir}/foundation" PARENT_SCOPE)
        return()
    endif()

    message(FATAL_ERROR
        "foundation source directory not found under '${input_dir}'. "
        "Expected either '${input_dir}/CMakeLists.txt' or "
        "'${input_dir}/foundation/CMakeLists.txt'.")
endfunction()

function(mc_ensure_foundation_dependency)
    set(MC_FOUNDATION_RESOLVED_SOURCE_DIR "" PARENT_SCOPE)

    if(TARGET foundation::foundation)
        return()
    endif()

    set(_foundation_source_dir "")

    if(MC_FOUNDATION_SOURCE_DIR)
        mc_normalize_foundation_source_dir(
            "${MC_FOUNDATION_SOURCE_DIR}"
            _foundation_source_dir)
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/Foundation/CMakeLists.txt")
        set(_foundation_source_dir
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/Foundation")
    endif()

    if(_foundation_source_dir)
        message(STATUS
            "Using foundation from local source: ${_foundation_source_dir}")
        mc_prepare_foundation_build_options()
        add_subdirectory(
            "${_foundation_source_dir}"
            "${CMAKE_CURRENT_BINARY_DIR}/_deps/foundation-build")
        set(MC_FOUNDATION_RESOLVED_SOURCE_DIR
            "${_foundation_source_dir}"
            PARENT_SCOPE)
        return()
    endif()

    find_package(foundation CONFIG QUIET)
    if(TARGET foundation::foundation)
        message(STATUS "Using installed foundation package")
        return()
    endif()

    message(STATUS
        "Fetching foundation from ${MC_FOUNDATION_GIT_REPOSITORY} "
        "(ref: ${MC_FOUNDATION_GIT_TAG})")
    mc_prepare_foundation_build_options()
    FetchContent_Declare(
        foundation
        GIT_REPOSITORY "${MC_FOUNDATION_GIT_REPOSITORY}"
        GIT_TAG "${MC_FOUNDATION_GIT_TAG}"
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(foundation)
    if(DEFINED foundation_SOURCE_DIR)
        set(MC_FOUNDATION_RESOLVED_SOURCE_DIR
            "${foundation_SOURCE_DIR}"
            PARENT_SCOPE)
    endif()
endfunction()

function(mc_patch_foundation_source_compatibility foundation_source_dir)
    if(NOT foundation_source_dir)
        return()
    endif()

    if(APPLE)
        mc_patch_text_file(
            "${foundation_source_dir}/include/foundation/base/Platform.h"
            "#if defined(__linux__)"
            "#if defined(__linux__) || defined(__APPLE__)"
            "Patched foundation Platform.h for Apple POSIX platform detection")
    endif()

    set(_plugin_loader_header
        "${foundation_source_dir}/include/foundation/plugin/PluginLoader.h")
    mc_patch_text_file(
        "${_plugin_loader_header}"
        "new_state->library.GetSymbol<"
        "new_state->library.template GetSymbol<"
        "Patched foundation PluginLoader.h for dependent-template lookup")
endfunction()

mc_ensure_foundation_dependency()
if(MC_FOUNDATION_RESOLVED_SOURCE_DIR)
    mc_patch_foundation_source_compatibility(
        "${MC_FOUNDATION_RESOLVED_SOURCE_DIR}")
endif()

if(NOT TARGET foundation::foundation)
    message(FATAL_ERROR
        "foundation dependency could not be resolved. "
        "Set MC_FOUNDATION_SOURCE_DIR to a local checkout or allow CMake "
        "to fetch ${MC_FOUNDATION_GIT_REPOSITORY}.")
endif()

add_library(mc_core_framework SHARED
    src/framework/Context.cpp
    src/framework/ModuleBase.cpp
    src/framework/ModuleManager.cpp
    src/framework/ServiceRegistry.cpp
)

set(MC_CORE_FRAMEWORK_PUBLIC_HEADERS
    include/module_context/framework/Export.h
    include/module_context/framework/IContext.h
    include/module_context/framework/IModule.h
    include/module_context/framework/IModuleManager.h
    include/module_context/framework/ModuleState.h
    include/module_context/http/IHttpTransferService.h
    include/module_context/http/Types.h
    include/module_context/messaging/IMessageBusService.h
    include/module_context/messaging/Types.h
    include/module_context/plugin/IPluginManagerService.h
    include/module_context/plugin/Types.h
)

target_compile_definitions(mc_core_framework
    PUBLIC
        MC_FRAMEWORK_SHARED_LIBRARY
    PRIVATE
        MC_FRAMEWORK_BUILDING_LIBRARY
)

target_include_directories(mc_core_framework
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/SEMIPLUGIN/api/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(MC_SEMIPLUGIN_VENDOR_INCLUDE_DIRS)
    target_include_directories(mc_core_framework
        PUBLIC
            ${MC_SEMIPLUGIN_VENDOR_INCLUDE_DIRS}
    )
endif()

target_link_libraries(mc_core_framework
    PUBLIC
        foundation::foundation
)

if(UNIX AND NOT APPLE)
    target_link_libraries(mc_core_framework PRIVATE ${CMAKE_DL_LIBS})
elseif(APPLE)
    target_link_libraries(mc_core_framework PRIVATE ${CMAKE_DL_LIBS})
endif()

install(TARGETS mc_core_framework
    EXPORT module_context_core_targets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
    FILES
        include/module_context/framework/Export.h
        include/module_context/framework/IContext.h
        include/module_context/framework/IModule.h
        include/module_context/framework/IModuleManager.h
        include/module_context/framework/ModuleState.h
    DESTINATION
        ${CMAKE_INSTALL_INCLUDEDIR}/module_context/framework
)

install(
    FILES
        include/module_context/http/IHttpTransferService.h
        include/module_context/http/Types.h
    DESTINATION
        ${CMAKE_INSTALL_INCLUDEDIR}/module_context/http
)

install(
    FILES
        include/module_context/messaging/IMessageBusService.h
        include/module_context/messaging/Types.h
    DESTINATION
        ${CMAKE_INSTALL_INCLUDEDIR}/module_context/messaging
)

install(
    FILES
        include/module_context/plugin/IPluginManagerService.h
        include/module_context/plugin/Types.h
    DESTINATION
        ${CMAKE_INSTALL_INCLUDEDIR}/module_context/plugin
)

install(
    FILES
        third_party/SEMIPLUGIN/api/include/iplugin.h
        third_party/SEMIPLUGIN/api/include/iplugin_tgv_etching.h
    DESTINATION
        ${CMAKE_INSTALL_INCLUDEDIR}
)

if((MC_BUILD_RABBITMQ_MODULE OR MC_BUILD_HTTP_TRANSPORT_MODULE OR
    MC_BUILD_SEMIPLUGIN_MANAGER_MODULE) AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/modules/CMakeLists.txt")
    add_subdirectory(modules)
endif()

if((MC_BUILD_TASK_FLOW_E2E OR MC_BUILD_TASK_FLOW_QT_DEMO) AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/CMakeLists.txt")
    add_subdirectory(examples)
endif()

if(MC_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(tests)
    endif()
endif()
