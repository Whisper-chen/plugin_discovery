/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
#include "plugin_loader/class_loader.hpp"

namespace plugin_loader
{

std::string systemLibrarySuffix() {return ".so";}

std::string systemLibraryFormat(const std::string & library_name)
{
  return "lib" + library_name + systemLibrarySuffix();
}

std::atomic<bool> ClassLoader::has_unmanaged_instance_been_created_{false};

ClassLoader::ClassLoader(const std::string & library_path, bool ondemand_load_unload)
: ondemand_load_unload_(ondemand_load_unload),
  library_path_(library_path),
  load_ref_count_(0),
  plugin_ref_count_(0)
{
  impl::log(
    LogLevel::Debug, "ClassLoader for %s created (on-demand=%d)", library_path.c_str(),
    ondemand_load_unload ? 1 : 0);
  if (!ondemand_load_unload_) {
    loadLibrary();
  }
}

ClassLoader::~ClassLoader()
{
  impl::log(LogLevel::Debug, "destroying ClassLoader for %s", library_path_.c_str());
  unloadLibraryInternal(true);
}

const std::string & ClassLoader::getLibraryPath() const {return library_path_;}

bool ClassLoader::isLibraryLoaded() const
{
  return impl::is_library_loaded(library_path_, this);
}

bool ClassLoader::isLibraryLoadedByAnyClassloader() const
{
  return impl::is_library_loaded_by_anybody(library_path_);
}

bool ClassLoader::isOnDemandLoadUnloadEnabled() const {return ondemand_load_unload_;}

void ClassLoader::loadLibrary()
{
  std::lock_guard<std::recursive_mutex> lock(load_ref_count_mutex_);
  impl::load_library(library_path_, this);
  ++load_ref_count_;
}

int ClassLoader::unloadLibrary()
{
  return unloadLibraryInternal(true);
}

int ClassLoader::unloadLibraryInternal(bool lock_plugin_ref_count)
{
  std::unique_lock<std::recursive_mutex> plugin_lock(plugin_ref_count_mutex_, std::defer_lock);
  if (lock_plugin_ref_count) {
    plugin_lock.lock();
  }
  std::lock_guard<std::recursive_mutex> lock(load_ref_count_mutex_);

  if (plugin_ref_count_ > 0) {
    impl::log(
      LogLevel::Warn,
      "library %s will NOT be unloaded: %d object(s) created by this ClassLoader still exist",
      library_path_.c_str(), plugin_ref_count_);
  } else if (load_ref_count_ > 0) {
    --load_ref_count_;
    if (load_ref_count_ == 0) {
      impl::unload_library(library_path_, this);
    }
  }
  return load_ref_count_;
}

bool ClassLoader::hasUnmanagedInstanceBeenCreated()
{
  return has_unmanaged_instance_been_created_;
}

void ClassLoader::setUnmanagedInstanceBeenCreated(bool state)
{
  has_unmanaged_instance_been_created_ = state;
}

}  // namespace plugin_loader
