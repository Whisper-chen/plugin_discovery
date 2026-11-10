/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
#include <dlfcn.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>

#include "plugin_loader/exceptions.hpp"
#include "plugin_loader/impl.hpp"

namespace plugin_loader
{
namespace impl
{

// ---- logging -----------------------------------------------------------------
namespace
{

LogLevel threshold()
{
  static const LogLevel level = []() {
      const char * v = std::getenv("PLUGIN_LOADER_LOG_LEVEL");
      if (v == nullptr) {return LogLevel::Warn;}
      if (!std::strcmp(v, "debug")) {return LogLevel::Debug;}
      if (!std::strcmp(v, "info")) {return LogLevel::Info;}
      if (!std::strcmp(v, "error")) {return LogLevel::Error;}
      if (!std::strcmp(v, "none")) {return LogLevel::None;}
      return LogLevel::Warn;
    }();
  return level;
}

}  // namespace

void log(LogLevel level, const char * fmt, ...)
{
  if (level < threshold()) {
    return;
  }
  static const char * const names[] = {"DEBUG", "INFO", "WARN", "ERROR"};
  std::fprintf(stderr, "[plugin_loader %s] ", names[static_cast<int>(level)]);
  va_list args;
  va_start(args, fmt);
  std::vfprintf(stderr, fmt, args);
  va_end(args);
  std::fputc('\n', stderr);
}

// ---- global state ------------------------------------------------------------
namespace
{

// A registered class. Deliberately a plain struct holding a plain function
// pointer: no code from the plugin .so is needed to destroy one, so factories
// can safely be deleted after their library has been dlclose()'d.
struct Factory
{
  std::string class_name;
  std::string base_class_name;
  std::string library_path;
  void * (*create)() = nullptr;
  std::set<const ClassLoader *> owners;  // may contain nullptr == "no owner"

