# odio-edit

set(SOURCES
    gstreamer.c
    chunk.c
    chunkview.c
    main.c
    message.c
    mainwindow.c
    ringbuf.c
    player.c
    file.c
    viewcache.c
    datasource.c
    tempfile.c
    document.c
)

add_executable ("odio-edit" ${SOURCES})
target_compile_definitions ("odio-edit" PUBLIC G_LOG_DOMAIN="${CMAKE_PROJECT_NAME}" LOCALEDIR="${CMAKE_INSTALL_FULL_LOCALEDIR}" APPVERSION="${CMAKE_PROJECT_VERSION}")
target_link_libraries ("odio-edit" ${DEPS_LIBRARIES} m)
target_include_directories ("odio-edit" PUBLIC ${DEPS_INCLUDE_DIRS})
install (TARGETS "odio-edit" RUNTIME DESTINATION ${CMAKE_INSTALL_FULL_BINDIR})
