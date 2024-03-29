
# get commit id from a file:
set(TELEPORT_COMMIT $ENV{TELEPORT_COMMIT})
set(CPACK_PACKAGE_VERSION_MAJOR "${TELEPORT_COMMIT}")
set(CPACK_PACKAGE_VERSION_MINOR "0")
set(CPACK_PACKAGE_VERSION_PATCH "0")
set(CPACK_PACKAGE_VERSION "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")

message("TELEPORT_COMMIT ${TELEPORT_COMMIT}")

if("${TELEPORT_COMMIT}" STREQUAL "")
	message("Test installer build.")
	message("CPACK_PACKAGING_INSTALL_PREFIX ${CPACK_PACKAGING_INSTALL_PREFIX}")
	#C:/TestInstallTeleport)
endif()
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${TELEPORT_COMMIT}-x64")
message("CPACK_PACKAGE_FILE_NAME ${CPACK_PACKAGE_FILE_NAME}")
 