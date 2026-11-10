/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
#include "plugin_discovery/plugin_registry.hpp"

#include <dlfcn.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>

namespace fs = std::filesystem;

namespace plugin_discovery
{

// ---- records written by PLUGIN_DISCOVERY_REGISTER_BACKEND -----------------------------

namespace
{

struct BackendRecord
{
  std::string library;  // path of the library holding the record, from dladdr
  std::string base_class;
  std::string class_name;
  int major;
  int minor;
  std::string abi_tag;
};

std::mutex & records_mutex()
{
  static std::mutex m;
  return m;
}

std::vector<BackendRecord> & records()
{
  static auto * r = new std::vector<BackendRecord>();  // leaked: used by static initializers
  return *r;
}

std::string canonical(const std::string & path)
{
  std::error_code ec;
  const fs::path c = fs::weakly_canonical(path, ec);
  return ec ? path : c.string();
}

}  // namespace

namespace detail
{

void record_backend(
  const void * anchor, const char * base_class, const char * class_name, int major, int minor,
  const char * abi_tag)
{
  Dl_info info{};
  std::string library;
  if (dladdr(anchor, &info) != 0 && info.dli_fname != nullptr) {
    library = canonical(info.dli_fname);
  }
  std::lock_guard<std::mutex> lock(records_mutex());
  records().push_back({library, base_class, class_name, major, minor, abi_tag});
}

}  // namespace detail

const char * to_string(Skipped::Kind kind)
{
  switch (kind) {
    case Skipped::Kind::NotListed: return "not_listed";
    case Skipped::Kind::Disabled: return "disabled";
    case Skipped::Kind::Missing: return "missing";
    case Skipped::Kind::LacksCapability: return "lacks_capability";
    case Skipped::Kind::Incompatible: return "incompatible";
    case Skipped::Kind::LoadFailed: return "load_failed";
    case Skipped::Kind::Unsupported: return "unsupported";
    case Skipped::Kind::Fault: return "fault";
    case Skipped::Kind::DuplicateDevice: return "duplicate_device";
  }
  return "?";
}

// ---- helpers ---------------------------------------------------------------------------

namespace
{

constexpr const char * kManifestSuffix = ".plugin.yaml";
constexpr const char * kPathEnv = "PLUGIN_DISCOVERY_PATH";
constexpr const char * kConfigPathEnv = "PLUGIN_DISCOVERY_CONFIG_PATH";
constexpr const char * kConfigSuffix = ".config.yaml";
constexpr const char * kPlatformConfigDir = "/etc/plugin_discovery/platform.d";
constexpr const char * kCustomerConfigDir = "/etc/plugin_discovery/config.d";

std::vector<std::string> split(const std::string & value, char sep)
{
  std::vector<std::string> out;
  std::stringstream ss(value);
  std::string item;
  while (std::getline(ss, item, sep)) {
    item.erase(0, item.find_first_not_of(" \t"));
    item.erase(item.find_last_not_of(" \t") + 1);
    if (!item.empty()) {
      out.push_back(item);
    }
  }
  return out;
}

std::vector<std::string> split_env(const char * name, char sep)
{
  const char * value = std::getenv(name);
  return value == nullptr ? std::vector<std::string>{} : split(value, sep);
}

bool ends_with(const std::string & s, const std::string & suffix)
{
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string to_lower(std::string s)
{
  std::transform(s.begin(), s.end(), s.begin(),
    [](unsigned char c) {return static_cast<char>(std::tolower(c));});
  return s;
}

bool contains(const std::vector<std::string> & v, const std::string & x)
{
  return std::find(v.begin(), v.end(), x) != v.end();
}

std::string resolve_library(const std::string & lib, const fs::path & manifest_dir)
{
  const fs::path p(lib);
  if (p.is_absolute()) {
    return lib;
  }
  std::error_code ec;
  if (fs::exists(manifest_dir / p, ec)) {
    return canonical((manifest_dir / p).string());
  }
  for (const auto & dir : split_env("LD_LIBRARY_PATH", ':')) {
    if (fs::exists(fs::path(dir) / p, ec)) {
      return canonical((fs::path(dir) / p).string());
    }
  }
  return lib;  // let dlopen() search its default locations
}

std::string required_string(const YAML::Node & node, const char * key)
{
  const YAML::Node v = node[key];
  if (!v || !v.IsScalar() || v.as<std::string>().empty()) {
    throw std::runtime_error(std::string("missing or empty '") + key + "'");
  }
  return v.as<std::string>();
}

std::vector<std::string> string_list(const YAML::Node & node, const char * key, bool lower)
{
  std::vector<std::string> out;
  if (node[key]) {
    for (const auto & s : node[key].as<std::vector<std::string>>()) {
      out.push_back(lower ? to_lower(s) : s);
    }
  }
  return out;
}

void parse_manifest(const fs::path & file, std::vector<PluginInfo> & out)
{
  YAML::Node root;
  try {
    root = YAML::LoadFile(file.string());
  } catch (const std::exception & e) {
    std::cerr << "[plugin_discovery] skip manifest " << file << ": " << e.what() << std::endl;
    return;
  }
  const YAML::Node plugins = root["plugins"];
  if (!plugins || !plugins.IsSequence()) {
    std::cerr << "[plugin_discovery] skip manifest " << file
              << ": top-level 'plugins' list is missing" << std::endl;
    return;
  }

  size_t index = 0;
  for (const auto & entry : plugins) {
    try {
      if (entry["platforms"]) {
        throw std::runtime_error(
                "'platforms' is no longer supported: plugin_discovery does not select by SoC; "
                "list the backend in a platform configuration instead");
      }
      PluginInfo info;
      info.name = required_string(entry, "name");
      info.class_name = required_string(entry, "class");
      info.base_class = required_string(entry, "base");
      info.library = required_string(entry, "library");
      info.domain = to_lower(required_string(entry, "domain"));
      info.backend = entry["backend"] ? to_lower(entry["backend"].as<std::string>()) : "";
      info.origin = entry["origin"] ? to_lower(entry["origin"].as<std::string>()) : "customer";
      if (info.origin != "qcom" && info.origin != "customer") {
        throw std::runtime_error("origin must be 'qcom' or 'customer', not '" + info.origin + "'");
      }
      info.priority = entry["priority"] ? entry["priority"].as<int>() : 0;
      info.capabilities = string_list(entry, "capabilities", false);
      info.manifest_path = file.string();
      info.library_path = resolve_library(info.library, file.parent_path());
      out.push_back(std::move(info));
    } catch (const std::exception & e) {
      std::cerr << "[plugin_discovery] skip entry #" << index << " in " << file << ": "
                << e.what() << std::endl;
    }
    ++index;
  }
}

std::vector<fs::path> files_in(const std::string & dir, const std::string & suffix, bool recursive)
{
  std::vector<fs::path> files;
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) {
    return files;
  }
  if (recursive) {
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
      if (it->is_regular_file() && ends_with(it->path().filename().string(), suffix)) {
        files.push_back(it->path());
      }
    }
  } else {
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
      if (it->is_regular_file() && ends_with(it->path().filename().string(), suffix)) {
        files.push_back(it->path());
      }
    }
  }
  std::sort(files.begin(), files.end());  // results must not depend on directory order
  return files;
}

