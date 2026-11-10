# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear
cmake_minimum_required(VERSION 3.16)

# Platform configuration template. Copy this directory, replace every <PLACEHOLDER>, and
# build it as its own package. It contains no code and does not need the plugin_discovery
# library to build, only the list of backends this machine fully supports with this OS.
# Installing it makes the platform work out of the box: on the first boot platform-detect
# links the candidate that matches the machine and the OS type.
#
#   cmake -S . -B build -DCMAKE_INSTALL_PREFIX=<prefix>
#   cmake --install build

project(plugin-discovery-config-MACHINE VERSION 0.1.0 LANGUAGES NONE)

# <prefix>/share/plugin_discovery/platforms.d holds the candidates; plugin_discovery does
# not read it. One file per machine, OS type and domain, named
# <machine>-<platform>-<domain>.config.yaml (no NN: the combination is unique), e.g.
# QCS8275-Ubuntu-camera.config.yaml. <machine> is the string in /sys/devices/soc0/machine,
# <platform> the OS type (Ubuntu or QCLinux); names are compared ignoring case.
# Rename MACHINE and PLATFORM in the file names to the real values before building.
install(FILES
  config/MACHINE-PLATFORM-camera.config.yaml
  # config/MACHINE-PLATFORM-video.config.yaml
  # config/MACHINE-PLATFORM-audio.config.yaml
  DESTINATION share/plugin_discovery/platforms.d)
