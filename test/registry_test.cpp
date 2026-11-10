/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
// Tests for PluginRegistry: manifests, configuration layers, probe results, version / ABI
// checks and device de-duplication. Uses the test backends built from test/backends/.
// Each check prints PASS/FAIL; exit code != 0 on failure.
//
// Usage: registry_test <dir-with-test-backend-libraries>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "plugin_discovery/plugin_registry.hpp"
#include "test_backend.hpp"

namespace fs = std::filesystem;
using plugin_discovery::PluginRegistry;
using plugin_discovery::Skipped;

namespace
{

int failures = 0;

void check(bool ok, const std::string & what)
{
  std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
  if (!ok) {
    ++failures;
  }
}

fs::path g_libs;
fs::path g_root;

void write(const fs::path & path, const std::string & text)
{
  fs::create_directories(path.parent_path());
  std::ofstream(path) << text;
}

// One manifest per backend: name sensor/<cls>, backend <backend>.
void manifest(const std::string & dir, const std::string & cls, const std::string & backend,
  const std::string & origin, int priority = 0, const std::string & caps = "")
{
  write(g_root / dir / ("sensor_" + cls + ".plugin.yaml"),
    "plugins:\n"
    "  - name: sensor/" + cls + "\n"
    "    class: test::" + cls + "\n"
    "    base: test::Sensor\n"
    "    library: " + (g_libs / ("libtest_backend_" + cls + ".so")).string() + "\n"
    "    domain: sensor\n"
    "    backend: " + backend + "\n"
    "    origin: " + origin + "\n"
    "    priority: " + std::to_string(priority) + "\n"
    "    capabilities: [" + caps + "]\n");
}

std::vector<std::string> names(const plugin_discovery::BackendSet<test::Sensor> & set)
{
  std::vector<std::string> out;
  for (const auto & b : set.backends) {
    out.push_back(b.handle->id());
  }
  return out;
}

const Skipped * skipped(const plugin_discovery::BackendSet<test::Sensor> & set,
  const std::string & name)
{
  for (const auto & s : set.skipped) {
    if (s.name == name) {
      return &s;
    }
  }
  return nullptr;
}

std::string join(const std::vector<std::string> & v)
{
  std::string s;
  for (const auto & x : v) {
    s += (s.empty() ? "" : ",") + x;
  }
  return s;
}

PluginRegistry registry(const std::vector<std::string> & config_dirs)
{
  std::vector<std::string> cfg;
  for (const auto & d : config_dirs) {
    cfg.push_back((g_root / d).string());
  }
  if (cfg.empty()) {
    cfg.push_back((g_root / "no-config").string());
  }
  PluginRegistry r({(g_root / "plugins").string(), (g_root / "plugins2").string()}, cfg);
  r.scan();
  return r;
}

}  // namespace