std::string env_name_for(const std::string & domain)
{
  std::string var = "PLUGIN_DISCOVERY_BACKEND_";
  for (unsigned char c : domain) {
    var += std::isalnum(c) ? static_cast<char>(std::toupper(c)) : '_';
  }
  return var;
}

}  // namespace

// ---- PluginInfo / PluginRegistry --------------------------------------------------------

bool PluginInfo::has_capabilities(const std::vector<std::string> & required) const
{
  return std::all_of(required.begin(), required.end(),
           [this](const std::string & c) {return contains(capabilities, c);});
}

PluginRegistry::PluginRegistry(
  std::vector<std::string> search_paths, std::vector<std::string> config_paths)
: search_paths_(std::move(search_paths)), config_paths_(std::move(config_paths))
{
  if (search_paths_.empty()) {
    search_paths_ = split_env(kPathEnv, ':');
  }
  if (config_paths_.empty()) {
    config_paths_ = split_env(kConfigPathEnv, ':');
  }
  if (config_paths_.empty()) {
    config_paths_ = {kPlatformConfigDir, kCustomerConfigDir};
  }
}

size_t PluginRegistry::scan()
{
  plugins_.clear();
  for (const auto & dir : search_paths_) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
      std::cerr << "[plugin_discovery] search path is not a directory: " << dir << std::endl;
      continue;
    }
    for (const auto & file : files_in(dir, kManifestSuffix, true)) {
      std::vector<PluginInfo> found;
      parse_manifest(file, found);
      for (auto & info : found) {
        auto existing = std::find_if(plugins_.begin(), plugins_.end(),
            [&](const PluginInfo & p) {return p.name == info.name;});
        if (existing == plugins_.end()) {
          plugins_.push_back(std::move(info));
        } else if (info.is_customer() && !existing->is_customer()) {
          std::cerr << "[plugin_discovery] INFO: customer plugin '" << info.name << "' ("
                    << info.manifest_path << ") replaces the QCOM one (" << existing->manifest_path
                    << ")" << std::endl;
          *existing = std::move(info);
        } else {
          std::cerr << "[plugin_discovery] WARN: duplicate plugin '" << info.name << "' in "
                    << info.manifest_path << " ignored (already defined in "
                    << existing->manifest_path << ")" << std::endl;
        }
      }
    }
  }
  read_config();
  return plugins_.size();
}

