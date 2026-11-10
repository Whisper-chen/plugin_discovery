/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
// Test plugin libraries, compiled once per implementation under test:
//   -DUSE_UPSTREAM   class_loader's CLASS_LOADER_REGISTER_CLASS
//   (default)        plugin_loader's PLUGIN_REGISTER_CLASS
// and once per library flavour: TEST_LIB_A, TEST_LIB_B, TEST_LIB_C.
//
//   A: test::Dog, test::Cat (Animal)      test::Car (Vehicle)
//   B: test::Fish, test::Bird (Animal)
//   C: test::Dog (Animal)  -- same class name as A's, different behaviour
#include <string>

#include "test_interfaces.hpp"

#ifdef USE_UPSTREAM
#include <class_loader/register_macro.hpp>
#define REGISTER(Derived, Base) CLASS_LOADER_REGISTER_CLASS(Derived, Base)
#define REGISTER_WITH_MESSAGE(Derived, Base, Msg) \
  CLASS_LOADER_REGISTER_CLASS_WITH_MESSAGE(Derived, Base, Msg)
#else
#include "plugin_loader/register_macro.hpp"
#define REGISTER(Derived, Base) PLUGIN_REGISTER_CLASS(Derived, Base)
#define REGISTER_WITH_MESSAGE(Derived, Base, Msg) \
  PLUGIN_REGISTER_CLASS_WITH_MESSAGE(Derived, Base, Msg)
#endif

namespace test
{

#define ANIMAL(Class, Tag) \
  class Class : public Animal \
  { \
  public: \
    std::string name() override {return #Class "@" Tag;} \
  };

#if defined(TEST_LIB_A)
ANIMAL(Dog, "A")
ANIMAL(Cat, "A")
class Car : public Vehicle
{
public:
  std::string name() override {return "Car@A";}
};
#elif defined(TEST_LIB_B)
ANIMAL(Fish, "B")
ANIMAL(Bird, "B")
#elif defined(TEST_LIB_C)
ANIMAL(Dog, "C")
#else
#error "define TEST_LIB_A, TEST_LIB_B or TEST_LIB_C"
#endif

}  // namespace test

#if defined(TEST_LIB_A)
REGISTER(test::Dog, test::Animal)
REGISTER(test::Cat, test::Animal)
REGISTER(test::Car, test::Vehicle)
#elif defined(TEST_LIB_B)
REGISTER(test::Fish, test::Animal)
REGISTER_WITH_MESSAGE(test::Bird, test::Animal, "test::Bird registered")
#elif defined(TEST_LIB_C)
REGISTER(test::Dog, test::Animal)
#endif