int main(int argc, char ** argv)
{
  if (argc != 2) {
    std::cerr << "usage: registry_test <test-backend-lib-dir>" << std::endl;
    return 2;
  }
  g_libs = fs::absolute(argv[1]);
  g_root = fs::temp_directory_path() / ("pd_registry_test_" + std::to_string(::getpid()));
  fs::remove_all(g_root);
  unsetenv("PLUGIN_DISCOVERY_BACKEND_SENSOR");

  // Installed backends:
  //   Alpha    qcom,     backend alpha, Ok, node /dev/a
  //   Beta     qcom,     backend beta,  Ok, node /dev/shared, priority 5
  //   Gamma    qcom,     backend gamma, Unsupported
  //   Delta    qcom,     backend delta, Fault
  //   Cust     customer, backend cust,  Ok, node /dev/shared (same device as Beta)
  //   Old      qcom,     backend old,   built against Sensor 1.0 (app needs 1.1)
  //   AbiX     qcom,     backend abix,  built with a different ABI tag
  //   BetaCust customer, backend beta,  Ok  (a customer rebuild of "beta")
  manifest("plugins", "Alpha", "alpha", "qcom");
  manifest("plugins", "Beta", "beta", "qcom", 5, "zero_copy");
  manifest("plugins", "Gamma", "gamma", "qcom");
  manifest("plugins", "Delta", "delta", "qcom");
  manifest("plugins", "Old", "old", "qcom");
  manifest("plugins", "AbiX", "abix", "qcom");
  manifest("plugins2", "Cust", "cust", "customer");

  // ---- no configuration: every compatible backend, customer first --------------------
  {
    auto r = registry({});
    check(r.all().size() == 7, "scan finds 7 plugins");
    auto set = r.load<test::Sensor>();
    check(set.how == "all", "no config -> how=all (got " + set.how + ")");
    check(join(names(set)) == "Cust,Alpha",
      "no config: customer first, then priority, then name; Beta lost its only device (got " +
      join(names(set)) + ")");
    check(skipped(set, "sensor/Gamma") &&
      skipped(set, "sensor/Gamma")->kind == Skipped::Kind::Unsupported, "Unsupported is skipped");
    check(skipped(set, "sensor/Delta") &&
      skipped(set, "sensor/Delta")->kind == Skipped::Kind::Fault, "Fault is skipped");
    const Skipped * old = skipped(set, "sensor/Old");
    check(old && old->kind == Skipped::Kind::Incompatible &&
      old->reason.find("1.0") != std::string::npos &&
      old->reason.find("1.1") != std::string::npos,
      "older interface minor rejected, message names both versions");
    const Skipped * abi = skipped(set, "sensor/AbiX");
    check(abi && abi->kind == Skipped::Kind::Incompatible &&
      abi->reason.find("fake-abi") != std::string::npos,
      "ABI tag mismatch rejected, message names the tag");
    // Beta reports only /dev/shared, which Cust already serves: Beta is unloaded again.
    check(skipped(set, "sensor/Beta") &&
      skipped(set, "sensor/Beta")->kind == Skipped::Kind::DuplicateDevice,
      "same device node reported twice: the later backend does not get it");
  }

  // ---- platform config package ----------------------------------------------------------
  write(g_root / "platform" / "50-sensor-iqx.config.yaml",
    "domains:\n  sensor:\n    backends: [alpha, gamma]\n");
  {
    auto r = registry({"platform"});
    auto set = r.load<test::Sensor>();
    check(set.how == "config", "platform config -> how=config");
    check(join(names(set)) == "Alpha", "only the listed backends load (got " +
      join(names(set)) + ")");
    check(skipped(set, "sensor/Beta") &&
      skipped(set, "sensor/Beta")->kind == Skipped::Kind::NotListed,
      "installed but unlisted backend is not_listed");
  }

  // ---- customer config: added first, disable applies to platform entries ---------------
  write(g_root / "customer" / "50-sensor-mine.config.yaml",
    "origin: customer\ndomains:\n  sensor:\n    backends: [cust, beta]\n    disable: [alpha]\n");
  {
    auto r = registry({"platform", "customer"});
    auto set = r.load<test::Sensor>();
    check(join(names(set)) == "Cust" && skipped(set, "sensor/Alpha"),
      "customer entries first, disable removes a platform entry (got " + join(names(set)) + ")");
    check(skipped(set, "sensor/Alpha") &&
      skipped(set, "sensor/Alpha")->kind == Skipped::Kind::Disabled, "disabled backend reported");
  }

  // ---- file names, application order, reset, no replacement or mask -------------------------
  const auto cfg = [](const std::string & dir, const std::string & name, const std::string & body) {
      write(g_root / dir / name, body);
    };
  const std::string cust = "origin: customer\n";
  {
    // A file without the .config.yaml suffix is ignored (and says so).
    cfg("legacy", "iqx.yaml", "domains:\n  sensor:\n    backends: [alpha]\n");
    auto set = registry({"legacy"}).load<test::Sensor>();
    check(set.how == "all", "a file not named *.config.yaml is ignored (how=" + set.how + ")");

    // Within one tier files apply in file-name order: 10 before 20, whatever they list.
    cfg("order", "20-sensor-late.config.yaml", "domains:\n  sensor:\n    backends: [beta]\n");
    cfg("order", "10-sensor-early.config.yaml", "domains:\n  sensor:\n    backends: [alpha]\n");
    set = registry({"order"}).load<test::Sensor>();
    check(join(names(set)) == "Beta,Alpha",
      "files apply in file-name order, the later one goes to the front (got " +
      join(names(set)) + ")");

    // disable acts at once: a later file listing the backend adds it back, an earlier one
    // does not.
    cfg("dis_a", "10-sensor-x.config.yaml", "domains:\n  sensor:\n    disable: [alpha]\n");
    cfg("dis_a", "20-sensor-y.config.yaml", "domains:\n  sensor:\n    backends: [alpha]\n");
    set = registry({"dis_a"}).load<test::Sensor>();
    check(join(names(set)) == "Alpha", "listing a backend after its disable enables it");
    cfg("dis_b", "10-sensor-x.config.yaml", "domains:\n  sensor:\n    backends: [alpha]\n");
    cfg("dis_b", "20-sensor-y.config.yaml", "domains:\n  sensor:\n    disable: [alpha]\n");
    set = registry({"dis_b"}).load<test::Sensor>();
    check(set.backends.empty() && skipped(set, "sensor/Alpha") &&
      skipped(set, "sensor/Alpha")->kind == Skipped::Kind::Disabled,
      "disable after the listing removes it and is reported as disabled");

    // A customer file can undo a platform disable; its entries come first.
    cfg("plat_d", "50-sensor-p.config.yaml",
      "domains:\n  sensor:\n    backends: [alpha, beta]\n    disable: [beta]\n");
    cfg("cust_d", "50-sensor-c.config.yaml", cust + "domains:\n  sensor:\n    backends: [beta]\n");
    set = registry({"plat_d"}).load<test::Sensor>();
    check(join(names(set)) == "Alpha", "platform disable removes beta (got " + join(names(set)) + ")");
    set = registry({"plat_d", "cust_d"}).load<test::Sensor>();
    check(join(names(set)) == "Beta,Alpha", "customer file undoes a platform disable, listed first (got " +
      join(names(set)) + ")");

    // The merged view names the file behind every entry (used by pluginlib-detect).
    {
      PluginRegistry r = registry({"plat_d", "cust_d"});
      const auto view = r.config_view();
      check(view.size() == 1 && view[0].domain == "sensor" && view[0].backends.size() == 2 &&
        view[0].backends[0].name == "beta" && view[0].backends[0].customer &&
        view[0].backends[0].file.find("cust_d") != std::string::npos &&
        view[0].backends[1].name == "alpha" && !view[0].backends[1].customer &&
        view[0].disabled.empty(), "config_view shows order, origin and source file");
      r = registry({"plat_d"});
      const auto v2 = r.config_view();
      check(v2.size() == 1 && v2[0].disabled.size() == 1 && v2[0].disabled[0].name == "beta" &&
        v2[0].disabled[0].file.find("plat_d") != std::string::npos,
        "config_view shows who disabled a backend");
    }

    // reset forgets what earlier files set for the domain.
    cfg("cust_r", "50-sensor-c.config.yaml",
      cust + "domains:\n  sensor:\n    reset: true\n    backends: [beta]\n");
    set = registry({"plat_d", "cust_r"}).load<test::Sensor>();
    check(join(names(set)) == "Beta" && skipped(set, "sensor/Alpha"),
      "reset drops the earlier layers (got " + join(names(set)) + ")");

    // Merge order: each file's backends go to the front in the file's own order; a name
    // already listed moves to the front; the first entry of the final list is the default.
    {
      const auto order = [&](const std::string & dir) {
          std::string out;
          for (const auto & v : registry({dir}).config_view()) {
            for (const auto & b : v.backends) {
              out += (out.empty() ? "" : ",") + b.name;
            }
          }
          return out;
        };
      cfg("front", "50-sensor-a.config.yaml", "domains:\n  sensor:\n    backends: [alpha, beta]\n");
      cfg("front", "60-sensor-b.config.yaml", "domains:\n  sensor:\n    backends: [gamma]\n");
      check(order("front") == "gamma,alpha,beta", "later file's backends go to the front (got " +
        order("front") + ")");
      cfg("front", "70-sensor-c.config.yaml", "domains:\n  sensor:\n    backends: [beta, delta]\n");
      check(order("front") == "beta,delta,gamma,alpha",
        "a listed name moves to the front, file order kept (got " + order("front") + ")");
      cfg("tiers", "90-sensor-p.config.yaml", "domains:\n  sensor:\n    backends: [alpha]\n");
      cfg("cust_t", "10-sensor-c.config.yaml", cust + "domains:\n  sensor:\n    backends: [beta]\n");
      PluginRegistry r = registry({"tiers", "cust_t"});
      const auto v = r.config_view();
      check(v.size() == 1 && v[0].backends.size() == 2 && v[0].backends[0].name == "beta",
        "customer tier is applied after the platform tier whatever the NN");
      set = r.load<test::Sensor>();
      check(set.first == "sensor/Beta" && set.backends.front().handle.info().name == "sensor/Beta",
        "BackendSet::first is the first entry of the final list (got " + set.first + ")");
    }

    // A first entry that is configured but not installed is the default and cannot load.
    {
      cfg("nosuch", "50-sensor-a.config.yaml", "domains:\n  sensor:\n    backends: [alpha]\n");
      cfg("nosuch", "60-sensor-b.config.yaml", "domains:\n  sensor:\n    backends: [nosuch]\n");
      const auto ns = registry({"nosuch"}).load<test::Sensor>();
      check(ns.first == "nosuch" && join(names(ns)) == "Alpha" && skipped(ns, "nosuch") &&
        skipped(ns, "nosuch")->kind == Skipped::Kind::Missing,
        "a configured but not installed first entry stays the default (first=" + ns.first + ")");
    }

    // compat: a scalar, the last file wins, reset restores false, default false.
    {
      cfg("cm", "50-sensor-a.config.yaml",
        "domains:\n  sensor:\n    backends: [alpha]\n    compat: true\n");
      auto v = registry({"cm"}).config_view();
      check(v.size() == 1 && v[0].compat && !v[0].compat_file.empty(), "compat: true is read");
      check(registry({"cm"}).load<test::Sensor>().compat, "BackendSet::compat follows config");
      cfg("cm", "60-sensor-b.config.yaml", "domains:\n  sensor:\n    compat: false\n");
      v = registry({"cm"}).config_view();
      check(v.size() == 1 && !v[0].compat && v[0].backends.size() == 1,
        "a later file's compat wins and leaves the list alone");
      cfg("cm2", "50-sensor-a.config.yaml",
        "domains:\n  sensor:\n    backends: [alpha]\n    compat: true\n");
      cfg("cm2", "60-sensor-b.config.yaml",
        "domains:\n  sensor:\n    reset: true\n    backends: [beta]\n");
      v = registry({"cm2"}).config_view();
      check(v.size() == 1 && !v[0].compat && v[0].compat_file.empty(), "reset restores compat=false");
      cfg("cm3", "50-sensor-a.config.yaml", "domains:\n  sensor:\n    backends: [alpha]\n");
      check(!registry({"cm3"}).load<test::Sensor>().compat, "compat defaults to false");
      cfg("cm4", "50-sensor-a.config.yaml",
        "domains:\n  sensor:\n    backends: [alpha]\n    compat: true\n");
      check(!registry({"cm4"}).load<test::Sensor>({"alpha"}).compat, "manual choice ignores compat");
      // The first entry is the default even when it cannot be used: no silent fallback.
      cfg("firstbad", "50-sensor-a.config.yaml", "domains:\n  sensor:\n    backends: [alpha]\n");
      cfg("firstbad", "60-sensor-b.config.yaml", "domains:\n  sensor:\n    backends: [gamma]\n");
      const auto fb = registry({"firstbad"}).load<test::Sensor>();
      check(fb.first == "sensor/Gamma" && join(names(fb)) == "Alpha",
        "unsupported first entry stays the default, it is not in the loaded set (first=" +
        fb.first + ")");
    }

    // No replacement and no masking: files of the same name in two directories both apply,
    // and an empty file or a link to /dev/null is not a mask.
    cfg("rep_p", "50-sensor-x.config.yaml", "domains:\n  sensor:\n    backends: [alpha]\n");
    cfg("rep_c", "50-sensor-x.config.yaml", cust + "domains:\n  sensor:\n    backends: [beta]\n");
    set = registry({"rep_p", "rep_c"}).load<test::Sensor>();
    check(join(names(set)) == "Beta,Alpha",
      "same-named files in two directories both apply (got " + join(names(set)) + ")");
    cfg("mask_c", "50-sensor-x.config.yaml", "");
    set = registry({"rep_p", "mask_c"}).load<test::Sensor>();
    check(join(names(set)) == "Alpha",
      "an empty same-named file does not mask the earlier one (got " + join(names(set)) + ")");
    fs::create_directories(g_root / "mask_l");
    fs::create_symlink("/dev/null", g_root / "mask_l" / "50-sensor-x.config.yaml");
    set = registry({"rep_p", "mask_l"}).load<test::Sensor>();
    check(join(names(set)) == "Alpha",
      "a link to /dev/null does not mask the earlier file (got " + join(names(set)) + ")");

    // A platform-tier link named <domain>.config.yaml sorts after every NN-... file by name
    // but still applies below the customer tier.
    cfg("lk_plat", "sensor.config.yaml", "domains:\n  sensor:\n    backends: [alpha]\n");
    cfg("lk_cust", "50-sensor-c.config.yaml", cust + "domains:\n  sensor:\n    backends: [beta]\n");
    set = registry({"lk_plat", "lk_cust"}).load<test::Sensor>();
    check(join(names(set)) == "Beta,Alpha" && set.first == "sensor/Beta",
      "the platform link does not beat the customer file (got " + join(names(set)) + ")");

    // A link that leads nowhere is skipped without stopping the directory walk.
    fs::create_directories(g_root / "dangle");
    fs::create_symlink(g_root / "dangle" / "nowhere.yaml", g_root / "dangle" / "a.config.yaml");
    cfg("dangle", "z-sensor.config.yaml", "domains:\n  sensor:\n    backends: [gamma]\n");
    set = registry({"dangle"}).load<test::Sensor>();
    check(set.how == "config", "a dangling link is skipped, the other files still apply");
  }

  // ---- manual selection beats configuration --------------------------------------------
  {
    auto r = registry({"platform", "customer"});
    auto set = r.load<test::Sensor>({"beta"});
    check(set.how == "manual" && join(names(set)) == "Beta", "load({beta}) loads only beta");
    setenv("PLUGIN_DISCOVERY_BACKEND_SENSOR", "alpha,missing", 1);
    set = r.load<test::Sensor>();
    unsetenv("PLUGIN_DISCOVERY_BACKEND_SENSOR");
    check(set.how == "manual" && join(names(set)) == "Alpha", "env var selects backends");
    check(skipped(set, "missing") &&
      skipped(set, "missing")->kind == Skipped::Kind::Missing, "unknown backend name reported");
  }

  // ---- capabilities ---------------------------------------------------------------------
  {
    auto r = registry({});
    auto set = r.load<test::Sensor>({}, {"zero_copy"});
    check(join(names(set)) == "Beta", "required capability keeps only Beta (got " +
      join(names(set)) + ")");
  }

  // ---- customer rebuild of a QCOM backend (same backend name) --------------------------
  manifest("plugins2", "BetaCust", "beta", "customer");
  {
    auto r = registry({"platform", "customer"});
    auto set = r.load<test::Sensor>();
    check(join(names(set)) == "Cust,BetaCust",
      "customer backend with the same name replaces the QCOM one (got " + join(names(set)) + ")");
  }

  // ---- create by name, error text -------------------------------------------------------
  {
    auto r = registry({});
    auto h = r.create<test::Sensor>("sensor/Alpha");
    check(h && h->id() == "Alpha", "create<Sensor>(name)");
    bool threw = false;
    try {
      r.create<test::Sensor>("sensor/Nope");
    } catch (const plugin_discovery::PluginError &) {
      threw = true;
    }
    check(threw, "create of an unknown name throws PluginError");
  }

  // ---- a manifest whose library does not exist: reported, the others still load ---------
  write(g_root / "plugins" / "ghost.plugin.yaml",
    "plugins:\n  - name: sensor/Ghost\n    class: test::Ghost\n    base: test::Sensor\n"
    "    library: libtest_backend_does_not_exist.so\n    domain: sensor\n    backend: ghost\n"
    "    origin: qcom\n");
  {
    auto r = registry({});
    auto set = r.load<test::Sensor>();
    const Skipped * ghost = skipped(set, "sensor/Ghost");
    check(ghost && ghost->kind == Skipped::Kind::LoadFailed && !set.backends.empty(),
      "missing library is load_failed, other backends still load");
  }
  fs::remove(g_root / "plugins" / "ghost.plugin.yaml");

  // ---- manifests with the removed `platforms` field are rejected ------------------------
  write(g_root / "plugins" / "legacy.plugin.yaml",
    "plugins:\n  - name: sensor/Legacy\n    class: x\n    base: test::Sensor\n"
    "    library: libx.so\n    domain: sensor\n    platforms: [iq8]\n");
  {
    auto r = registry({});
    check(r.find("sensor/Legacy") == nullptr, "manifest entry with 'platforms' is skipped");
  }

  fs::remove_all(g_root);
  std::cout << (failures == 0 ? "all passed" : std::to_string(failures) + " failed") << std::endl;
  return failures == 0 ? 0 : 1;
}