namespace
{

struct ConfigFile
{
  fs::path path;
  bool customer_dir;
};

// What one file says about one domain, parsed before anything is applied so that a
// malformed file changes nothing.
struct DomainOp
{
  std::string domain;
  bool reset = false;
  std::vector<std::string> disable;
  std::vector<std::string> backends;
  bool has_compat = false;
  bool compat = false;
};

struct ParsedConfig
{
  std::string file;
  bool customer;
  std::vector<DomainOp> ops;
};

}  // namespace

void PluginRegistry::read_config()
{
  config_.clear();
  config_files_.clear();

  // Every file of every directory takes part; nothing replaces or masks another file.
  // The customer directory is the customer tier, any other directory the platform tier.
  std::vector<ConfigFile> files;
  for (const auto & dir : config_paths_) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
      continue;
    }
    const bool customer_dir =
      fs::path(dir).lexically_normal() == fs::path(kCustomerConfigDir).lexically_normal();
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
      const std::string name = it->path().filename().string();
      if (!ends_with(name, kConfigSuffix)) {
        if (ends_with(name, ".yaml") && !ends_with(name, kManifestSuffix)) {
          std::cerr << "[plugin_discovery] WARN: ignored " << it->path()
                    << ": configuration files must be named *" << kConfigSuffix << std::endl;
        }
        continue;
      }
      std::error_code probe_ec;  // a dangling link must not stop the directory walk
      if (fs::is_regular_file(it->path(), probe_ec)) {
        files.push_back(ConfigFile{it->path(), customer_dir});
      } else if (fs::is_symlink(it->path(), probe_ec)) {
        std::cerr << "[plugin_discovery] WARN: ignored " << it->path()
                  << ": the link does not lead to a file" << std::endl;
      }
    }
  }

  // Ascending file-name order (directory order for equal names); platform files then go
  // first and customer files after (below). The last file wins, so this is also the order
  // of application.
  std::stable_sort(files.begin(), files.end(),
    [](const ConfigFile & a, const ConfigFile & b) {
      return a.path.filename() < b.path.filename();
    });
  std::vector<ParsedConfig> parsed;
  for (const ConfigFile & file : files) {
    try {
      const YAML::Node root = YAML::LoadFile(file.path.string());
      const std::string origin = root["origin"] ? to_lower(root["origin"].as<std::string>()) :
        (file.customer_dir ? "customer" : "qcom");
      const YAML::Node domains = root["domains"];
      if (!domains || !domains.IsMap()) {
        throw std::runtime_error("top-level 'domains' map is missing");
      }
      ParsedConfig pc{file.path.string(), origin == "customer", {}};
      for (const auto & d : domains) {
        DomainOp op;
        op.domain = to_lower(d.first.as<std::string>());
        op.reset = d.second["reset"] && d.second["reset"].as<bool>();
        op.disable = string_list(d.second, "disable", true);
        op.backends = string_list(d.second, "backends", true);
        if (d.second["compat"]) {
          op.has_compat = true;
          op.compat = d.second["compat"].as<bool>();
        }
        pc.ops.push_back(std::move(op));
      }
      parsed.push_back(std::move(pc));
    } catch (const std::exception & e) {
      std::cerr << "[plugin_discovery] skip config " << file.path << ": " << e.what()
                << std::endl;
      config_files_.push_back({file.path.string(), "invalid", e.what()});
    }
  }
  std::stable_sort(parsed.begin(), parsed.end(),
    [](const ParsedConfig & a, const ParsedConfig & b) {return !a.customer && b.customer;});
  for (const auto & pc : parsed) {
    config_files_.push_back({pc.file, "applied", pc.customer ? "customer" : "platform"});
  }

  auto domain_config = [this](const std::string & domain) -> DomainConfig & {
      for (auto & entry : config_) {
        if (entry.first == domain) {
          return entry.second;
        }
      }
      config_.emplace_back(domain, DomainConfig{});
      return config_.back().second;
    };
  for (const auto & pc : parsed) {
    for (const auto & op : pc.ops) {
      DomainConfig & cfg = domain_config(op.domain);
      cfg.configured = true;
      if (op.reset) {
        cfg.entries.clear();
        cfg.disabled.clear();
        cfg.disabled_by.clear();
        cfg.compat = false;
        cfg.compat_file.clear();
      }
      if (op.has_compat) {
        cfg.compat = op.compat;
        cfg.compat_file = pc.file;
      }
      // The file's backends go to the front in the file's own order; a name that is
      // already listed moves there.
      std::vector<ConfigEntry> front;
      for (const auto & n : op.backends) {
        const auto dis = std::find(cfg.disabled.begin(), cfg.disabled.end(), n);
        if (dis != cfg.disabled.end()) {
          cfg.disabled_by.erase(cfg.disabled_by.begin() + (dis - cfg.disabled.begin()));
          cfg.disabled.erase(dis);
        }
        const bool again = std::any_of(front.begin(), front.end(),
            [&](const ConfigEntry & e) {return e.name == n;});
        if (!again) {
          front.push_back({n, pc.customer, pc.file});
        }
      }
      for (const auto & e : cfg.entries) {
        const bool moved = std::any_of(front.begin(), front.end(),
            [&](const ConfigEntry & f) {return f.name == e.name;});
        if (!moved) {
          front.push_back(e);
        }
      }
      cfg.entries = std::move(front);
      for (const auto & n : op.disable) {
        const auto it = std::find_if(cfg.entries.begin(), cfg.entries.end(),
            [&](const ConfigEntry & e) {return e.name == n;});
        if (it != cfg.entries.end()) {
          cfg.entries.erase(it);
        }
        if (!contains(cfg.disabled, n)) {
          cfg.disabled.push_back(n);
          cfg.disabled_by.push_back(pc.file);
        }
      }
    }
  }
}

