# Builds AMQP-CPP
#
# Options:
#
# - AMQP-CPP_BUILD_SHARED (default OFF)
#       ON:  Build shared lib
#       OFF: Build static lib
#
# - AMQP-CPP_LINUX_TCP (default OFF)
#       ON:  Build posix handler implementation
#       OFF: Don't build posix handler implementation

cmake_minimum_required(VERSION 3.4 FATAL_ERROR)

# project name
project(amqpcpp)
set (VERSION_MAJOR 4)
set (VERSION_MINOR 3)
set (VERSION_PATCH 19)
set (SO_VERSION ${VERSION_MAJOR}.${VERSION_MINOR})

# build options
option(AMQP-CPP_BUILD_SHARED "Build shared library. If off, build will be static." OFF)
option(AMQP-CPP_LINUX_TCP "Build linux sockets implementation." OFF)
option(AMQP-CPP_BUILD_EXAMPLES "Build amqpcpp examples" OFF)

if(WIN32 AND AMQP-CPP_LINUX_TCP)
    message(FATAL_ERROR "AMQP-CPP_LINUX_TCP requires a POSIX platform and cannot be enabled on Windows.")
endif()

# ensure c++11 on all compilers
set (CMAKE_CXX_STANDARD 11)

# add source files
# ------------------------------------------------------------------------------------------------------

# set include/ as include directory
include_directories(SYSTEM ${CMAKE_CURRENT_SOURCE_DIR}/include)

# macro that adds a list of provided source files to a list called SRCS.
# if variable SRCS does not yet exist, it is created.
macro (add_sources)
    file (RELATIVE_PATH _relPath "${PROJECT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
    foreach (_src ${ARGN})
        if (_relPath)
            list (APPEND SRCS "${_relPath}/${_src}")
        else()
            list (APPEND SRCS "${_src}")
        endif()
    endforeach()
    if (_relPath)
        # propagate SRCS to parent directory
        set (SRCS ${SRCS} PARENT_SCOPE)
    endif()
endmacro()

# add source files
file(GLOB src_MAIN CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
set(src_LINUX_TCP)
if(AMQP-CPP_LINUX_TCP)
    file(GLOB src_LINUX_TCP CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/linux_tcp/*.cpp")
endif()

file(GLOB AMQP_CPP_CORE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/include/amqpcpp/*.h")
list(REMOVE_ITEM AMQP_CPP_CORE_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/include/amqpcpp/linux_tcp.h")
list(REMOVE_ITEM AMQP_CPP_CORE_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/amqpcpp/libboostasio.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/amqpcpp/libev.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/amqpcpp/libevent.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/amqpcpp/libuv.h"
)

set(AMQP_CPP_PLATFORM_ROOT_HEADERS)
set(AMQP_CPP_PLATFORM_TCP_HEADERS)
if(AMQP-CPP_LINUX_TCP)
    list(APPEND AMQP_CPP_PLATFORM_ROOT_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/include/amqpcpp/linux_tcp.h")
    list(APPEND AMQP_CPP_PLATFORM_ROOT_HEADERS
        "${CMAKE_CURRENT_SOURCE_DIR}/include/amqpcpp/libboostasio.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/amqpcpp/libev.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/amqpcpp/libevent.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/amqpcpp/libuv.h"
    )
    file(GLOB AMQP_CPP_LINUX_TCP_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/include/amqpcpp/linux_tcp/*.h")
    list(APPEND AMQP_CPP_PLATFORM_TCP_HEADERS ${AMQP_CPP_LINUX_TCP_HEADERS})
endif()

# potentially build the examples
if(AMQP-CPP_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# settings for specific compilers
# ------------------------------------------------------------------------------------------------------

# we have to prevent windows from defining the max macro.
if (WIN32)
    add_definitions(-DNOMINMAX)
endif()

# build targets
# ------------------------------------------------------------------------------------------------------

# set output directory
set(LIBRARY_OUTPUT_PATH ${CMAKE_CURRENT_BINARY_DIR}/bin)

if(AMQP-CPP_BUILD_SHARED)
    # create shared lib
    #add_library(${PROJECT_NAME} SHARED ${SRCS})
    add_library(${PROJECT_NAME} SHARED ${src_MAIN} ${src_LINUX_TCP})
    # set shared lib version
    set_target_properties(${PROJECT_NAME} PROPERTIES SOVERSION ${SO_VERSION})
    if(WIN32)
        # ensure symbols are exported from the DLL and an import library is generated
        set_target_properties(${PROJECT_NAME} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    endif()
else()
    # create static lib
    #add_library(${PROJECT_NAME} STATIC ${SRCS})
    add_library(${PROJECT_NAME} STATIC ${src_MAIN} ${src_LINUX_TCP})
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES DEBUG_POSTFIX "d")

# install rules
# ------------------------------------------------------------------------------------------------------

if(AMQP-CPP_BUILD_SHARED)
    # copy shared lib and its static counter part
    install(TARGETS ${PROJECT_NAME} EXPORT ${PROJECT_NAME}Config
            ARCHIVE DESTINATION lib
            LIBRARY DESTINATION lib
            RUNTIME DESTINATION bin
    )
else()
    # copy static lib
    install(TARGETS ${PROJECT_NAME} EXPORT ${PROJECT_NAME}Config
            ARCHIVE DESTINATION lib
    )
endif()

# copy header files
install(FILES ${AMQP_CPP_CORE_HEADERS} DESTINATION include/amqpcpp)
if(AMQP_CPP_PLATFORM_ROOT_HEADERS)
    install(FILES ${AMQP_CPP_PLATFORM_ROOT_HEADERS} DESTINATION include/amqpcpp)
endif()
if(AMQP_CPP_PLATFORM_TCP_HEADERS)
    install(FILES ${AMQP_CPP_PLATFORM_TCP_HEADERS} DESTINATION include/amqpcpp/linux_tcp)
endif()
install(FILES include/amqpcpp.h DESTINATION include)

install(EXPORT ${PROJECT_NAME}Config DESTINATION cmake)
export(TARGETS ${PROJECT_NAME} FILE ${PROJECT_NAME}Config.cmake)

set(DEST_DIR "${CMAKE_INSTALL_PREFIX}")
set(PRIVATE_LIBS "-llibamqpcc")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/amqpcpp.pc.in"
               "${CMAKE_CURRENT_BINARY_DIR}/amqpcpp.pc" @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/amqpcpp.pc" DESTINATION lib/pkgconfig)

# submodule support
# ------------------------------------------------------------------------------------------------------
target_include_directories(${PROJECT_NAME} SYSTEM PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/>
    $<INSTALL_INTERFACE:include/>
)

if(AMQP-CPP_LINUX_TCP)
    target_link_libraries(${PROJECT_NAME} ${CMAKE_DL_LIBS})
    # Find OpenSSL and provide include dirs
    find_package(OpenSSL REQUIRED)
    target_include_directories(${PROJECT_NAME} PRIVATE ${OPENSSL_INCLUDE_DIR})
endif()

# package generation (cpack)
# ------------------------------------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "amqpcpp")
set(CPACK_PACKAGE_VENDOR "Copernica")
set(CPACK_PACKAGE_VERSION_MAJOR "${VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${VERSION_PATCH}")

if(WIN32)
    set(CPACK_GENERATOR "ZIP")
else()
    set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)
