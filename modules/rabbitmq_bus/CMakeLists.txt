set(AMQP_BUS_SOURCES
    src/internal/MessageBusServiceProxy.cpp
    src/RabbitMqBusModule.cpp
)

function(mc_add_amqp_bus_plugin target_name)
    add_library(${target_name} SHARED
        ${AMQP_BUS_SOURCES}
    )

    target_compile_definitions(${target_name}
        PRIVATE
            MC_FRAMEWORK_SHARED_LIBRARY
    )

    target_include_directories(${target_name}
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/src/internal
            ${CMAKE_CURRENT_SOURCE_DIR}/../../include
            ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    )

    target_link_libraries(${target_name}
        PRIVATE
            mc_core_framework
            foundation::foundation
            amqpcpp
    )

    if(WIN32)
        target_link_libraries(${target_name} PRIVATE ws2_32)
        set_target_properties(${target_name} PROPERTIES PREFIX "")
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:mc_core_framework>
                $<TARGET_FILE_DIR:${target_name}>
        )
    endif()

    set_target_properties(${target_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )

    install(TARGETS ${target_name}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/plugins
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/plugins
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}/plugins
    )
endfunction()

if(MC_BUILD_AMQP_BUS_MODULE)
    mc_add_amqp_bus_plugin(amqp_bus)
endif()

# 兼容既有脚本和部署包。该 DLL 内声明的标准 ModuleType 已迁移为 amqp_bus，
# rabbitmq_bus 只作为旧配置 type 的兼容别名保留。
if(MC_BUILD_RABBITMQ_MODULE)
    mc_add_amqp_bus_plugin(rabbitmq_bus)
endif()
