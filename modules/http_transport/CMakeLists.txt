set(HTTP_TRANSPORT_SOURCES
    src/HttpTransportModule.cpp
)

add_library(http_transport SHARED
    ${HTTP_TRANSPORT_SOURCES}
)

target_compile_definitions(http_transport
    PRIVATE
        MC_FRAMEWORK_SHARED_LIBRARY
        _WIN32_WINNT=0x0A00
)

target_include_directories(http_transport
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../src
        ${MC_CPP_HTTPLIB_RESOLVED_SOURCE_DIR}
)

target_link_libraries(http_transport
    PRIVATE
        mc_core_framework
        foundation::foundation
)

if(WIN32)
    target_link_libraries(http_transport PRIVATE ws2_32)
    set_target_properties(http_transport PROPERTIES PREFIX "")
    add_custom_command(TARGET http_transport POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:mc_core_framework>
            $<TARGET_FILE_DIR:http_transport>
    )
endif()

set_target_properties(http_transport PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

install(TARGETS http_transport
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/plugins
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/plugins
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}/plugins
)
