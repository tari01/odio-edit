#set (SCHEMA_FILE "${CMAKE_CURRENT_BINARY_DIR}/${SCHEMA_NAME}")

# in.tari.odio-edit.gschema.xml

find_package (GSettings REQUIRED)
add_schema ("in.tari.odio-edit.gschema.xml")

# odio-edit.desktop

configure_file ("${CMAKE_CURRENT_SOURCE_DIR}/odio-edit.desktop.in" "${CMAKE_CURRENT_BINARY_DIR}/odio-edit.desktop" @ONLY)
install (FILES "${CMAKE_CURRENT_BINARY_DIR}/odio-edit.desktop" DESTINATION "${CMAKE_INSTALL_FULL_DATADIR}/applications")

# odio-edit.svg

install (FILES "${CMAKE_CURRENT_SOURCE_DIR}/odio-edit.svg" DESTINATION "${CMAKE_INSTALL_FULL_DATADIR}/icons/hicolor/scalable/apps")
