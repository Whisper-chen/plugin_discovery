/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
// Interface used by registry_test. TEST_IFACE_MINOR lets a test backend claim it was
// built against another interface version.
#pragma once

#include <string>

#include "plugin_discovery/backend.hpp"

#ifndef TEST_IFACE_MINOR
#  define TEST_IFACE_MINOR 1
#endif

namespace test
{
class Sensor : public plugin_discovery::Backend
{
public:
  virtual std::string id() = 0;
};
}  // namespace test

PLUGIN_DISCOVERY_INTERFACE(test::Sensor, "sensor", 1, TEST_IFACE_MINOR)
