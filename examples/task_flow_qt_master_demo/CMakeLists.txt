cmake_minimum_required(VERSION 3.16)

find_package(Qt5 5.9 REQUIRED COMPONENTS Widgets Network)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

add_executable(mc_task_flow_qt_master_demo WIN32
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    masterworker.cpp
    masterworker.h
    bmpframesource.cpp
    bmpframesource.h
    rabbitmqadminclient.cpp
    rabbitmqadminclient.h
    taskflowdemotypes.h
)

target_include_directories(mc_task_flow_qt_master_demo
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../task_flow
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_link_libraries(mc_task_flow_qt_master_demo
    PRIVATE
        Qt5::Widgets
        Qt5::Network
        mc_task_flow_example_support
)

set_target_properties(mc_task_flow_qt_master_demo PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    DEBUG_POSTFIX d
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

if(WIN32 AND TARGET mc_core_framework)
    add_custom_command(TARGET mc_task_flow_qt_master_demo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:mc_core_framework>
            $<TARGET_FILE_DIR:mc_task_flow_qt_master_demo>
    )
endif()

if(WIN32 AND TARGET rabbitmq_bus)
    add_custom_command(TARGET mc_task_flow_qt_master_demo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:rabbitmq_bus>
            $<TARGET_FILE_DIR:mc_task_flow_qt_master_demo>
    )
endif()

if(WIN32 AND TARGET http_transport)
    add_custom_command(TARGET mc_task_flow_qt_master_demo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:http_transport>
            $<TARGET_FILE_DIR:mc_task_flow_qt_master_demo>
    )
endif()

if(WIN32 AND TARGET mc_task_flow_worker_host)
    add_dependencies(mc_task_flow_qt_master_demo mc_task_flow_worker_host)
    add_custom_command(TARGET mc_task_flow_qt_master_demo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:mc_task_flow_worker_host>
            $<TARGET_FILE_DIR:mc_task_flow_qt_master_demo>
    )
endif()

if(WIN32 AND TARGET semiplugin_manager)
    add_dependencies(mc_task_flow_qt_master_demo semiplugin_manager)
    add_custom_command(TARGET mc_task_flow_qt_master_demo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:semiplugin_manager>
            $<TARGET_FILE_DIR:mc_task_flow_qt_master_demo>
    )
endif()

if(WIN32 AND TARGET tgv_etching_semiplugin)
    add_dependencies(mc_task_flow_qt_master_demo tgv_etching_semiplugin)
    add_custom_command(TARGET mc_task_flow_qt_master_demo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:tgv_etching_semiplugin>
            $<TARGET_FILE_DIR:mc_task_flow_qt_master_demo>
    )
endif()
