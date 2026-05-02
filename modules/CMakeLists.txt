include(FetchContent)

if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.0)
    if(NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM OR
       CMAKE_POLICY_VERSION_MINIMUM VERSION_LESS 3.5)
        set(CMAKE_POLICY_VERSION_MINIMUM
            "3.5"
            CACHE STRING
            "Minimum policy version for legacy third-party CMake projects."
            FORCE)
        message(STATUS
            "Set CMAKE_POLICY_VERSION_MINIMUM=3.5 for AMQP-CPP compatibility")
    endif()
endif()

function(mc_prepare_amqp_build_options)
    set(AMQP-CPP_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(AMQP-CPP_LINUX_TCP OFF CACHE BOOL "" FORCE)
    set(AMQP-CPP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
endfunction()

function(mc_normalize_amqp_source_dir input_dir output_var)
    if(NOT input_dir)
        set(${output_var} "" PARENT_SCOPE)
        return()
    endif()

    if(EXISTS "${input_dir}/CMakeLists.txt")
        set(${output_var} "${input_dir}" PARENT_SCOPE)
        return()
    endif()

    if(EXISTS "${input_dir}/AMQP-CPP-CXX11/CMakeLists.txt")
        set(${output_var} "${input_dir}/AMQP-CPP-CXX11" PARENT_SCOPE)
        return()
    endif()

    message(FATAL_ERROR
        "AMQP-CPP source directory not found under '${input_dir}'. "
        "Expected either '${input_dir}/CMakeLists.txt' or "
        "'${input_dir}/AMQP-CPP-CXX11/CMakeLists.txt'.")
endfunction()

function(mc_ensure_amqp_dependency)
    if(TARGET amqpcpp)
        return()
    endif()

    set(_amqp_source_dir "")

    if(MC_AMQP_CPP_SOURCE_DIR)
        mc_normalize_amqp_source_dir(
            "${MC_AMQP_CPP_SOURCE_DIR}"
            _amqp_source_dir)
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/AMQP-CPP-CXX11/CMakeLists.txt")
        set(_amqp_source_dir
            "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/AMQP-CPP-CXX11")
    endif()

    if(_amqp_source_dir)
        message(STATUS "Using AMQP-CPP from local source: ${_amqp_source_dir}")
        mc_prepare_amqp_build_options()
        add_subdirectory(
            "${_amqp_source_dir}"
            "${CMAKE_CURRENT_BINARY_DIR}/_deps/amqpcpp-build")
        return()
    endif()

    find_package(amqpcpp CONFIG QUIET)
    if(TARGET amqpcpp)
        message(STATUS "Using installed AMQP-CPP package")
        return()
    endif()

    message(STATUS
        "Fetching AMQP-CPP from ${MC_AMQP_CPP_GIT_REPOSITORY} "
        "(ref: ${MC_AMQP_CPP_GIT_TAG})")
    mc_prepare_amqp_build_options()
    FetchContent_Declare(
        amqpcpp
        GIT_REPOSITORY "${MC_AMQP_CPP_GIT_REPOSITORY}"
        GIT_TAG "${MC_AMQP_CPP_GIT_TAG}"
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(amqpcpp)
endfunction()

if(MC_BUILD_AMQP_BUS_MODULE OR MC_BUILD_RABBITMQ_MODULE)
    mc_ensure_amqp_dependency()

    if(NOT TARGET amqpcpp)
        message(FATAL_ERROR
            "AMQP-CPP dependency could not be resolved. "
            "Set MC_AMQP_CPP_SOURCE_DIR to a local checkout or allow CMake to "
            "fetch ${MC_AMQP_CPP_GIT_REPOSITORY}.")
    endif()

    add_subdirectory(rabbitmq_bus)
endif()

if(MC_BUILD_HTTP_TRANSPORT_MODULE)
    set(_cpp_httplib_source_dir "")
    if(MC_CPP_HTTPLIB_SOURCE_DIR)
        set(_cpp_httplib_source_dir "${MC_CPP_HTTPLIB_SOURCE_DIR}")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/cpp-httplib/httplib.h")
        set(_cpp_httplib_source_dir
            "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/cpp-httplib")
    endif()

    if(NOT _cpp_httplib_source_dir OR
       NOT EXISTS "${_cpp_httplib_source_dir}/httplib.h")
        message(FATAL_ERROR
            "cpp-httplib dependency could not be resolved. "
            "Set MC_CPP_HTTPLIB_SOURCE_DIR or place httplib.h under "
            "third_party/cpp-httplib.")
    endif()

    set(MC_CPP_HTTPLIB_RESOLVED_SOURCE_DIR
        "${_cpp_httplib_source_dir}"
        CACHE INTERNAL
        "Resolved cpp-httplib source directory")
    message(STATUS
        "Using cpp-httplib from local source: ${MC_CPP_HTTPLIB_RESOLVED_SOURCE_DIR}")
    add_subdirectory(http_transport)
endif()

if(MC_BUILD_SEMIPLUGIN_MANAGER_MODULE)
    add_subdirectory(semiplugin_manager)
endif()
