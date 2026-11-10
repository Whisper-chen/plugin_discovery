/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
// Differential test: the SAME source, built against plugin_loader and (with
// -DUSE_UPSTREAM) against the real class_loader. Each scenario prints a
// transcript to stdout; run_diff.sh runs every scenario in its own process (class
// loader state such as "an unmanaged instance was created" is process-wide) for
// both implementations and diffs the transcripts.
//
//   scenarios <library_dir> --list
//   scenarios <library_dir> <scenario>
#include <dlfcn.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "test_interfaces.hpp"

#ifdef USE_UPSTREAM
#include "class_loader/class_loader.hpp"
#include "class_loader/exceptions.hpp"
#include "class_loader/multi_library_class_loader.hpp"
namespace api = class_loader;
#else
#include "plugin_loader/plugin_loader.hpp"
namespace api = plugin_loader;
#endif

using test::Animal;
using test::Vehicle;

namespace
{

std::string LIB_A, LIB_B, LIB_C;

void out(const std::string & s) {std::cout << s << std::endl;}
std::string yn(bool b) {return b ? "true" : "false";}

std::string list(const std::vector<std::string> & v)
{
  std::string s = "[";
  for (size_t i = 0; i < v.size(); ++i) {s += (i ? "," : "") + v[i];}
  return s + "]";
}

std::string sorted_list(std::vector<std::string> v)
{
  std::sort(v.begin(), v.end());
  return list(v);
}

// Is the .so still mapped into the process? (the ground truth for "unloaded")
bool mapped(const std::string & path);
std::string M(const std::string & path);

bool mapped(const std::string & path)
{
  void * h = ::dlopen(path.c_str(), RTLD_LAZY | RTLD_NOLOAD);
  if (h == nullptr) {return false;}
  ::dlclose(h);
  return true;
}

std::string M(const std::string & path) {return "mapped=" + yn(mapped(path));}

void state(const std::string & label, const std::string & path, const api::ClassLoader & l)
{
  out(label + ": loader=" + yn(l.isLibraryLoaded()) + " anybody=" +
    yn(l.isLibraryLoadedByAnyClassloader()) + " mapped=" + yn(mapped(path)));
}

// Run f and print what it threw, by type (messages differ between the two
// implementations and are not part of the contract).
void guarded(const std::string & what, const std::function<void()> & f)
{
  try {
    f();
  } catch (const api::NoClassLoaderExistsException &) {
    out(what + " -> NoClassLoaderExistsException");
  } catch (const api::CreateClassException &) {
    out(what + " -> CreateClassException");
  } catch (const api::LibraryLoadException &) {
    out(what + " -> LibraryLoadException");
  } catch (const api::LibraryUnloadException &) {
    out(what + " -> LibraryUnloadException");
  } catch (const api::ClassLoaderException &) {
    out(what + " -> ClassLoaderException");
  } catch (const std::exception &) {
    out(what + " -> std::exception");
  }
}

// ---------------------------------------------------------------------------
// ClassLoader
// ---------------------------------------------------------------------------
void create_kinds()
{
  api::ClassLoader l(LIB_A);
  out("animals " + list(l.getAvailableClasses<Animal>()));
  out("vehicles " + list(l.getAvailableClasses<Vehicle>()));
  {auto p = l.createInstance<Animal>("test::Dog"); out("shared " + p->name());}
  {auto p = l.createUniqueInstance<Animal>("test::Cat"); out("unique " + p->name());}
  Animal * raw = l.createUnmanagedInstance<Animal>("test::Dog");
  out("unmanaged " + raw->name());
  delete raw;
  out("Dog available " + yn(l.isClassAvailable<Animal>("test::Dog")));
  out("Dog as Vehicle " + yn(l.isClassAvailable<Vehicle>("test::Dog")));
  out("Car as Vehicle " + yn(l.isClassAvailable<Vehicle>("test::Car")));
  out("onDemand " + yn(l.isOnDemandLoadUnloadEnabled()));
  out("path matches " + yn(l.getLibraryPath() == LIB_A));
}

void scope_filtering()
{
  api::ClassLoader a(LIB_A), b(LIB_B);
  out("a sees " + list(a.getAvailableClasses<Animal>()));
  out("b sees " + list(b.getAvailableClasses<Animal>()));
  out("a has Fish " + yn(a.isClassAvailable<Animal>("test::Fish")));
  guarded("a.create Fish", [&]() {a.createInstance<Animal>("test::Fish");});
  guarded("b.create Dog", [&]() {b.createInstance<Animal>("test::Dog");});
  guarded("a.create Dog as Vehicle", [&]() {a.createInstance<Vehicle>("test::Dog");});
}

void two_loaders_one_library()
{
  api::ClassLoader l1(LIB_A), l2(LIB_A);
  state("start l1", LIB_A, l1);
  state("start l2", LIB_A, l2);
  out("l2 sees " + list(l2.getAvailableClasses<Animal>()));
  out("l1.unload -> " + std::to_string(l1.unloadLibrary()));
  state("after l1 unload, l1", LIB_A, l1);
  state("after l1 unload, l2", LIB_A, l2);
  out("l1 sees " + list(l1.getAvailableClasses<Animal>()));
  out("l2 sees " + list(l2.getAvailableClasses<Animal>()));
  out("l2.unload -> " + std::to_string(l2.unloadLibrary()));
  state("after l2 unload", LIB_A, l2);
}

void load_counts()
{
  api::ClassLoader l(LIB_A);
  l.loadLibrary();
  l.loadLibrary();
  state("load x3", LIB_A, l);
  for (int i = 0; i < 4; ++i) {
    out("unload -> " + std::to_string(l.unloadLibrary()));
    state("  after", LIB_A, l);
  }
}

void unload_with_live_object()
{
  api::ClassLoader l(LIB_A);
  auto obj = l.createInstance<Animal>("test::Dog");
  out("unload with live object -> " + std::to_string(l.unloadLibrary()));
  state("  after", LIB_A, l);
  out("object still works " + obj->name());
  obj.reset();
  out("unload without objects -> " + std::to_string(l.unloadLibrary()));
  state("  after", LIB_A, l);
}

void other_loader_holds()
{
  // A loader that has not loaded a library that somebody else already holds.
  api::ClassLoader holder(LIB_A);
  api::ClassLoader late(LIB_A, true);
  state("late before", LIB_A, late);
  out("late sees " + list(late.getAvailableClasses<Animal>()));
  out("late has Dog " + yn(late.isClassAvailable<Animal>("test::Dog")));
  guarded("late.create Dog", [&]() {out("  -> " + late.createInstance<Animal>("test::Dog")->name());});
  state("late after create", LIB_A, late);
  out("late sees " + list(late.getAvailableClasses<Animal>()));
  out("holder sees " + list(holder.getAvailableClasses<Animal>()));
}

void unbound_while_shared()
{
  api::ClassLoader l1(LIB_A), l2(LIB_A);
  out("l1.unload -> " + std::to_string(l1.unloadLibrary()));
  state("l1 unbound", LIB_A, l1);
  guarded("l1.create Dog", [&]() {out("  -> " + l1.createInstance<Animal>("test::Dog")->name());});
  guarded("l2.create Dog", [&]() {out("  -> " + l2.createInstance<Animal>("test::Dog")->name());});
  out("l1.unload again -> " + std::to_string(l1.unloadLibrary()));
  state("l1 after second unload", LIB_A, l1);
  l1.loadLibrary();
  state("l1 reloaded", LIB_A, l1);
  out("l1 sees " + list(l1.getAvailableClasses<Animal>()));
  guarded("l1.create Dog", [&]() {out("  -> " + l1.createInstance<Animal>("test::Dog")->name());});
}

void destructor_unloads()
{
  {
    api::ClassLoader l(LIB_A);
    out("inside scope " + M(LIB_A));
  }
  out("after destroy " + M(LIB_A));
  {
    api::ClassLoader l(LIB_A);
    l.loadLibrary();
    l.loadLibrary();
  }
  out("after destroy (loaded 3x) " + M(LIB_A));
}

void ondemand_lazy()
{
  api::ClassLoader l(LIB_A, true);
  out("onDemand " + yn(l.isOnDemandLoadUnloadEnabled()));
  state("before any create", LIB_A, l);
  out("classes before load " + list(l.getAvailableClasses<Animal>()));
  auto p = l.createInstance<Animal>("test::Dog");
  state("after create", LIB_A, l);
  out("classes after load " + list(l.getAvailableClasses<Animal>()));
  p.reset();
  state("after last managed destroyed", LIB_A, l);
}

void ondemand_many_objects()
{
  api::ClassLoader l(LIB_A, true);
  auto p1 = l.createInstance<Animal>("test::Dog");
  auto p2 = l.createUniqueInstance<Animal>("test::Cat");
  p1.reset();
  state("one of two destroyed", LIB_A, l);
  p2.reset();
  state("both destroyed", LIB_A, l);
  auto p3 = l.createInstance<Animal>("test::Dog");
  state("recreated", LIB_A, l);
  p3.reset();
  state("destroyed again", LIB_A, l);
}

void ondemand_unmanaged_blocks_unload()
{
  api::ClassLoader l(LIB_A, true);
  Animal * raw = l.createUnmanagedInstance<Animal>("test::Dog");
  auto managed = l.createInstance<Animal>("test::Cat");
  managed.reset();
  state("managed destroyed after an unmanaged was created", LIB_A, l);
  delete raw;
}

void exceptions()
{
  guarded("load missing library", []() {api::ClassLoader l("/nonexistent/libnope.so");});
  api::ClassLoader l(LIB_A);
  guarded("create unknown class", [&]() {l.createInstance<Animal>("test::Nope");});
  guarded("create with wrong base", [&]() {l.createUniqueInstance<Vehicle>("test::Dog");});
  guarded("create unmanaged unknown", [&]() {l.createUnmanagedInstance<Animal>("test::Nope");});
}

void name_collision()
{
  api::ClassLoader a(LIB_A);
  api::ClassLoader c(LIB_C);
  out("a sees " + list(a.getAvailableClasses<Animal>()));
  out("c sees " + list(c.getAvailableClasses<Animal>()));
  guarded("a.create Dog", [&]() {out("  -> " + a.createInstance<Animal>("test::Dog")->name());});
  guarded("c.create Dog", [&]() {out("  -> " + c.createInstance<Animal>("test::Dog")->name());});
  guarded("a.create Cat", [&]() {out("  -> " + a.createInstance<Animal>("test::Cat")->name());});
}

void reload()
{
  api::ClassLoader l(LIB_A);
  out("unload -> " + std::to_string(l.unloadLibrary()));
  state("unloaded", LIB_A, l);
  out("classes while unloaded " + list(l.getAvailableClasses<Animal>()));
  l.loadLibrary();
  state("reloaded", LIB_A, l);
  out("classes " + list(l.getAvailableClasses<Animal>()));
  out("create " + l.createInstance<Animal>("test::Dog")->name());
  api::ClassLoader other(LIB_B);
  out("other (B) sees " + list(other.getAvailableClasses<Animal>()));
}

void format_names()
{
  out("format foo = " + api::systemLibraryFormat("foo"));
}

void threads()
{
  api::ClassLoader l(LIB_A);
  std::atomic<int> errors{0};
  std::vector<std::thread> ts;
  for (int t = 0; t < 8; ++t) {
    ts.emplace_back([&]() {
        for (int i = 0; i < 300; ++i) {
          try {
            auto p = l.createInstance<Animal>(i % 2 ? "test::Dog" : "test::Cat");
            if (p->name().empty()) {++errors;}
          } catch (...) {++errors;}
        }
      });
  }
  for (auto & t : ts) {t.join();}
  out("errors " + std::to_string(errors.load()));
  state("after threads", LIB_A, l);
  out("unload -> " + std::to_string(l.unloadLibrary()));
  state("after unload", LIB_A, l);
}

// ---------------------------------------------------------------------------
// MultiLibraryClassLoader
// ---------------------------------------------------------------------------
void multi_basic()
{
  api::MultiLibraryClassLoader m(false);
  m.loadLibrary(LIB_A);
  m.loadLibrary(LIB_B);
  std::vector<std::string> libs = m.getRegisteredLibraries();
  out("registered " + std::to_string(libs.size()));
  out("A available " + yn(m.isLibraryAvailable(LIB_A)));
  out("C available " + yn(m.isLibraryAvailable(LIB_C)));
  out("all animals " + sorted_list(m.getAvailableClasses<Animal>()));
  out("A animals " + list(m.getAvailableClassesForLibrary<Animal>(LIB_A)));
  out("B animals " + list(m.getAvailableClassesForLibrary<Animal>(LIB_B)));
  out("Fish available " + yn(m.isClassAvailable<Animal>("test::Fish")));
  out("any Dog " + m.createInstance<Animal>("test::Dog")->name());
  out("any Fish " + m.createUniqueInstance<Animal>("test::Fish")->name());
  out("Dog from A " + m.createInstance<Animal>("test::Dog", LIB_A)->name());
  Animal * raw = m.createUnmanagedInstance<Animal>("test::Bird");
  out("unmanaged " + raw->name());
  delete raw;
  Animal * raw2 = m.createUnmanagedInstance<Animal>("test::Cat", LIB_A);
  out("unmanaged from A " + raw2->name());
  delete raw2;
}

void multi_errors()
{
  api::MultiLibraryClassLoader m(false);
  guarded("create with nothing loaded", [&]() {m.createInstance<Animal>("test::Dog");});
  guarded("create from unloaded library",
    [&]() {m.createInstance<Animal>("test::Dog", LIB_A);});
  guarded("unique from unloaded library",
    [&]() {m.createUniqueInstance<Animal>("test::Dog", LIB_A);});
  guarded("unmanaged from unloaded library",
    [&]() {m.createUnmanagedInstance<Animal>("test::Dog", LIB_A);});
  guarded("classes for unknown library",
    [&]() {m.getAvailableClassesForLibrary<Animal>(LIB_A);});
}

// Upstream crashes (SIGSEGV) in the calls that follow a failed loadLibrary / in the
// "class not found" case; kept as separate scenarios so each is a recorded fact.
void multi_failed_load()
{
  api::MultiLibraryClassLoader m(false);
  guarded("load missing library", [&]() {m.loadLibrary("/nonexistent/libnope.so");});
  out("registered " + std::to_string(m.getRegisteredLibraries().size()));
  m.loadLibrary(LIB_B);
  out("registered " + std::to_string(m.getRegisteredLibraries().size()));
  out("animals " + sorted_list(m.getAvailableClasses<Animal>()));
}

void multi_failed_load_then_create()
{
  api::MultiLibraryClassLoader m(false);
  guarded("load missing library", [&]() {m.loadLibrary("/nonexistent/libnope.so");});
  guarded("create after failed load", [&]() {m.createInstance<Animal>("test::Dog");});
  m.loadLibrary(LIB_A);
  out("create after loading A " + m.createInstance<Animal>("test::Dog")->name());
}

void multi_failed_load_then_other_library()
{
  api::MultiLibraryClassLoader m(false);
  guarded("load missing library", [&]() {m.loadLibrary("/nonexistent/libnope.so");});
  m.loadLibrary(LIB_B);
  guarded("Dog is not in B", [&]() {m.createInstance<Animal>("test::Dog");});
  guarded("Dog from B explicitly", [&]() {m.createInstance<Animal>("test::Dog", LIB_B);});
  out("Fish " + m.createInstance<Animal>("test::Fish")->name());
}

// The original multi_errors sequence, in one piece.
void multi_errors_sequence()
{
  api::MultiLibraryClassLoader m(false);
  guarded("create with nothing loaded", [&]() {m.createInstance<Animal>("test::Dog");});
  guarded("create from unloaded library",
    [&]() {m.createInstance<Animal>("test::Dog", LIB_A);});
  guarded("classes for unknown library",
    [&]() {m.getAvailableClassesForLibrary<Animal>(LIB_A);});
  guarded("load missing library", [&]() {m.loadLibrary("/nonexistent/libnope.so");});
  out("registered after failure " + std::to_string(m.getRegisteredLibraries().size()));
  m.loadLibrary(LIB_B);
  guarded("class not in loaded library", [&]() {m.createInstance<Animal>("test::Dog");});
  guarded("class in other library", [&]() {m.createInstance<Animal>("test::Dog", LIB_B);});
}

void multi_class_not_found()
{
  api::MultiLibraryClassLoader m(false);
  m.loadLibrary(LIB_B);
  guarded("class not in loaded library", [&]() {m.createInstance<Animal>("test::Dog");});
  guarded("class in other library", [&]() {m.createInstance<Animal>("test::Dog", LIB_B);});
}

void multi_load_counts()
{
  api::MultiLibraryClassLoader m(false);
  m.loadLibrary(LIB_A);
  m.loadLibrary(LIB_A);
  out("registered " + std::to_string(m.getRegisteredLibraries().size()));
  for (int i = 0; i < 3; ++i) {
    out("unload -> " + std::to_string(m.unloadLibrary(LIB_A)));
    out("  registered " + std::to_string(m.getRegisteredLibraries().size()) +
      " available " + yn(m.isLibraryAvailable(LIB_A)) + " " + M(LIB_A));
  }
  out("unload unknown -> " + std::to_string(m.unloadLibrary(LIB_B)));
}

void multi_ondemand()
{
  api::MultiLibraryClassLoader m(true);
  m.loadLibrary(LIB_A);
  out("after loadLibrary: available " + yn(m.isLibraryAvailable(LIB_A)) + " " + M(LIB_A));
  {
    auto p = m.createInstance<Animal>("test::Dog");
    out("after create: available " + yn(m.isLibraryAvailable(LIB_A)) + " " + M(LIB_A) + " " +
      p->name());
  }
  out("after destroy: available " + yn(m.isLibraryAvailable(LIB_A)) + " " + M(LIB_A));
  out("registered " + std::to_string(m.getRegisteredLibraries().size()));
}

void multi_destructor()
{
  {
    api::MultiLibraryClassLoader m(false);
    m.loadLibrary(LIB_A);
    out("inside " + M(LIB_A));
  }
  out("after destroy " + M(LIB_A));
}

// ---------------------------------------------------------------------------
// Only meaningful in the executable that links libtest_a directly ("non-pure").
// ---------------------------------------------------------------------------
void linked_directly()
{
  api::ClassLoader b(LIB_B);
  out("b sees " + list(b.getAvailableClasses<Animal>()));
  out("Dog (linked in, no owner) " + yn(b.isClassAvailable<Animal>("test::Dog")));
  guarded("create linked-in Dog", [&]() {out("  -> " + b.createInstance<Animal>("test::Dog")->name());});
  state("B before unload", LIB_B, b);
  out("unload -> " + std::to_string(b.unloadLibrary()));
  state("B after unload (nothing can be unloaded now)", LIB_B, b);
}

const std::map<std::string, std::function<void()>> & scenarios()
{
  static const std::map<std::string, std::function<void()>> m = {
    {"create_kinds", create_kinds},
    {"scope_filtering", scope_filtering},
    {"two_loaders_one_library", two_loaders_one_library},
    {"load_counts", load_counts},
    {"unload_with_live_object", unload_with_live_object},
    {"other_loader_holds", other_loader_holds},
    {"unbound_while_shared", unbound_while_shared},
    {"destructor_unloads", destructor_unloads},
    {"ondemand_lazy", ondemand_lazy},
    {"ondemand_many_objects", ondemand_many_objects},
    {"ondemand_unmanaged_blocks_unload", ondemand_unmanaged_blocks_unload},
    {"exceptions", exceptions},
    {"name_collision", name_collision},
    {"reload", reload},
    {"format_names", format_names},
    {"threads", threads},
    {"multi_basic", multi_basic},
    {"multi_errors", multi_errors},
    {"multi_failed_load", multi_failed_load},
    {"multi_failed_load_then_create", multi_failed_load_then_create},
    {"multi_failed_load_then_other_library", multi_failed_load_then_other_library},
    {"multi_errors_sequence", multi_errors_sequence},
    {"multi_class_not_found", multi_class_not_found},
    {"multi_load_counts", multi_load_counts},
    {"multi_ondemand", multi_ondemand},
    {"multi_destructor", multi_destructor},
    {"linked_directly", linked_directly},
  };
  return m;
}

}  // namespace

int main(int argc, char ** argv)
{
  if (argc < 3) {
    std::cerr << "usage: " << argv[0] << " <library_dir> (--list | <scenario>)\n";
    return 2;
  }
  const std::string dir = argv[1];
  const std::string what = argv[2];
  if (what == "--list") {
    for (const auto & s : scenarios()) {std::cout << s.first << "\n";}
    return 0;
  }
  LIB_A = dir + "/libtest_a.so";
  LIB_B = dir + "/libtest_b.so";
  LIB_C = dir + "/libtest_c.so";

  auto it = scenarios().find(what);
  if (it == scenarios().end()) {
    std::cerr << "unknown scenario " << what << "\n";
    return 2;
  }
  it->second();
  return 0;
}
