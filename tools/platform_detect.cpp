/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
// platform-detect: chooses the platform configuration of this machine.
//
// The platform packages install one candidate per machine, OS type and domain:
//   <prefix>/share/plugin_discovery/platforms.d/<machine>-<platform>-<domain>.config.yaml
// plugin_discovery never reads them. This tool reads the machine (/sys/devices/soc0/machine,
// e.g. QCS8275) and the OS type (/etc/os-release) and links the matching candidates into the
// platform tier, which plugin_discovery does read:
//   /etc/plugin_discovery/platform.d/<domain>.config.yaml -> the candidate
// It is meant to run once, on the first boot (a systemd oneshot); a stamp file records that
// it ran, and a later run does nothing unless --force is given. The library itself still
// knows nothing about SoCs.
//
// OS types: ubuntu (ID=ubuntu), qclinux (NAME starts with "Qualcomm Linux"). Anything else
// is unknown: no link, a WARN. Names are compared ignoring case.
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifndef PLATFORM_DETECT_CANDIDATES_DIR
#define PLATFORM_DETECT_CANDIDATES_DIR "/usr/share/plugin_discovery/platforms.d"
#endif

namespace fs = std::filesystem;

namespace
{

constexpr const char * kSuffix = ".config.yaml";

std::string lower(std::string s)
{
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {return std::tolower(c);});
  return s;
}

std::string trim(const std::string & s)
{
  const auto b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) {
    return "";
  }
  return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
}

std::string unquote(std::string v)
{
  v = trim(v);
  if (v.size() >= 2 && (v.front() == '"' || v.front() == '\'') && v.back() == v.front()) {
    v = v.substr(1, v.size() - 2);
  }
  return v;
}

std::string read_machine(const std::string & file)
{
  std::ifstream in(file);
  std::string line;
  if (in && std::getline(in, line)) {
    return trim(line);
  }
  return "";
}

// "ubuntu", "qclinux", or "" when the OS type is not (yet) supported.
std::string read_os_type(const std::string & file)
{
  std::ifstream in(file);
  std::string line, id, name;
  while (std::getline(in, line)) {
    const auto eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    const std::string key = trim(line.substr(0, eq));
    if (key == "ID") {
      id = lower(unquote(line.substr(eq + 1)));
    } else if (key == "NAME") {
      name = lower(unquote(line.substr(eq + 1)));
    }
  }
  if (id == "ubuntu") {
    return "ubuntu";
  }
  if (name.rfind("qualcomm linux", 0) == 0) {
    return "qclinux";
  }
  return "";
}

void usage(std::ostream & os)
{
  os << "usage: platform-detect [--force] [--dry-run] [--candidates DIR] [--platform-dir DIR]\n"
        "                       [--stamp FILE] [--machine-file FILE] [--os-release FILE]\n"
        "\n"
        "Links the platform configuration matching this machine and OS type into the platform\n"
        "tier of plugin_discovery. Runs once: with the stamp file present it does nothing\n"
        "unless --force is given (then the links it made earlier are refreshed).\n"
        "  --candidates DIR     default " PLATFORM_DETECT_CANDIDATES_DIR "\n"
        "  --platform-dir DIR   default /etc/plugin_discovery/platform.d\n"
        "  --stamp FILE         default /var/lib/plugin_discovery/platform-detect.done\n"
        "  --machine-file FILE  default /sys/devices/soc0/machine\n"
        "  --os-release FILE    default /etc/os-release\n";
}

bool points_into(const fs::path & link, const fs::path & dir)
{
  std::error_code ec;
  const fs::path target = fs::read_symlink(link, ec);
  return !ec && target.lexically_normal().string().rfind(dir.lexically_normal().string(), 0) == 0;
}

}  // namespace

