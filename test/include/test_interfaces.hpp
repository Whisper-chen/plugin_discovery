/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
#pragma once

#include <string>

namespace test
{

struct Animal
{
  virtual ~Animal() = default;
  virtual std::string name() = 0;
};

struct Vehicle
{
  virtual ~Vehicle() = default;
  virtual std::string name() = 0;
};

}  // namespace test