  bool owned_by(const ClassLoader * loader) const {return owners.count(loader) != 0;}
};

struct Library
{
  std::string path;
  void * handle = nullptr;
  std::set<const ClassLoader *> loaders;  // loaders currently holding this library
};

struct State
{
  std::recursive_mutex mutex;
  // base type name -> class name -> factory
  std::map<std::string, std::map<std::string, Factory *>> by_base;
  std::vector<std::unique_ptr<Factory>> live;       // owns the factories in by_base
  // Factories of libraries that were unloaded while the .so stayed mapped (some
  // other reference kept it alive). Its static initializers will not run again on
  // the next dlopen, so these are revived instead.
  std::vector<std::unique_ptr<Factory>> graveyard;
  std::vector<Library> libraries;
  bool non_pure_library_opened = false;
};

// Never destroyed: static initializers of plugin libraries and destructors of
// static objects may run in any order at process exit.
State & state()
{
  static State * s = new State();
  return *s;
}

// Context for the dlopen() in progress on this thread. Static initializers run on
// the dlopen()ing thread, so register_factory() can read it.
thread_local const ClassLoader * t_active_loader = nullptr;
thread_local std::string t_loading_library;

Library * find_library(State & s, const std::string & path)
{
  for (auto & lib : s.libraries) {
    if (lib.path == path) {return &lib;}
  }
  return nullptr;
}

void for_each_live_factory_of(State & s, const std::string & path, const std::function<void(Factory &)> & fn)
{
  for (auto & f : s.live) {
    if (f->library_path == path) {fn(*f);}
  }
}

void erase_from_base_map(State & s, const Factory * f)
{
  auto base_it = s.by_base.find(f->base_class_name);
  if (base_it == s.by_base.end()) {return;}
  auto it = base_it->second.find(f->class_name);
  if (it != base_it->second.end() && it->second == f) {base_it->second.erase(it);}
}

// Move every live factory of `path` to the graveyard.
void bury_factories_of(State & s, const std::string & path)
{
  for (auto it = s.live.begin(); it != s.live.end();) {
    if ((*it)->library_path == path) {
      erase_from_base_map(s, it->get());
      s.graveyard.push_back(std::move(*it));
      it = s.live.erase(it);
    } else {
      ++it;
    }
  }
}

void drop_graveyard_of(State & s, const std::string & path)
{
  s.graveyard.erase(
    std::remove_if(
      s.graveyard.begin(), s.graveyard.end(),
      [&](const std::unique_ptr<Factory> & f) {return f->library_path == path;}),
    s.graveyard.end());
}

// Remove factories registered by a dlopen() that failed half-way.
void discard_factories_of(State & s, const std::string & path)
{
  for (auto it = s.live.begin(); it != s.live.end();) {
    if ((*it)->library_path == path) {
      erase_from_base_map(s, it->get());
      it = s.live.erase(it);
    } else {
      ++it;
    }
  }
}

bool still_mapped(const std::string & path)
{
  void * h = ::dlopen(path.c_str(), RTLD_LAZY | RTLD_NOLOAD);
  if (h == nullptr) {return false;}
  ::dlclose(h);
  return true;
}

}  // namespace

// ---- registration ------------------------------------------------------------
void register_factory(
  const std::string & class_name, const std::string & base_class_name, void * (*create)())
{
  State & s = state();
  std::lock_guard<std::recursive_mutex> lock(s.mutex);

  log(LogLevel::Debug, "registering class %s (loader=%p, library=%s)", class_name.c_str(),
    static_cast<const void *>(t_active_loader), t_loading_library.c_str());

  if (t_active_loader == nullptr) {
    log(
      LogLevel::Debug,
      "a library containing plugins was opened without going through ClassLoader (for example "
      "it is linked into the executable); no library opened from now on can be unloaded safely");
    s.non_pure_library_opened = true;
  }

  auto factory = std::make_unique<Factory>();
  factory->class_name = class_name;
  factory->base_class_name = base_class_name;
  factory->library_path = t_loading_library;
  factory->create = create;
  factory->owners.insert(t_active_loader);

  auto & classes = s.by_base[base_class_name];
  auto existing = classes.find(class_name);
  if (existing != classes.end()) {
    log(
      LogLevel::Warn,
      "SEVERE WARNING: name collision for plugin class %s: the new factory OVERWRITES the "
      "existing one. This happens when plugin libraries are linked into the executable, or "
      "when two libraries register the same class name.",
      class_name.c_str());
    const Factory * old = existing->second;
    s.live.erase(
      std::remove_if(
        s.live.begin(), s.live.end(),
        [old](const std::unique_ptr<Factory> & f) {return f.get() == old;}),
      s.live.end());
  }
  classes[class_name] = factory.get();
  s.live.push_back(std::move(factory));
}

// ---- queries / creation ------------------------------------------------------
std::vector<std::string> available_classes(
  const std::string & base_class_name, const ClassLoader * loader)
{
  State & s = state();
  std::lock_guard<std::recursive_mutex> lock(s.mutex);

  std::vector<std::string> owned, unowned;
  auto it = s.by_base.find(base_class_name);
  if (it != s.by_base.end()) {
    for (const auto & entry : it->second) {
      if (entry.second->owned_by(loader)) {
        owned.push_back(entry.first);
      } else if (entry.second->owned_by(nullptr)) {
        unowned.push_back(entry.first);
      }
    }
  }
  owned.insert(owned.end(), unowned.begin(), unowned.end());
  return owned;
}

void * create_instance(
  const std::string & base_class_name, const std::string & class_name, ClassLoader * loader)
{
  void * (*create)() = nullptr;
  {
    State & s = state();
    std::lock_guard<std::recursive_mutex> lock(s.mutex);
    auto base_it = s.by_base.find(base_class_name);
    const Factory * factory = nullptr;
    if (base_it != s.by_base.end()) {
      auto it = base_it->second.find(class_name);
      if (it != base_it->second.end()) {factory = it->second;}
    }
    if (factory == nullptr) {
      log(LogLevel::Error, "no factory registered for class %s", class_name.c_str());
    } else if (factory->owned_by(loader)) {
      create = factory->create;
    } else if (factory->owned_by(nullptr)) {
      log(
        LogLevel::Debug,
        "class %s has no owner (its library was opened outside ClassLoader); creating anyway",
        class_name.c_str());
      create = factory->create;
    }
  }
  // Run the constructor without holding the lock: it may use a ClassLoader itself.
  if (create == nullptr) {
    throw CreateClassException("Could not create instance of type " + class_name);
  }
  return create();
}

// ---- library management ------------------------------------------------------
void load_library(const std::string & path, ClassLoader * loader)
{
  State & s = state();
  std::lock_guard<std::recursive_mutex> lock(s.mutex);

  if (Library * lib = find_library(s, path)) {
    // Already open: give this loader access to what is there.
    lib->loaders.insert(loader);
    for_each_live_factory_of(s, path, [&](Factory & f) {
        f.owners.insert(loader);
      });
    return;
  }

  const ClassLoader * saved_loader = t_active_loader;
  const std::string saved_library = t_loading_library;
  t_active_loader = loader;
  t_loading_library = path;
  void * handle = ::dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
  const char * err = handle == nullptr ? ::dlerror() : nullptr;
  const std::string err_text = err != nullptr ? err : "";
  t_active_loader = saved_loader;
  t_loading_library = saved_library;

  if (handle == nullptr) {
    discard_factories_of(s, path);
    throw LibraryLoadException("Could not load library " + path + ": " + err_text);
  }

  // The library was still mapped from an earlier load, so its static initializers
  // did not run again: bring its factories back.
  for (auto it = s.graveyard.begin(); it != s.graveyard.end();) {
    if ((*it)->library_path == path) {
      (*it)->owners.clear();
      (*it)->owners.insert(loader);
      s.by_base[(*it)->base_class_name][(*it)->class_name] = it->get();
      s.live.push_back(std::move(*it));
      it = s.graveyard.erase(it);
    } else {
      ++it;
    }
  }

  Library lib;
  lib.path = path;
  lib.handle = handle;
  lib.loaders.insert(loader);
  s.libraries.push_back(std::move(lib));
}

void unload_library(const std::string & path, ClassLoader * loader)
{
  State & s = state();
  std::lock_guard<std::recursive_mutex> lock(s.mutex);

  if (s.non_pure_library_opened) {
    log(
      LogLevel::Warn,
      "cannot unload %s: a library with plugins was opened outside ClassLoader, so no library "
      "can be unloaded safely any more",
      path.c_str());
    return;
  }
  Library * lib = find_library(s, path);
  if (lib == nullptr) {
    return;
  }

  // This loader gives up its access (it no longer sees the library's classes). The
  // library itself stays open while any other loader still holds it, and is closed
  // when the last one lets go.
  lib->loaders.erase(loader);
  for_each_live_factory_of(s, path, [&](Factory & f) {f.owners.erase(loader);});
  if (!lib->loaders.empty()) {
    return;
  }

  bury_factories_of(s, path);
  void * handle = lib->handle;
  s.libraries.erase(
    std::remove_if(
      s.libraries.begin(), s.libraries.end(),
      [&](const Library & l) {return l.path == path;}),
    s.libraries.end());

  if (::dlclose(handle) != 0) {
    const char * err = ::dlerror();
    throw LibraryUnloadException(
            "Could not unload library " + path + ": " + (err != nullptr ? err : "unknown error"));
  }
  if (still_mapped(path)) {
    // dlclose() only dropped one reference; keep the factories for the next load.
    log(LogLevel::Debug, "%s is still mapped after dlclose; keeping its factories", path.c_str());
  } else {
    drop_graveyard_of(s, path);
  }
}

bool is_library_loaded_by_anybody(const std::string & path)
{
  State & s = state();
  std::lock_guard<std::recursive_mutex> lock(s.mutex);
  return find_library(s, path) != nullptr;
}

bool is_library_loaded(const std::string & path, const ClassLoader * loader)
{
  State & s = state();
  std::lock_guard<std::recursive_mutex> lock(s.mutex);
  const Library * lib = find_library(s, path);
  return lib != nullptr && lib->loaders.count(loader) != 0;
}

std::vector<std::string> all_libraries_used_by(const ClassLoader * loader)
{
  State & s = state();
  std::lock_guard<std::recursive_mutex> lock(s.mutex);
  std::vector<std::string> out;
  for (const auto & lib : s.libraries) {
    if (lib.loaders.count(loader) != 0) {out.push_back(lib.path);}
  }
  return out;
}

bool has_non_pure_plugin_library_been_opened()
{
  State & s = state();
  std::lock_guard<std::recursive_mutex> lock(s.mutex);
  return s.non_pure_library_opened;
}

}  // namespace impl
}  // namespace plugin_loader
