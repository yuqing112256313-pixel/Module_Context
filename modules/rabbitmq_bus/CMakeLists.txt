set(RABBITMQ_BUS_SOURCES
    src/internal/MessageBusServiceProxy.cpp
    src/RabbitMqBusModule.cpp
)

add_library(rabbitmq_bus SHARED
    ${RABBITMQ_BUS_SOURCES}
)

target_compile_definitions(rabbitmq_bus
    PRIVATE
        MC_FRAMEWORK_SHARED_LIBRARY
)

target_include_directories(rabbitmq_bus
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/internal
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_link_libraries(rabbitmq_bus
    PRIVATE
        mc_core_framework
        foundation::foundation
        amqpcpp
)

if(WIN32)
    target_link_libraries(rabbitmq_bus PRIVATE ws2_32)
    set_target_properties(rabbitmq_bus PROPERTIES PREFIX "")
    add_custom_command(TARGET rabbitmq_bus POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:mc_core_framework>
            $<TARGET_FILE_DIR:rabbitmq_bus>
    )
endif()

set_target_properties(rabbitmq_bus PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

install(TARGETS rabbitmq_bus
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/plugins
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/plugins
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}/plugins
)
