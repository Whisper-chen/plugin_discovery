/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
// MultiLibraryClassLoader: one ClassLoader per library, behind a single object.
// API follows class_loader::MultiLibraryClassLoader.
#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "plugin_loader/class_loader.hpp"
#include "plugin_loader/exceptions.hpp"

namespace plugin_loader
{

typedef std::string LibraryPath;
typedef std::map<LibraryPath, ClassLoader *> LibraryToClassLoaderMap;
typedef std::vector<ClassLoader *> ClassLoaderVector;

class MultiLibraryClassLoader
{
public:
  // enable_ondemand_loadunload is handed to every ClassLoader it creates.
  explicit MultiLibraryClassLoader(bool enable_ondemand_loadunload);
  virtual ~MultiLibraryClassLoader();

  MultiLibraryClassLoader(const MultiLibraryClassLoader &) = delete;
  MultiLibraryClassLoader & operator=(const MultiLibraryClassLoader &) = delete;

  // Create from the first library (in path order) that defines the class.
  // Throws CreateClassException if none does.
  template<class Base>
  std::shared_ptr<Base> createInstance(const std::string & class_name)
  {
    ClassLoader * loader = requireLoaderForClass<Base>(class_name);
    return loader->createInstance<Base>(class_name);
  }

  // Create from a specific library. Throws NoClassLoaderExistsException if it
  // was not loaded through this object.
  template<class Base>
  std::shared_ptr<Base> createInstance(
    const std::string & class_name, const std::string & library_path)
  {
    return requireLoaderForLibrary(library_path)->createInstance<Base>(class_name);
  }

  template<class Base>
  ClassLoader::UniquePtr<Base> createUniqueInstance(const std::string & class_name)
  {
    ClassLoader * loader = requireLoaderForClass<Base>(class_name);
    return loader->createUniqueInstance<Base>(class_name);
  }

  template<class Base>
  ClassLoader::UniquePtr<Base> createUniqueInstance(
    const std::string & class_name, const std::string & library_path)
  {
    return requireLoaderForLibrary(library_path)->createUniqueInstance<Base>(class_name);
  }

  // The plugin system cannot unload safely once unmanaged instances exist.
  template<class Base>
  Base * createUnmanagedInstance(const std::string & class_name)
  {
    ClassLoader * loader = requireLoaderForClass<Base>(class_name);
    return loader->createUnmanagedInstance<Base>(class_name);
  }

  template<class Base>
  Base * createUnmanagedInstance(const std::string & class_name, const std::string & library_path)
  {
    return requireLoaderForLibrary(library_path)->createUnmanagedInstance<Base>(class_name);
  }

  template<class Base>
  bool isClassAvailable(const std::string & class_name) const
  {
    std::vector<std::string> classes = getAvailableClasses<Base>();
    return std::find(classes.begin(), classes.end(), class_name) != classes.end();
  }

  bool isLibraryAvailable(const std::string & library_path) const;

  template<class Base>
  std::vector<std::string> getAvailableClasses() const
  {
    std::vector<std::string> out;
    for (ClassLoader * loader : getAllAvailableClassLoaders()) {
      std::vector<std::string> classes = loader->getAvailableClasses<Base>();
      out.insert(out.end(), classes.begin(), classes.end());
    }
    return out;
  }

  template<class Base>
  std::vector<std::string> getAvailableClassesForLibrary(const std::string & library_path)
  {
    ClassLoader * loader = getClassLoaderForLibrary(library_path);
    if (loader == nullptr) {
      throw NoClassLoaderExistsException(
              "There is no ClassLoader in MultiLibraryClassLoader bound to library " +
              library_path + ". Ensure you called MultiLibraryClassLoader::loadLibrary()");
    }
    return loader->getAvailableClasses<Base>();
  }

  std::vector<std::string> getRegisteredLibraries() const;

  void loadLibrary(const std::string & library_path);

  // Returns how many more unloadLibrary() calls it takes to unbind the library.
  int unloadLibrary(const std::string & library_path);

private:
  bool isOnDemandLoadUnloadEnabled() const;

  ClassLoader * getClassLoaderForLibrary(const std::string & library_path);

  template<typename Base>
  ClassLoader * getClassLoaderForClass(const std::string & class_name)
  {
    for (ClassLoader * loader : getAllAvailableClassLoaders()) {
      if (!loader->isLibraryLoaded()) {
        loader->loadLibrary();
      }
      if (loader->isClassAvailable<Base>(class_name)) {
        return loader;
      }
    }
    return nullptr;
  }

  ClassLoaderVector getAllAvailableClassLoaders() const;

  // Unloads and destroys every ClassLoader.
  void shutdownAllClassLoaders();

  ClassLoader * requireLoaderForLibrary(const std::string & library_path)
  {
    ClassLoader * loader = getClassLoaderForLibrary(library_path);
    if (loader == nullptr) {
      throw NoClassLoaderExistsException(
              "Could not create instance as there is no ClassLoader in MultiLibraryClassLoader "
              "bound to library " + library_path +
              ". Ensure you called MultiLibraryClassLoader::loadLibrary()");
    }
    return loader;
  }

  template<class Base>
  ClassLoader * requireLoaderForClass(const std::string & class_name)
  {
    ClassLoader * loader = getClassLoaderForClass<Base>(class_name);
    if (loader == nullptr) {
      throw CreateClassException(
              "MultiLibraryClassLoader: Could not create object of class type " + class_name +
              " as no factory exists for it. Make sure that the library exists and was "
              "explicitly loaded through MultiLibraryClassLoader::loadLibrary()");
    }
    return loader;
  }

  bool ondemand_load_unload_;
  mutable std::recursive_mutex mutex_;
  LibraryToClassLoaderMap loaders_;
};

}  // namespace plugin_loader
