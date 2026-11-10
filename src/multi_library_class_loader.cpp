/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
#include "plugin_loader/multi_library_class_loader.hpp"

namespace plugin_loader
{

MultiLibraryClassLoader::MultiLibraryClassLoader(bool enable_ondemand_loadunload)
: ondemand_load_unload_(enable_ondemand_loadunload)
{
}

MultiLibraryClassLoader::~MultiLibraryClassLoader()
{
  shutdownAllClassLoaders();
}

bool MultiLibraryClassLoader::isOnDemandLoadUnloadEnabled() const
{
  return ondemand_load_unload_;
}

bool MultiLibraryClassLoader::isLibraryAvailable(const std::string & library_path) const
{
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  // "Registered", not "loaded": in on-demand mode the library is not loaded yet.
  return loaders_.find(library_path) != loaders_.end();
}

std::vector<std::string> MultiLibraryClassLoader::getRegisteredLibraries() const
{
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  std::vector<std::string> out;
  for (const auto & entry : loaders_) {
    out.push_back(entry.first);
  }
  return out;
}

ClassLoader * MultiLibraryClassLoader::getClassLoaderForLibrary(const std::string & library_path)
{
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  auto it = loaders_.find(library_path);
  return it == loaders_.end() ? nullptr : it->second;
}

ClassLoaderVector MultiLibraryClassLoader::getAllAvailableClassLoaders() const
{
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  ClassLoaderVector out;
  for (const auto & entry : loaders_) {
    out.push_back(entry.second);
  }
  return out;
}

void MultiLibraryClassLoader::loadLibrary(const std::string & library_path)
{
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (loaders_.find(library_path) == loaders_.end()) {
    loaders_[library_path] = new ClassLoader(library_path, ondemand_load_unload_);
  }
}

int MultiLibraryClassLoader::unloadLibrary(const std::string & library_path)
{
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  auto it = loaders_.find(library_path);
  if (it == loaders_.end()) {
    return 0;
  }
  const int remaining = it->second->unloadLibrary();
  if (remaining == 0) {
    delete it->second;
    loaders_.erase(it);
  }
  return remaining;
}

void MultiLibraryClassLoader::shutdownAllClassLoaders()
{
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  for (auto & entry : loaders_) {
    delete entry.second;  // ~ClassLoader unloads its library
  }
  loaders_.clear();
}

}  // namespace plugin_loader