int main(int argc, char ** argv)
{
  std::string candidates = PLATFORM_DETECT_CANDIDATES_DIR;
  std::string platform_dir = "/etc/plugin_discovery/platform.d";
  std::string stamp = "/var/lib/plugin_discovery/platform-detect.done";
  std::string machine_file = "/sys/devices/soc0/machine";
  std::string os_release = "/etc/os-release";
  bool force = false, dry_run = false;

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto value = [&](std::string & out) {
        if (i + 1 >= argc) {
          std::cerr << "platform-detect: " << a << " needs a value\n";
          usage(std::cerr);
          std::exit(2);
        }
        out = argv[++i];
      };
    if (a == "-h" || a == "--help") {
      usage(std::cout);
      return 0;
    } else if (a == "--force") {
      force = true;
    } else if (a == "--dry-run") {
      dry_run = true;
    } else if (a == "--candidates") {
      value(candidates);
    } else if (a == "--platform-dir") {
      value(platform_dir);
    } else if (a == "--stamp") {
      value(stamp);
    } else if (a == "--machine-file") {
      value(machine_file);
    } else if (a == "--os-release") {
      value(os_release);
    } else {
      std::cerr << "platform-detect: unknown argument " << a << "\n";
      usage(std::cerr);
      return 2;
    }
  }

  std::error_code ec;
  if (!force && fs::exists(stamp, ec)) {
    std::cout << "platform-detect: already done (" << stamp << "); use --force to run again\n";
    return 0;
  }

  const std::string machine = read_machine(machine_file);
  const std::string os_type = read_os_type(os_release);
  std::vector<std::pair<std::string, fs::path>> matches;  // domain, candidate
  if (machine.empty()) {
    std::cerr << "[platform-detect] WARN: cannot read the machine from " << machine_file
              << ": no platform configuration is linked\n";
  } else if (os_type.empty()) {
    std::cerr << "[platform-detect] WARN: unknown OS type in " << os_release
              << " (supported: Ubuntu, Qualcomm Linux): no platform configuration is linked\n";
  } else {
    const std::string prefix = lower(machine) + "-" + os_type + "-";
    std::vector<fs::path> names;
    for (fs::directory_iterator it(candidates, ec), end; !ec && it != end; it.increment(ec)) {
      names.push_back(it->path());
    }
    std::sort(names.begin(), names.end());
    for (const auto & path : names) {
      const std::string name = lower(path.filename().string());
      const size_t tail = std::string(kSuffix).size();
      if (name.rfind(prefix, 0) != 0 || name.size() <= prefix.size() + tail ||
        name.compare(name.size() - tail, tail, kSuffix) != 0)
      {
        continue;
      }
      const std::string domain = name.substr(prefix.size(), name.size() - prefix.size() - tail);
      const bool twice = std::any_of(matches.begin(), matches.end(),
          [&](const auto & m) {return m.first == domain;});
      if (twice) {
        std::cerr << "[platform-detect] WARN: second candidate for domain '" << domain << "': "
                  << path << " ignored\n";
        continue;
      }
      matches.emplace_back(domain, fs::absolute(path));
    }
    if (matches.empty()) {
      std::cerr << "[platform-detect] WARN: no platform configuration for machine '" << machine
                << "' and OS '" << os_type << "' in " << candidates
                << ": the domains stay unconfigured and every compatible backend loads\n";
    }
  }

  int failed = 0;
  if (!dry_run && !matches.empty()) {
    fs::create_directories(platform_dir, ec);
    if (ec) {
      std::cerr << "[platform-detect] ERROR: cannot create " << platform_dir << ": "
                << ec.message() << "\n";
      return 1;
    }
  }
  if (force && !dry_run) {
    // Refresh: drop the links this tool made earlier (those into the candidates directory).
    for (fs::directory_iterator it(platform_dir, ec), end; !ec && it != end; it.increment(ec)) {
      std::error_code e2;
      if (fs::is_symlink(it->path(), e2) && points_into(it->path(), candidates)) {
        fs::remove(it->path(), e2);
      }
    }
  }
  for (const auto & m : matches) {
    const fs::path link = fs::path(platform_dir) / (m.first + kSuffix);
    if (fs::symlink_status(link, ec).type() != fs::file_type::not_found) {
      std::cerr << "[platform-detect] WARN: " << link << " exists, kept as it is\n";
      continue;
    }
    std::cout << "platform-detect: " << link.string() << " -> " << m.second.string() << "\n";
    if (dry_run) {
      continue;
    }
    fs::create_symlink(m.second, link, ec);
    if (ec) {
      std::cerr << "[platform-detect] ERROR: cannot create " << link << ": " << ec.message()
                << "\n";
      ++failed;
    }
  }
  if (failed != 0) {
    return 1;
  }

  if (!dry_run) {
    fs::create_directories(fs::path(stamp).parent_path(), ec);
    std::ofstream(stamp) << "machine=" << machine << "\nos=" << os_type << "\nlinks="
                         << matches.size() << "\n";
    if (!std::ifstream(stamp)) {
      std::cerr << "[platform-detect] WARN: cannot write the stamp file " << stamp
                << ": it will run again at the next boot\n";
    }
  }
  return 0;
}
