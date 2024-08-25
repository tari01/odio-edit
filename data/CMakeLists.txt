# in.tari.odio-edit.gschema.xml

find_package (GSettings REQUIRED)
add_schema ("in.tari.${CMAKE_PROJECT_NAME}.gschema.xml")

# odio-edit.desktop

find_package (Intltool REQUIRED)
configure_file ("${CMAKE_CURRENT_SOURCE_DIR}/${CMAKE_PROJECT_NAME}.desktop.in" "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.desktop.intl" @ONLY)
intltool_merge_translations ("${CMAKE_PROJECT_NAME}.desktop.intl" "${CMAKE_PROJECT_NAME}.desktop" ALL UTF8)
install (FILES "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.desktop" DESTINATION "${CMAKE_INSTALL_FULL_DATADIR}/applications")

# odio-edit.svg

install (FILES "${CMAKE_CURRENT_SOURCE_DIR}/${CMAKE_PROJECT_NAME}.svg" DESTINATION "${CMAKE_INSTALL_FULL_DATADIR}/icons/hicolor/scalable/apps")
