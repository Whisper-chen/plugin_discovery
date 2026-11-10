# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear
cmake_minimum_required(VERSION 3.16)

# Customer configuration package: adds the v4l2 backend (UVC cameras) in front of the
# platform's camera backends. Installed to /etc/plugin_discovery/config.d/ (the customer
# tier), where it applies after the platform tier. dpkg treats it as a conffile: local edits
# survive an upgrade, and `dpkg -P` removes it.
#
#   cmake -S . -B build && cd build && cpack -G DEB
project(plugin-discovery-config-customer VERSION 0.1.0 LANGUAGES NONE)

install(FILES 50-camera-v4l2.config.yaml DESTINATION etc/plugin_discovery/config.d)

set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_NAME plugin-discovery-config-v4l2)
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "Qualcomm Technologies, Inc. <noreply@qualcomm.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "plugin_discovery customer configuration: v4l2 camera backend first")
set(CPACK_PACKAGING_INSTALL_PREFIX /)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE all)
set(CPACK_DEBIAN_PACKAGE_SECTION libs)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "plugin-discovery (>= 0.3.0)")
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA ${CMAKE_CURRENT_SOURCE_DIR}/conffiles)
include(CPack)
