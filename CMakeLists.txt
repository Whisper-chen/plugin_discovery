# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear
cmake_minimum_required(VERSION 3.16)
project(plugin_discovery VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PLUGIN_LOADER_BUILD_TESTS "Build the plugin_loader scenario tests" OFF)
option(PLUGIN_LOADER_COMPARE_UPSTREAM
  "Also build the scenarios against ROS class_loader to compare behaviour (needs ROS sourced)" OFF)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Installed binaries and plugins find libplugin_loader.so without LD_LIBRARY_PATH.
# Only a default: distribution builds (e.g. Yocto) pass -DCMAKE_INSTALL_RPATH= to drop it.
if(NOT DEFINED CMAKE_INSTALL_RPATH)
  set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_FULL_LIBDIR}")
endif()

find_package(yaml-cpp REQUIRED)

# --- plugin_loader: dlopen + registration macro + process-wide factory table.
#     Plugins and hosts must link the SAME shared library so they share the table.
add_library(plugin_loader SHARED
  src/impl.cpp
  src/class_loader.cpp
  src/multi_library_class_loader.cpp)
add_library(plugin_discovery::plugin_loader ALIAS plugin_loader)
target_include_directories(plugin_loader PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(plugin_loader PUBLIC ${CMAKE_DL_LIBS})
target_compile_options(plugin_loader PRIVATE -Wall -Wextra -Wpedantic)

# --- plugin_discovery: manifests + configuration -> which backends to load.
add_library(plugin_discovery SHARED src/plugin_registry.cpp)
add_library(plugin_discovery::plugin_discovery ALIAS plugin_discovery)
target_link_libraries(plugin_discovery
  PUBLIC plugin_loader ${CMAKE_DL_LIBS}
  PRIVATE yaml-cpp::yaml-cpp)  # implementation detail, not in the public header
target_compile_options(plugin_discovery PRIVATE -Wall -Wextra -Wpedantic)

# --- pluginlib-detect: show the merged configuration and where each entry came from.
add_executable(pluginlib-detect tools/pluginlib_detect.cpp)
target_link_libraries(pluginlib-detect PRIVATE plugin_discovery)
target_compile_options(pluginlib-detect PRIVATE -Wall -Wextra -Wpedantic)

# --- platform-detect: first-boot choice of the platform configuration (see tools/platform_detect.cpp).
add_executable(platform-detect tools/platform_detect.cpp)
target_compile_definitions(platform-detect PRIVATE
  PLATFORM_DETECT_CANDIDATES_DIR="${CMAKE_INSTALL_FULL_DATADIR}/plugin_discovery/platforms.d")
target_compile_options(platform-detect PRIVATE -Wall -Wextra -Wpedantic)
configure_file(systemd/plugin-discovery-platform-detect.service.in
  ${CMAKE_CURRENT_BINARY_DIR}/plugin-discovery-platform-detect.service @ONLY)

install(TARGETS plugin_loader plugin_discovery pluginlib-detect platform-detect
  EXPORT plugin_discoveryTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
# Platform packages install their candidates here; the library never reads this directory,
# platform-detect links the matching ones into /etc/plugin_discovery/platform.d/.
install(DIRECTORY DESTINATION ${CMAKE_INSTALL_DATADIR}/plugin_discovery/platforms.d)
# Runs platform-detect once, on the first boot. Application units order themselves After= it.
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/plugin-discovery-platform-detect.service
  DESTINATION lib/systemd/system)

install(EXPORT plugin_discoveryTargets
  NAMESPACE plugin_discovery::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/plugin_discovery)

configure_package_config_file(
  cmake/plugin_discoveryConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/plugin_discoveryConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/plugin_discovery)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/plugin_discoveryConfigVersion.cmake
  COMPATIBILITY SameMajorVersion)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/plugin_discoveryConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/plugin_discoveryConfigVersion.cmake
  cmake/plugin_discoveryMacros.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/plugin_discovery)

# Filled-in configuration examples, to copy from.
install(FILES
  templates/platform-config/examples/QCS8300-Ubuntu-camera.config.yaml
  templates/customer-config/examples/50-camera-v4l2.config.yaml
  DESTINATION ${CMAKE_INSTALL_DOCDIR}/examples)
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR} RENAME copyright)

# Debian package: build on the target (or natively for it) and install with dpkg/apt:
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr && cmake --build build -j
#   cpack --config build/CPackConfig.cmake -G DEB      # writes build/plugin-discovery_<version>_<arch>.deb
set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_NAME plugin-discovery)
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "Qualcomm Technologies, Inc. <noreply@qualcomm.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Cross-platform backend mechanism for the Robotics SDK")
set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
set(CPACK_DEBIAN_PACKAGE_SECTION libs)
set(CPACK_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS
  OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "systemd")
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
  ${CMAKE_CURRENT_SOURCE_DIR}/packaging/postinst
  ${CMAKE_CURRENT_SOURCE_DIR}/packaging/prerm
  ${CMAKE_CURRENT_SOURCE_DIR}/packaging/postrm)
include(CPack)

if(PLUGIN_LOADER_BUILD_TESTS OR PLUGIN_LOADER_COMPARE_UPSTREAM)
  find_package(Threads REQUIRED)
  add_subdirectory(test)
endif()
