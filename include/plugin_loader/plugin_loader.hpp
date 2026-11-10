/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
// plugin_loader: a dependency-free (libc/libdl + C++17) plugin loading library
// modelled on class_loader. See README.md for the mapping.
//
// Plugin libraries need only:   #include "plugin_loader/register_macro.hpp"
// Hosts need:                   #include "plugin_loader/plugin_loader.hpp"
#pragma once

#include "plugin_loader/class_loader.hpp"
#include "plugin_loader/exceptions.hpp"
#include "plugin_loader/multi_library_class_loader.hpp"
#include "plugin_loader/register_macro.hpp"
