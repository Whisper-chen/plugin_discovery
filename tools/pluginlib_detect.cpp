/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
// pluginlib-detect: show which backends the configuration selects and which file said so.
// Reads manifests and configuration only; no library is loaded and nothing is probed.
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "plugin_discovery/plugin_registry.hpp"

using plugin_discovery::PluginRegistry;

namespace
{

std::vector<std::string> split(const std::string & s)
{
  std::vector<std::string> out;
  std::string cur;
  for (const char c : s + ":") {
    if (c == ':') {
      if (!cur.empty()) {
        out.push_back(cur);
      }
      cur.clear();
    } else {
      cur += c;
    }
  }
  return out;
}

bool contains(const std::vector<std::string> & v, const std::string & s)
{
  return std::find(v.begin(), v.end(), s) != v.end();
}

void usage(std::ostream & os)
{
  os << "usage: pluginlib-detect [-d DOMAIN] [--path DIRS] [--config-path DIRS]\n"
        "\n"
        "Show the merged configuration of each domain and the file each entry came from.\n"
        "Nothing is loaded or probed.\n"
        "  -d, --domain DOMAIN   only this domain\n"
        "  --path DIRS           manifest directories, ':' separated (PLUGIN_DISCOVERY_PATH)\n"
        "  --config-path DIRS    configuration directories, ':' separated, later ones win\n"
        "                        (PLUGIN_DISCOVERY_CONFIG_PATH)\n";
}

}  // namespace

int main(int argc, char ** argv)
{
  std::string only;
  std::vector<std::string> paths, config_paths;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    const bool more = i + 1 < argc;
    if ((a == "-d" || a == "--domain") && more) {
      only = argv[++i];
    } else if (a == "--path" && more) {
      paths = split(argv[++i]);
    } else if (a == "--config-path" && more) {
      config_paths = split(argv[++i]);
    } else if (a == "-h" || a == "--help") {
      usage(std::cout);
      return 0;
    } else {
      usage(std::cerr);
      return 2;
    }
  }
  std::transform(only.begin(), only.end(), only.begin(), ::tolower);

  PluginRegistry registry(paths, config_paths);
  registry.scan();

  std::cout << "Manifest directories:\n";
  for (const auto & d : registry.search_paths()) {
    std::cout << "  " << d << "\n";
  }
  std::cout << "Configuration directories (platform tier first, customer tier after):\n";
  for (const auto & d : registry.config_paths()) {
    std::cout << "  " << d << "\n";
  }
  std::cout << "Configuration files:\n";
  if (registry.config_files().empty()) {
    std::cout << "  (none)\n";
  }
  for (const auto & f : registry.config_files()) {
    std::cout << "  " << f.state << (f.state.size() < 8 ? std::string(8 - f.state.size(), ' ') : "")
              << " " << f.path << (f.note.empty() ? "" : "  (" + f.note + ")") << "\n";
  }

  const auto views = registry.config_view();
  std::set<std::string> domains;
  for (const auto & p : registry.all()) {
    domains.insert(p.domain);
  }
  for (const auto & v : views) {
    domains.insert(v.domain);
  }

  int shown = 0;
  for (const auto & domain : domains) {
    if (!only.empty() && domain != only) {
      continue;
    }
    ++shown;
    const plugin_discovery::DomainConfigView * view = nullptr;
    for (const auto & v : views) {
      if (v.domain == domain) {
        view = &v;
      }
    }
    std::string env = "PLUGIN_DISCOVERY_BACKEND_" + domain;
    std::transform(env.begin(), env.end(), env.begin(), ::toupper);
    std::cout << "\nDomain " << domain << "\n";
    if (const char * v = std::getenv(env.c_str()); v != nullptr && *v != '\0') {
      std::cout << "  " << env << "=" << v << " is set: it replaces the configuration below\n";
    }

    std::vector<std::string> installed;
    for (const auto & p : registry.all()) {
      if (p.domain == domain && !p.backend.empty() && !contains(installed, p.backend)) {
        installed.push_back(p.backend);
      }
    }
    if (view == nullptr) {
      std::cout << "  no configuration: every compatible backend loads (WARN at run time)\n";
    } else {
      std::cout << "  load order (first = default):\n";
      int n = 0;
      for (const auto & b : view->backends) {
        std::cout << "    " << ++n << ". " << b.name << "  [" << (b.customer ? "customer" : "platform")
                  << "] " << b.file << (contains(installed, b.name) ? "" : "  (not installed)")
                  << "\n";
      }
      if (view->backends.empty()) {
        std::cout << "    (nothing)\n";
      }
      std::cout << "  compat: " << (view->compat ? "on" : "off")
                << (view->compat_file.empty() ? "" : "  by " + view->compat_file) << "\n";
      for (const auto & d : view->disabled) {
        std::cout << "  disabled: " << d.name << "  by " << d.file << "\n";
      }
    }
    for (const auto & name : installed) {
      const bool listed = view != nullptr && std::any_of(view->backends.begin(),
          view->backends.end(), [&](const plugin_discovery::ConfigSource & b) {
            return b.name == name;
          });
      const bool disabled = view != nullptr && std::any_of(view->disabled.begin(),
          view->disabled.end(), [&](const plugin_discovery::ConfigSource & b) {
            return b.name == name;
          });
      if (view != nullptr && !listed && !disabled) {
        std::cout << "  installed, not loaded: " << name << "\n";
      }
    }
  }
  if (!only.empty() && shown == 0) {
    std::cerr << "pluginlib-detect: no domain '" << only << "'\n";
    return 1;
  }
  return 0;
}
