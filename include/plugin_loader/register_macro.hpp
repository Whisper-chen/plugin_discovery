/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
// Registration macro for plugin libraries. Same shape as CLASS_LOADER_REGISTER_CLASS.
//
//   PLUGIN_REGISTER_CLASS(example::EnglishGreeter, example::Greeter)
//
// Place it at namespace scope in a .cpp of the plugin library. It defines a
// static object whose constructor runs while the library is being dlopen()'d and
// adds a factory for Derived (looked up by the *text* "Derived") under Base.
#pragma once

#include <string>

#include "plugin_loader/impl.hpp"

#define PLUGIN_REGISTER_CLASS_INTERNAL_WITH_MESSAGE_(Derived, Base, UniqueID, Message) \
  namespace \
  { \
  struct ProxyExec##UniqueID \
  { \
    ProxyExec##UniqueID() \
    { \
      if (!std::string(Message).empty()) { \
        ::plugin_loader::impl::log(::plugin_loader::LogLevel::Info, "%s", Message); \
      } \
      ::plugin_loader::impl::register_class<Derived, Base>(#Derived); \
    } \
  }; \
  static ProxyExec##UniqueID g_register_plugin_##UniqueID; \
  }

#define PLUGIN_REGISTER_CLASS_INTERNAL_HOP1_WITH_MESSAGE_(Derived, Base, UniqueID, Message) \
  PLUGIN_REGISTER_CLASS_INTERNAL_WITH_MESSAGE_(Derived, Base, UniqueID, Message)

// Same as PLUGIN_REGISTER_CLASS, and logs `Message` when the plugin is registered.
#define PLUGIN_REGISTER_CLASS_WITH_MESSAGE(Derived, Base, Message) \
  PLUGIN_REGISTER_CLASS_INTERNAL_HOP1_WITH_MESSAGE_(Derived, Base, __COUNTER__, Message)

#define PLUGIN_REGISTER_CLASS(Derived, Base) \
  PLUGIN_REGISTER_CLASS_WITH_MESSAGE(Derived, Base, "")
