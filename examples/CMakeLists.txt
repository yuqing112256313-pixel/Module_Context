if((MC_BUILD_TASK_FLOW_E2E OR MC_BUILD_TASK_FLOW_QT_DEMO) AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/task_flow/CMakeLists.txt")
    add_subdirectory(task_flow)
endif()

if(MC_BUILD_TASK_FLOW_QT_DEMO AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/task_flow_qt_master_demo/CMakeLists.txt")
    add_subdirectory(task_flow_qt_master_demo)
endif()
