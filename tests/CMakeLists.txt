function(mc_configure_test_target target)
    target_include_directories(${target}
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../include
            ${CMAKE_CURRENT_SOURCE_DIR}/../src
    )

    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )

    if(WIN32 AND TARGET mc_core_framework)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:mc_core_framework>
                $<TARGET_FILE_DIR:${target}>
        )
    endif()
endfunction()

add_executable(mc_module_base_lifecycle_test
    module_base_lifecycle_test.cpp
    ../src/framework/ModuleBase.cpp
)

mc_configure_test_target(mc_module_base_lifecycle_test)

target_link_libraries(mc_module_base_lifecycle_test
    PRIVATE
        foundation::foundation
)

add_library(mc_test_config_plugin SHARED
    module_manager_config_test_plugin.cpp
)

mc_configure_test_target(mc_test_config_plugin)

target_link_libraries(mc_test_config_plugin
    PRIVATE
        mc_core_framework
        foundation::foundation
)

add_executable(mc_module_manager_config_test
    module_manager_config_test.cpp
)

mc_configure_test_target(mc_module_manager_config_test)

target_link_libraries(mc_module_manager_config_test
    PRIVATE
        mc_core_framework
        foundation::foundation
)

target_compile_definitions(mc_module_manager_config_test
    PRIVATE
        MC_TEST_BINARY_DIR="${CMAKE_CURRENT_BINARY_DIR}"
        MC_TEST_CONFIG_PLUGIN_PATH="$<TARGET_FILE:mc_test_config_plugin>"
)

add_test(
    NAME mc_module_base_lifecycle_test
    COMMAND mc_module_base_lifecycle_test
)

add_test(
    NAME mc_module_manager_config_test
    COMMAND mc_module_manager_config_test
)

if(TARGET rabbitmq_bus)
    add_executable(mc_rabbitmq_bus_module_test
        rabbitmq_bus_module_test.cpp
        ../modules/rabbitmq_bus/src/internal/MessageBusServiceProxy.cpp
        ../modules/rabbitmq_bus/src/RabbitMqBusModule.cpp
    )

    mc_configure_test_target(mc_rabbitmq_bus_module_test)

    target_include_directories(mc_rabbitmq_bus_module_test
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../modules/rabbitmq_bus/src
            ${CMAKE_CURRENT_SOURCE_DIR}/../modules/rabbitmq_bus/src/internal
    )

    target_link_libraries(mc_rabbitmq_bus_module_test
        PRIVATE
            amqpcpp
            mc_core_framework
            foundation::foundation
    )

    if(WIN32)
        target_link_libraries(mc_rabbitmq_bus_module_test PRIVATE ws2_32)
    endif()

    add_executable(mc_rabbitmq_bus_plugin_integration_test
        rabbitmq_bus_plugin_integration_test.cpp
    )

    mc_configure_test_target(mc_rabbitmq_bus_plugin_integration_test)

    target_link_libraries(mc_rabbitmq_bus_plugin_integration_test
        PRIVATE
            mc_core_framework
            foundation::foundation
    )

    target_compile_definitions(mc_rabbitmq_bus_plugin_integration_test
        PRIVATE
            MC_TEST_RABBITMQ_BUS_PLUGIN_PATH="$<TARGET_FILE:rabbitmq_bus>"
    )

    add_test(
        NAME mc_rabbitmq_bus_module_test
        COMMAND mc_rabbitmq_bus_module_test
    )

    add_test(
        NAME mc_rabbitmq_bus_plugin_integration_test
        COMMAND mc_rabbitmq_bus_plugin_integration_test
    )

    if(MC_BUILD_TASK_FLOW_E2E AND WIN32)
        add_test(
            NAME mc_rabbitmq_task_flow_e2e
            COMMAND powershell
                -ExecutionPolicy Bypass
                -File ${CMAKE_CURRENT_SOURCE_DIR}/../examples/task_flow/run_task_flow_e2e.ps1
                -BuildDir ${CMAKE_BINARY_DIR}
                -Configuration $<CONFIG>
        )
    endif()
endif()

if(TARGET http_transport)
    add_executable(mc_http_transport_module_test
        http_transport_module_test.cpp
        ../modules/http_transport/src/HttpTransportModule.cpp
    )

    mc_configure_test_target(mc_http_transport_module_test)

    target_include_directories(mc_http_transport_module_test
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../modules/http_transport/src
            ${MC_CPP_HTTPLIB_RESOLVED_SOURCE_DIR}
    )

    target_compile_definitions(mc_http_transport_module_test
        PRIVATE
            _WIN32_WINNT=0x0A00
    )

    target_link_libraries(mc_http_transport_module_test
        PRIVATE
            mc_core_framework
            foundation::foundation
    )

    if(WIN32)
        target_link_libraries(mc_http_transport_module_test PRIVATE ws2_32)
    endif()

    add_executable(mc_http_transport_plugin_integration_test
        http_transport_plugin_integration_test.cpp
    )

    mc_configure_test_target(mc_http_transport_plugin_integration_test)

    target_link_libraries(mc_http_transport_plugin_integration_test
        PRIVATE
            mc_core_framework
            foundation::foundation
    )

    target_compile_definitions(mc_http_transport_plugin_integration_test
        PRIVATE
            MC_TEST_HTTP_TRANSPORT_PLUGIN_PATH="$<TARGET_FILE:http_transport>"
    )

    add_test(
        NAME mc_http_transport_module_test
        COMMAND mc_http_transport_module_test
    )

    add_test(
        NAME mc_http_transport_plugin_integration_test
        COMMAND mc_http_transport_plugin_integration_test
    )
