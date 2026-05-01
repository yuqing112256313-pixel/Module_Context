set(SEMIPLUGIN_MANAGER_SOURCES
    src/internal/PluginLoader.cpp
    src/internal/PluginRegistry.cpp
    src/SemipluginManagerModule.cpp
)

add_library(semiplugin_manager SHARED
    ${SEMIPLUGIN_MANAGER_SOURCES}
)

target_compile_definitions(semiplugin_manager
    PRIVATE
        MC_FRAMEWORK_SHARED_LIBRARY
)

target_include_directories(semiplugin_manager
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/internal
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../../src
        ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/SEMIPLUGIN/api/include
)

target_link_libraries(semiplugin_manager
    PRIVATE
        mc_core_framework
        foundation::foundation
)

if(WIN32)
    set_target_properties(semiplugin_manager PROPERTIES PREFIX "")
    add_custom_command(TARGET semiplugin_manager POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:mc_core_framework>
            $<TARGET_FILE_DIR:semiplugin_manager>
    )
else()
    target_link_libraries(semiplugin_manager PRIVATE ${CMAKE_DL_LIBS})
endif()

set_target_properties(semiplugin_manager PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

install(TARGETS semiplugin_manager
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/plugins
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/plugins
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}/plugins
)
