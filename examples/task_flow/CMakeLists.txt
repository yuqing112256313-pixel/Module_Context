function(mc_configure_task_flow_example_target target)
    target_include_directories(${target}
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../include
            ${CMAKE_CURRENT_SOURCE_DIR}/../../src
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

    if(WIN32 AND TARGET amqp_bus)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:amqp_bus>
                $<TARGET_FILE_DIR:${target}>
        )
    endif()

    if(WIN32 AND TARGET http_transport)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:http_transport>
                $<TARGET_FILE_DIR:${target}>
        )
    endif()

    if(WIN32 AND TARGET semiplugin_manager)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:semiplugin_manager>
                $<TARGET_FILE_DIR:${target}>
        )
    endif()

    if(WIN32 AND TARGET tgv_etching_semiplugin)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:tgv_etching_semiplugin>
                $<TARGET_FILE_DIR:${target}>
        )
    endif()

endfunction()

if(NOT TARGET amqp_bus)
    message(FATAL_ERROR
        "examples/task_flow requires amqp_bus. Enable MC_BUILD_AMQP_BUS_MODULE.")
endif()
if(NOT TARGET http_transport)
    message(FATAL_ERROR
        "examples/task_flow requires http_transport. Enable MC_BUILD_HTTP_TRANSPORT_MODULE.")
endif()
if(NOT TARGET semiplugin_manager)
    message(FATAL_ERROR
        "examples/task_flow requires semiplugin_manager. Enable MC_BUILD_SEMIPLUGIN_MANAGER_MODULE.")
endif()

add_library(tgv_etching_semiplugin SHARED
    ../../third_party/SEMIPLUGIN/modules/tgv_etching/src/plugin_tgv_etching_impl.cpp
)

target_compile_definitions(tgv_etching_semiplugin
    PRIVATE
        TGV_ETCHING_IMPL_EXPORTS
)

target_include_directories(tgv_etching_semiplugin
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/SEMIPLUGIN/api/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/SEMIPLUGIN/modules/tgv_etching/include
)

if(MC_SEMIPLUGIN_VENDOR_INCLUDE_DIRS)
    target_include_directories(tgv_etching_semiplugin
        PRIVATE
            ${MC_SEMIPLUGIN_VENDOR_INCLUDE_DIRS}
    )
endif()

if(MC_TGV_ETCHING_VENDOR_LIBRARIES)
    target_link_libraries(tgv_etching_semiplugin
        PRIVATE
            ${MC_TGV_ETCHING_VENDOR_LIBRARIES}
    )
endif()

set_target_properties(tgv_etching_semiplugin PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

if(WIN32 AND MC_TGV_ETCHING_VENDOR_RUNTIME_DLLS)
    foreach(_vendor_dll IN LISTS MC_TGV_ETCHING_VENDOR_RUNTIME_DLLS)
        add_custom_command(TARGET tgv_etching_semiplugin POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_vendor_dll}
                $<TARGET_FILE_DIR:tgv_etching_semiplugin>
        )
    endforeach()
endif()

add_library(mc_task_flow_example_support STATIC
    common.cpp
    master_runner.cpp
)

mc_configure_task_flow_example_target(mc_task_flow_example_support)

target_link_libraries(mc_task_flow_example_support
    PUBLIC
        mc_core_framework
        foundation::foundation
)

target_include_directories(mc_task_flow_example_support
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/SEMIPLUGIN/api/include
)

if(MC_SEMIPLUGIN_VENDOR_INCLUDE_DIRS)
    target_include_directories(mc_task_flow_example_support
        PUBLIC
            ${MC_SEMIPLUGIN_VENDOR_INCLUDE_DIRS}
    )
endif()

add_executable(mc_task_flow_master_host
    master_sim.cpp
)

mc_configure_task_flow_example_target(mc_task_flow_master_host)

target_link_libraries(mc_task_flow_master_host
    PRIVATE
        mc_task_flow_example_support
)

add_executable(mc_task_flow_worker_host
    worker_sim.cpp
)

mc_configure_task_flow_example_target(mc_task_flow_worker_host)

target_link_libraries(mc_task_flow_worker_host
    PRIVATE
        mc_task_flow_example_support
)

add_dependencies(mc_task_flow_worker_host
    semiplugin_manager
    tgv_etching_semiplugin
)
