# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear
cmake_minimum_required(VERSION 3.16)

# Platform configuration package: the camera backends Qualcomm fully supports on one machine
# and OS. It ships a candidate in /usr/share/plugin_discovery/platforms.d/ and asks
# platform-detect to link the matching one into /etc/plugin_discovery/platform.d/.
#
#   cmake -S . -B build [-DPD_MACHINE=QCS8300] [-DPD_PLATFORM=Ubuntu] [-DPD_CAMERA_BACKENDS="qmmf;qcarcam"]
#   cd build && cpack -G DEB
set(PD_MACHINE "QCS8300" CACHE STRING "Machine, the string in /sys/devices/soc0/machine")
set(PD_PLATFORM "Ubuntu" CACHE STRING "OS type: Ubuntu or QCLinux")
set(PD_CAMERA_BACKENDS "qmmf;qcarcam" CACHE STRING "Camera backends in load order; the first is the default")

project(plugin-discovery-config-platform VERSION 0.1.0 LANGUAGES NONE)

string(REPLACE ";" ", " PD_CAMERA_BACKENDS_LIST "${PD_CAMERA_BACKENDS}")
set(candidate "${PD_MACHINE}-${PD_PLATFORM}-camera.config.yaml")
configure_file(camera.config.yaml.in ${CMAKE_CURRENT_BINARY_DIR}/${candidate} @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${candidate}
  DESTINATION share/plugin_discovery/platforms.d)

string(TOLOWER "${PD_MACHINE}-${PD_PLATFORM}" pkg_suffix)
set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_NAME plugin-discovery-config-${pkg_suffix})
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "Qualcomm Technologies, Inc. <noreply@qualcomm.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
  "plugin_discovery platform configuration for ${PD_MACHINE} on ${PD_PLATFORM}")
set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE all)
set(CPACK_DEBIAN_PACKAGE_SECTION libs)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "plugin-discovery (>= 0.3.0)")
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
  ${CMAKE_CURRENT_SOURCE_DIR}/postinst ${CMAKE_CURRENT_SOURCE_DIR}/postrm)
include(CPack)
