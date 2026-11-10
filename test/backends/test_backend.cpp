/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
// One source, several test backend libraries. Each is compiled with:
//   TEST_CLASS   class name, e.g. AlphaSensor
//   TEST_PROBE   0 = Ok, 1 = Unsupported, 2 = Fault
//   TEST_NODE    device node reported by probe() (may be "")
#include "test_backend.hpp"

#ifndef TEST_PROBE
#  define TEST_PROBE 0
#endif
#ifndef TEST_NODE
#  define TEST_NODE ""
#endif

namespace test
{
class TEST_CLASS : public Sensor
{
public:
  std::string id() override {return PLUGIN_DISCOVERY_STR_(TEST_CLASS);}

  plugin_discovery::ProbeResult probe() override
  {
    if (TEST_PROBE == 1) {
      return plugin_discovery::ProbeResult::unsupported("vendor SDK does not run here");
    }
    if (TEST_PROBE == 2) {
      return plugin_discovery::ProbeResult::fault("device did not answer");
    }
    return plugin_discovery::ProbeResult::ok({{"0", TEST_NODE, "test device"}});
  }
};
}  // namespace test

PLUGIN_DISCOVERY_REGISTER_BACKEND(test::TEST_CLASS, test::Sensor)
