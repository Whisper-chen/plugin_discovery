# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear
# CMake helpers shipped with plugin_discovery (included by plugin_discoveryConfig.cmake).
include_guard(GLOBAL)
include(GNUInstallDirs)

# plugin_discovery_plugin_dir(<DOMAIN> <out-var>)
#   Absolute directory where plugin_discovery_add_plugin() installs the manifests
#   of <DOMAIN>. Handy as a compile-time default search path for an application:
#     plugin_discovery_plugin_dir(hello HELLO_PLUGIN_DIR)
#     target_compile_definitions(my_app PRIVATE HELLO_PLUGIN_DIR="${HELLO_PLUGIN_DIR}")
function(plugin_discovery_plugin_dir domain out_var)
  set(${out_var} "${CMAKE_INSTALL_FULL_DATADIR}/${domain}/plugins" PARENT_SCOPE)
endfunction()

# plugin_discovery_add_plugin(
#   TARGET <shared-library-target>
#   DOMAIN <domain>                 groups plugins: lib/<domain>/ and share/<domain>/plugins/
#   NAME <unique plugin id>         e.g. camera/QCarCamCamera
#   CLASS <Derived>                 text passed to PLUGIN_DISCOVERY_REGISTER_BACKEND
#   BASE <Interface>                the domain interface (as in PLUGIN_DISCOVERY_INTERFACE)
#   [BACKEND <name>]                name used to list the backend in configuration
#   [ORIGIN qcom|customer]          default customer; QCOM backends pass ORIGIN qcom
#   [PRIORITY <int>]                default 0, higher first when nothing else orders them
#   [CAPABILITIES <cap>...])        what the backend can do (zero_copy, ...)
#
# Installs the library to lib/<domain>/ and a manifest describing it to
# share/<domain>/plugins/<TARGET>.plugin.yaml. The manifest refers to the library
# by a path RELATIVE to itself, so the whole install tree can be moved.
#
# The manifests are their own install component, so they can be exported without
# installing anything else:
#   cmake --install <build-dir> --component manifests --prefix <out-dir>
function(plugin_discovery_add_plugin)
  cmake_parse_arguments(PD "" "TARGET;DOMAIN;NAME;CLASS;BASE;PRIORITY;BACKEND;ORIGIN"
    "CAPABILITIES;PLATFORMS" ${ARGN})
  if(DEFINED PD_PLATFORMS OR "PLATFORMS" IN_LIST PD_KEYWORDS_MISSING_VALUES)
    message(FATAL_ERROR "plugin_discovery_add_plugin(${PD_TARGET}): PLATFORMS is no longer "
      "supported. plugin_discovery does not select by SoC: the backend decides in probe() "
      "whether it works on this machine, and a platform config package lists the backends "
      "to load (see include/plugin_discovery/plugin_registry.hpp).")
  endif()
  foreach(required TARGET DOMAIN NAME CLASS BASE)
    if(NOT PD_${required})
      message(FATAL_ERROR "plugin_discovery_add_plugin: ${required} is required")
    endif()
  endforeach()
  if(NOT TARGET ${PD_TARGET})
    message(FATAL_ERROR "plugin_discovery_add_plugin: '${PD_TARGET}' is not a target")
  endif()
  if(NOT DEFINED PD_PRIORITY)
    set(PD_PRIORITY 0)
  endif()
  if(NOT DEFINED PD_ORIGIN)
    set(PD_ORIGIN customer)
  endif()
  if(NOT PD_ORIGIN MATCHES "^(qcom|customer)$")
    message(FATAL_ERROR "plugin_discovery_add_plugin: ORIGIN must be qcom or customer")
  endif()

  set(plugin_dir "${CMAKE_INSTALL_LIBDIR}/${PD_DOMAIN}")
  set(manifest_dir "${CMAKE_INSTALL_DATADIR}/${PD_DOMAIN}/plugins")
  # Both are relative to the install prefix; a leading "/" makes RELATIVE_PATH treat them
  # as absolute so the result is the path from the manifest directory to the library directory.
  file(RELATIVE_PATH rel_to_libs "/${manifest_dir}" "/${plugin_dir}")

  string(JOIN ", " capabilities ${PD_CAPABILITIES})
  set(backend_line "")
  if(PD_BACKEND)
    set(backend_line "    backend: ${PD_BACKEND}\n")
  endif()
  set(manifest "${CMAKE_CURRENT_BINARY_DIR}/${PD_TARGET}.plugin.yaml")
  file(GENERATE OUTPUT "${manifest}" CONTENT
"plugins:
  - name: ${PD_NAME}
    class: ${PD_CLASS}
    base: ${PD_BASE}
    library: ${rel_to_libs}/$<TARGET_FILE_NAME:${PD_TARGET}>
    domain: ${PD_DOMAIN}
${backend_line}    origin: ${PD_ORIGIN}
    priority: ${PD_PRIORITY}
    capabilities: [${capabilities}]
")

  # A backend must reach libplugin_discovery.so (the registration records live there).
  target_link_libraries(${PD_TARGET} PRIVATE plugin_discovery::plugin_discovery)
  install(TARGETS ${PD_TARGET} LIBRARY DESTINATION "${plugin_dir}")
  install(FILES "${manifest}" DESTINATION "${manifest_dir}" COMPONENT manifests)
endfunction()

# plugin_discovery_install_config(<file>... [DESTINATION <dir>])
#   Installs platform configuration candidates into share/plugin_discovery/platforms.d/.
#   Name each <machine>-<platform>-<domain>.config.yaml (e.g. QCS8275-Ubuntu-camera.config.yaml).
#   The library never reads that directory: platform-detect links the candidate that matches
#   this machine and OS into /etc/plugin_discovery/platform.d/ on the first boot.
function(plugin_discovery_install_config)
  cmake_parse_arguments(PDC "" "DESTINATION;COMPONENT" "" ${ARGN})
  if(NOT PDC_DESTINATION)
    set(PDC_DESTINATION "${CMAKE_INSTALL_DATADIR}/plugin_discovery/platforms.d")
  endif()
  if(NOT PDC_COMPONENT)
    set(PDC_COMPONENT config)
  endif()
  install(FILES ${PDC_UNPARSED_ARGUMENTS} DESTINATION "${PDC_DESTINATION}" COMPONENT ${PDC_COMPONENT})
endfunction()
