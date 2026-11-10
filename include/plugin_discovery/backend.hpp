/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
// Common base of every backend that plugin_discovery loads, plus the macros that tie a
// domain interface to a version and a backend to that interface.
//
// A domain interface derives from Backend and declares its version once, at global scope:
//
//   namespace hello { class Greeter : public plugin_discovery::Backend { ... }; }
//   PLUGIN_DISCOVERY_INTERFACE(hello::Greeter, "hello", 1, 0)
//
// A backend registers itself in one of its .cpp files:
//
//   PLUGIN_DISCOVERY_REGISTER_BACKEND(hello::IQ8Greeter, hello::Greeter)
//
// The macro records, inside the backend library, the interface version and ABI tag the
// backend was compiled against. plugin_discovery compares them with the application's
// before creating any object (see PluginRegistry::create).
#pragma once

#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "plugin_loader/register_macro.hpp"

namespace plugin_discovery
{

// One device a backend can serve, as reported by its vendor SDK.
struct DeviceInfo
{
  std::string id;           // the backend's own id for the device, e.g. "0"
  std::string node;         // device node the SDK reports, e.g. /dev/video2; may be empty
  std::string description;  // free text for logs
};

enum class ProbeStatus
{
  Ok,           // works on this machine; `devices` lists what it can serve
  Unsupported,  // not for this machine (e.g. its vendor SDK does not run on this SoC)
  Fault,        // should work here but failed (device error, daemon down, ...)
};

struct ProbeResult
{
  ProbeStatus status = ProbeStatus::Ok;
  std::vector<DeviceInfo> devices;
  std::string message;  // why, for Unsupported / Fault

  static ProbeResult ok(std::vector<DeviceInfo> devices)
  {
    return {ProbeStatus::Ok, std::move(devices), ""};
  }
  static ProbeResult unsupported(std::string why)
  {
    return {ProbeStatus::Unsupported, {}, std::move(why)};
  }
  static ProbeResult fault(std::string why)
  {
    return {ProbeStatus::Fault, {}, std::move(why)};
  }
};

class Backend
{
public:
  virtual ~Backend() = default;

  // Asks the backend's vendor SDK whether it works on this machine and which devices it
  // can serve. Called once, right after the backend is created. Must not crash or leave
  // anything behind when the answer is Unsupported.
  //
  // The default suits backends without devices to enumerate: Ok, one device "default".
  virtual ProbeResult probe()
  {
    return ProbeResult::ok({DeviceInfo{"default", "", ""}});
  }
};

// Version of a domain interface, filled in by PLUGIN_DISCOVERY_INTERFACE.
template<class Interface>
struct interface_traits;

namespace detail
{

// Called by PLUGIN_DISCOVERY_REGISTER_BACKEND while the backend library is being opened.
// `anchor` is any address inside that library; it identifies the library. Records are
// kept for the life of the process: a library that stays mapped after dlclose does not
// run its initializers again when it is reopened.
void record_backend(
  const void * anchor, const char * base_class, const char * class_name, int major, int minor,
  const char * abi_tag);

}  // namespace detail
}  // namespace plugin_discovery

// Compiler + C++ standard library ABI. Two binaries with different tags must not share
// C++ objects. May be predefined (tests use that to fake a mismatch).
#define PLUGIN_DISCOVERY_STR2_(x) #x
#define PLUGIN_DISCOVERY_STR_(x) PLUGIN_DISCOVERY_STR2_(x)
#ifndef PLUGIN_DISCOVERY_ABI_TAG
#  if defined(__clang__)
#    define PLUGIN_DISCOVERY_COMPILER_ "clang" PLUGIN_DISCOVERY_STR_(__clang_major__)
#  elif defined(__GNUC__)
#    define PLUGIN_DISCOVERY_COMPILER_ "gcc" PLUGIN_DISCOVERY_STR_(__GNUC__)
#  else
#    define PLUGIN_DISCOVERY_COMPILER_ "unknown"
#  endif
#  if defined(_GLIBCXX_USE_CXX11_ABI)
#    define PLUGIN_DISCOVERY_ABI_TAG \
  PLUGIN_DISCOVERY_COMPILER_ "-cxx11abi" PLUGIN_DISCOVERY_STR_(_GLIBCXX_USE_CXX11_ABI)
#  else
#    define PLUGIN_DISCOVERY_ABI_TAG PLUGIN_DISCOVERY_COMPILER_
#  endif
#endif

// Declares the domain and version of an interface. Use at global scope, right after the
// interface, with the fully qualified name (it must match BASE in plugin_discovery_add_plugin).
// Bump `minor` when only appending virtual functions at the end; bump `major` otherwise.
#define PLUGIN_DISCOVERY_INTERFACE(Interface, Domain, Major, Minor) \
  namespace plugin_discovery \
  { \
  template<> \
  struct interface_traits<Interface> \
  { \
    static constexpr const char * base_class = #Interface; \
    static constexpr const char * domain = Domain; \
    static constexpr int major = Major; \
    static constexpr int minor = Minor; \
  }; \
  }

#define PLUGIN_DISCOVERY_REGISTER_INTERNAL_(Derived, Interface, UniqueID) \
  PLUGIN_REGISTER_CLASS(Derived, Interface) \
  namespace \
  { \
  struct PluginDiscoveryRecord##UniqueID \
  { \
    PluginDiscoveryRecord##UniqueID() \
    { \
      ::plugin_discovery::detail::record_backend( \
        this, ::plugin_discovery::interface_traits<Interface>::base_class, #Derived, \
        ::plugin_discovery::interface_traits<Interface>::major, \
        ::plugin_discovery::interface_traits<Interface>::minor, PLUGIN_DISCOVERY_ABI_TAG); \
    } \
  }; \
  static PluginDiscoveryRecord##UniqueID g_plugin_discovery_record_##UniqueID; \
  }

#define PLUGIN_DISCOVERY_REGISTER_INTERNAL_HOP_(Derived, Interface, UniqueID) \
  PLUGIN_DISCOVERY_REGISTER_INTERNAL_(Derived, Interface, UniqueID)

// Registers Derived as a backend implementing Interface. Interface must derive from
// plugin_discovery::Backend and be declared with PLUGIN_DISCOVERY_INTERFACE.
#define PLUGIN_DISCOVERY_REGISTER_BACKEND(Derived, Interface) \
  static_assert(std::is_base_of<::plugin_discovery::Backend, Interface>::value, \
    #Interface " must derive from plugin_discovery::Backend"); \
  PLUGIN_DISCOVERY_REGISTER_INTERNAL_HOP_(Derived, Interface, __COUNTER__)
