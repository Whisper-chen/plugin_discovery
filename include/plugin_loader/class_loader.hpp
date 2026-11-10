/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
// ClassLoader: loads one shared library and creates objects of the classes it
// registered. API and behaviour follow class_loader::ClassLoader.
//
// * A ClassLoader only sees classes of the library it loaded (plus classes that
//   have no owner because their library was opened outside any ClassLoader).
// * loadLibrary()/unloadLibrary() are counted; the library is unbound from this
//   loader when the counts match, and closed when no loader holds it any more.
// * Managed instances (createInstance / createUniqueInstance) are counted. The
//   library is not unloaded while any exist.
// * On-demand mode: load on the first create, unload when the last managed
//   instance is destroyed. Creating an *unmanaged* instance anywhere in the
//   process turns the automatic unload off.
//
// A ClassLoader must outlive the objects it created: their deleters call back
// into it.
#pragma once

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <typeinfo>
#include <vector>

#include "plugin_loader/exceptions.hpp"
#include "plugin_loader/impl.hpp"

namespace plugin_loader
{

// "foo" -> "libfoo.so" (platform prefix + suffix).
std::string systemLibraryFormat(const std::string & library_name);
std::string systemLibrarySuffix();

class ClassLoader
{
public:
  template<typename Base>
  using DeleterType = std::function<void (Base *)>;

  template<typename Base>
  using UniquePtr = std::unique_ptr<Base, DeleterType<Base>>;

  // Non-on-demand loaders load immediately; throws LibraryLoadException on failure.
  explicit ClassLoader(const std::string & library_path, bool ondemand_load_unload = false);
  virtual ~ClassLoader();

  ClassLoader(const ClassLoader &) = delete;
  ClassLoader & operator=(const ClassLoader &) = delete;

  template<class Base>
  std::vector<std::string> getAvailableClasses() const
  {
    return impl::available_classes(typeid(Base).name(), this);
  }

  template<class Base>
  std::shared_ptr<Base> createInstance(const std::string & derived_class_name)
  {
    return std::shared_ptr<Base>(
      createRawInstance<Base>(derived_class_name, true),
      std::bind(&ClassLoader::onPluginDeletion<Base>, this, std::placeholders::_1));
  }

  template<class Base>
  UniquePtr<Base> createUniqueInstance(const std::string & derived_class_name)
  {
    Base * raw = createRawInstance<Base>(derived_class_name, true);
    return UniquePtr<Base>(
      raw, std::bind(&ClassLoader::onPluginDeletion<Base>, this, std::placeholders::_1));
  }

  // The caller owns the object and the library can no longer be unloaded
  // automatically (in on-demand mode) anywhere in the process.
  template<class Base>
  Base * createUnmanagedInstance(const std::string & derived_class_name)
  {
    return createRawInstance<Base>(derived_class_name, false);
  }

  template<class Base>
  bool isClassAvailable(const std::string & class_name) const
  {
    std::vector<std::string> classes = getAvailableClasses<Base>();
    return std::find(classes.begin(), classes.end(), class_name) != classes.end();
  }

  const std::string & getLibraryPath() const;

  // Loaded within *this* loader's scope.
  bool isLibraryLoaded() const;
  // Open in the process, not necessarily through this loader.
  bool isLibraryLoadedByAnyClassloader() const;
  bool isOnDemandLoadUnloadEnabled() const;

  // Idempotent as far as the library goes, but counted: call unloadLibrary() as
  // many times.
  void loadLibrary();

  // Returns how many more unloadLibrary() calls are needed to unbind the library
  // from this loader. Warns and does nothing while managed instances exist.
  int unloadLibrary();

private:
  template<class Base>
  void onPluginDeletion(Base * obj)
  {
    if (obj == nullptr) {
      return;
    }
    std::lock_guard<std::recursive_mutex> lock(plugin_ref_count_mutex_);
    delete obj;
    --plugin_ref_count_;
    if (plugin_ref_count_ == 0 && isOnDemandLoadUnloadEnabled()) {
      if (!hasUnmanagedInstanceBeenCreated()) {
        unloadLibraryInternal(false);
      } else {
        impl::log(
          LogLevel::Warn,
          "cannot unload library %s even though the last managed instance was destroyed: "
          "createUnmanagedInstance was used in this process",
          getLibraryPath().c_str());
      }
    }
  }

  template<class Base>
  Base * createRawInstance(const std::string & derived_class_name, bool managed)
  {
    if (!managed) {
      setUnmanagedInstanceBeenCreated(true);
    }
    if (managed && hasUnmanagedInstanceBeenCreated() && isOnDemandLoadUnloadEnabled()) {
      impl::log(
        LogLevel::Info,
        "creating a managed instance although an unmanaged one exists in this process: "
        "libraries will not be unloaded automatically in on-demand mode");
    }
    if (!isLibraryLoaded()) {
      loadLibrary();
    }

    Base * obj = static_cast<Base *>(
      impl::create_instance(typeid(Base).name(), derived_class_name, this));

    if (managed) {
      std::lock_guard<std::recursive_mutex> lock(plugin_ref_count_mutex_);
      ++plugin_ref_count_;
    }
    return obj;
  }

  static bool hasUnmanagedInstanceBeenCreated();
  static void setUnmanagedInstanceBeenCreated(bool state);

  int unloadLibraryInternal(bool lock_plugin_ref_count);

  bool ondemand_load_unload_;
  std::string library_path_;
  int load_ref_count_;
  std::recursive_mutex load_ref_count_mutex_;
  int plugin_ref_count_;
  std::recursive_mutex plugin_ref_count_mutex_;
  static std::atomic<bool> has_unmanaged_instance_been_created_;
};

}  // namespace plugin_loader