endif()

if(TARGET semiplugin_manager)
    add_library(mc_test_tgv_etching_semiplugin SHARED
        ../third_party/SEMIPLUGIN/modules/tgv_etching/src/plugin_tgv_etching_impl.cpp
    )

    target_compile_definitions(mc_test_tgv_etching_semiplugin
        PRIVATE
            TGV_ETCHING_IMPL_EXPORTS
    )

    target_include_directories(mc_test_tgv_etching_semiplugin
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/SEMIPLUGIN/api/include
            ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/SEMIPLUGIN/modules/tgv_etching/include
    )

    if(MC_SEMIPLUGIN_VENDOR_INCLUDE_DIRS)
        target_include_directories(mc_test_tgv_etching_semiplugin
            PRIVATE
                ${MC_SEMIPLUGIN_VENDOR_INCLUDE_DIRS}
        )
    endif()

    if(MC_TGV_ETCHING_VENDOR_LIBRARIES)
        target_link_libraries(mc_test_tgv_etching_semiplugin
            PRIVATE
                ${MC_TGV_ETCHING_VENDOR_LIBRARIES}
        )
    endif()

    set_target_properties(mc_test_tgv_etching_semiplugin PROPERTIES
        PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )

    if(WIN32 AND MC_TGV_ETCHING_VENDOR_RUNTIME_DLLS)
        foreach(_vendor_dll IN LISTS MC_TGV_ETCHING_VENDOR_RUNTIME_DLLS)
            add_custom_command(TARGET mc_test_tgv_etching_semiplugin POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${_vendor_dll}
                    $<TARGET_FILE_DIR:mc_test_tgv_etching_semiplugin>
            )
        endforeach()
    endif()

    add_executable(mc_semiplugin_manager_unit_test
        semiplugin_manager_unit_test.cpp
        ../modules/semiplugin_manager/src/internal/PluginLoader.cpp
        ../modules/semiplugin_manager/src/internal/PluginRegistry.cpp
        ../modules/semiplugin_manager/src/SemipluginManagerModule.cpp
    )

    mc_configure_test_target(mc_semiplugin_manager_unit_test)

    target_include_directories(mc_semiplugin_manager_unit_test
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../modules/semiplugin_manager/src
            ${CMAKE_CURRENT_SOURCE_DIR}/../modules/semiplugin_manager/src/internal
            ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/SEMIPLUGIN/api/include
    )

    target_link_libraries(mc_semiplugin_manager_unit_test
        PRIVATE
            mc_core_framework
            foundation::foundation
    )

    if(NOT WIN32)
        target_link_libraries(mc_semiplugin_manager_unit_test PRIVATE ${CMAKE_DL_LIBS})
    endif()

    add_executable(mc_semiplugin_manager_plugin_integration_test
        semiplugin_manager_plugin_integration_test.cpp
    )

    mc_configure_test_target(mc_semiplugin_manager_plugin_integration_test)

    target_include_directories(mc_semiplugin_manager_plugin_integration_test
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/SEMIPLUGIN/api/include
    )

    if(MC_SEMIPLUGIN_VENDOR_INCLUDE_DIRS)
        target_include_directories(mc_semiplugin_manager_plugin_integration_test
            PRIVATE
                ${MC_SEMIPLUGIN_VENDOR_INCLUDE_DIRS}
        )
    endif()

    target_link_libraries(mc_semiplugin_manager_plugin_integration_test
        PRIVATE
            mc_core_framework
            foundation::foundation
    )

    target_compile_definitions(mc_semiplugin_manager_plugin_integration_test
        PRIVATE
            MC_TEST_BINARY_DIR="${CMAKE_CURRENT_BINARY_DIR}"
            MC_TEST_SEMIPLUGIN_MANAGER_PLUGIN_PATH="$<TARGET_FILE:semiplugin_manager>"
            MC_TEST_TGV_ETCHING_PLUGIN_PATH="$<TARGET_FILE:mc_test_tgv_etching_semiplugin>"
    )

    add_test(
        NAME mc_semiplugin_manager_unit_test
        COMMAND mc_semiplugin_manager_unit_test
    )

    add_test(
        NAME mc_semiplugin_manager_plugin_integration_test
        COMMAND mc_semiplugin_manager_plugin_integration_test
    )
endif()