std::vector<DomainConfigView> PluginRegistry::config_view() const
{
  std::vector<DomainConfigView> out;
  for (const auto & entry : config_) {
    if (!entry.second.configured) {
      continue;
    }
    DomainConfigView v;
    v.domain = entry.first;
    for (const auto & e : entry.second.entries) {
      v.backends.push_back({e.name, e.file, e.customer});
    }
    v.compat = entry.second.compat;
    v.compat_file = entry.second.compat_file;
    for (size_t i = 0; i < entry.second.disabled.size(); ++i) {
      v.disabled.push_back({entry.second.disabled[i], entry.second.disabled_by[i], false});
    }
    out.push_back(std::move(v));
  }
  return out;
}

std::vector<PluginInfo> PluginRegistry::list(const std::string & base_class) const
{
  std::vector<PluginInfo> out;
  for (const auto & p : plugins_) {
    if (base_class.empty() || p.base_class == base_class) {
      out.push_back(p);
    }
  }
  return out;
}

const PluginInfo * PluginRegistry::find(const std::string & name) const
{
  for (const auto & p : plugins_) {
    if (p.name == name) {
      return &p;
    }
  }
  return nullptr;
}

LoadPlan PluginRegistry::plan(
  const std::string & base_class, const std::string & domain_in,
  const std::vector<std::string> & manual_in, const std::vector<std::string> & required_caps) const
{
  const std::string domain = to_lower(domain_in);
  LoadPlan out;

  std::vector<PluginInfo> pool;
  for (const auto & p : plugins_) {
    if (p.base_class != base_class) {
      continue;
    }
    if (!p.has_capabilities(required_caps)) {
      out.skipped.push_back({p.name, Skipped::Kind::LacksCapability,
          "lacks a required capability"});
      continue;
    }
    pool.push_back(p);
  }
  // Customer first, then priority, then name. Used whenever nothing else orders them.
  std::stable_sort(pool.begin(), pool.end(), [](const PluginInfo & a, const PluginInfo & b) {
      if (a.is_customer() != b.is_customer()) {
        return a.is_customer();
      }
      return a.priority != b.priority ? a.priority > b.priority : a.name < b.name;
    });

  // Build the ordered list of wanted backend names.
  std::vector<std::string> wanted;
  for (const auto & m : manual_in) {
    wanted.push_back(to_lower(m));
  }
  std::vector<std::string> disable;
  if (!wanted.empty()) {
    out.how = "manual";
  } else if (!(wanted = split_env(env_name_for(domain).c_str(), ',')).empty()) {
    for (auto & w : wanted) {
      w = to_lower(w);
    }
    out.how = "manual";
  } else {
    for (const auto & entry : config_) {
      if (entry.first == domain && entry.second.configured) {
        out.how = "config";
        for (const auto & e : entry.second.entries) {
          wanted.push_back(e.name);
        }
        out.compat = entry.second.compat;
        disable = entry.second.disabled;
      }
    }
  }

  if (out.how.empty()) {
    out.how = "all";
    out.plugins = pool;
    if (!pool.empty()) {
      out.first = pool.front().name;
    }
    if (!pool.empty()) {
      std::cerr << "[plugin_discovery] WARN: no configuration for domain '" << domain
                << "' (no platform configuration matched this machine; see platform-detect); loading all "
                << pool.size() << " compatible backend(s)" << std::endl;
    }
    return out;
  }

  // In the wanted order; within one backend name, customer plugins come first (pool order).
  for (const auto & w : wanted) {
    bool found = false;
    for (const auto & p : pool) {
      if (p.backend == w) {
        out.plugins.push_back(p);
        found = true;
      }
    }
    if (!found) {
      out.skipped.push_back({w, Skipped::Kind::Missing, "configured but not installed"});
    }
  }
  // A customer plugin with the same backend name as a QCOM one replaces it.
  std::vector<PluginInfo> kept;
  for (const auto & p : out.plugins) {
    const bool shadowed = !p.is_customer() && std::any_of(out.plugins.begin(), out.plugins.end(),
        [&](const PluginInfo & o) {return o.is_customer() && o.backend == p.backend;});
    if (shadowed) {
      out.skipped.push_back({p.name, Skipped::Kind::Disabled,
          "replaced by the customer backend of the same name"});
    } else {
      kept.push_back(p);
    }
  }
  out.plugins = std::move(kept);
  if (!wanted.empty()) {
    out.first = wanted.front();  // not installed, unless a plugin below carries that name
    for (const auto & p : out.plugins) {
      if (p.backend == wanted.front()) {
        out.first = p.name;
        break;
      }
    }
  }
  for (const auto & p : pool) {
    const bool chosen = std::any_of(out.plugins.begin(), out.plugins.end(),
        [&](const PluginInfo & o) {return o.name == p.name;});
    const bool already = std::any_of(out.skipped.begin(), out.skipped.end(),
        [&](const Skipped & s) {return s.name == p.name;});
    if (!chosen && !already) {
      out.skipped.push_back({p.name,
          contains(disable, p.backend) ? Skipped::Kind::Disabled : Skipped::Kind::NotListed,
          out.how == "manual" ? "not requested" : "not in the configuration"});
    }
  }
  return out;
}

void PluginRegistry::check_compatible(
  const PluginInfo & info, const char * base_class, int major, int minor) const
{
  const std::string library = canonical(info.library_path);
  BackendRecord found{};
  bool have = false;
  {
    std::lock_guard<std::mutex> lock(records_mutex());
    for (const auto & r : records()) {
      if (r.class_name == info.class_name && r.base_class == base_class &&
          (r.library.empty() || r.library == library))
      {
        found = r;
        have = true;
      }
    }
  }
  if (!have) {
    throw PluginError(
            "plugin '" + info.name + "': incompatible: '" + info.class_name +
            "' was not registered with PLUGIN_DISCOVERY_REGISTER_BACKEND, so its interface "
            "version is unknown");
  }
  if (found.abi_tag != PLUGIN_DISCOVERY_ABI_TAG) {
    throw PluginError(
            "plugin '" + info.name + "': incompatible: built with " + found.abi_tag +
            ", the application with " + PLUGIN_DISCOVERY_ABI_TAG +
            "; rebuild the backend with the SDK toolchain");
  }
  if (found.major != major || found.minor < minor) {
    throw PluginError(
            "plugin '" + info.name + "': incompatible: built against " + base_class + " " +
            std::to_string(found.major) + "." + std::to_string(found.minor) +
            ", the application needs " + std::to_string(major) + "." + std::to_string(minor) +
            (found.major != major ? " (major version differs)" : " or newer"));
  }
}

void PluginRegistry::report(
  const std::string & domain, const std::string & how, const std::vector<std::string> & loaded,
  const std::vector<Skipped> & skipped)
{
  std::cerr << "[plugin_discovery] INFO: domain '" << domain << "' (" << how << "): ";
  if (loaded.empty()) {
    std::cerr << "no usable backend";
  }
  for (size_t i = 0; i < loaded.size(); ++i) {
    std::cerr << (i ? ", " : "") << loaded[i];
  }
  std::cerr << std::endl;
  for (const auto & s : skipped) {
    const bool loud = s.kind == Skipped::Kind::Fault || s.kind == Skipped::Kind::Incompatible ||
      s.kind == Skipped::Kind::LoadFailed || s.kind == Skipped::Kind::Missing;
    if (s.kind == Skipped::Kind::NotListed || s.kind == Skipped::Kind::LacksCapability) {
      continue;  // the normal case for backends of other platforms; keep the log short
    }
    std::cerr << "[plugin_discovery] " << (loud ? "ERROR" : "INFO") << ":   skipped " << s.name
              << " [" << to_string(s.kind) << "]: " << s.reason << std::endl;
  }
}

}  // namespace plugin_discovery
