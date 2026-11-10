# plugin_discovery library reference

**English** | [中文](LIBRARY.zh-CN.md)

The library's API, manifest and configuration formats, CMake functions and tests. For a project overview see the root [`README.md`](../README.md).

A plain CMake project that **does not depend on ROS** (no colcon / ament / pluginlib / class_loader / rcpputils / console_bridge), in two layers:

```
App / customer middleware / ROS node     ← depends only on the domain interface + plugin_discovery
  └── plugin_discovery    management layer: decides from configuration which backends to load, checks version and ABI, collects the devices each backend reports
        └── plugin_loader loading layer: dlopen, registration macro, factory table, ClassLoader, reference counting (aligned with class_loader)
              └── backend .so     PLUGIN_DISCOVERY_REGISTER_BACKEND(Derived, Interface)
```

Design rationale: [`CROSS_PLATFORM_DESIGN.md`](CROSS_PLATFORM_DESIGN.md); implementation notes: [`DESIGN.md`](DESIGN.md).

```
<repository root>/
├── include/plugin_loader/        loading-layer public headers (plugin_loader.hpp includes them all)
├── include/plugin_discovery/     management-layer public headers: backend.hpp (Backend base and macros), plugin_registry.hpp
├── src/                          impl.cpp class_loader.cpp multi_library_class_loader.cpp plugin_registry.cpp
├── cmake/                        plugin_discovery_add_plugin() and other CMake functions
└── test/                         scenario tests + differential tests against upstream class_loader + registry_test
```

**plugin_discovery knows nothing about SoCs.** Whether a backend works on this machine is answered by its own `probe()` (usually by asking its vendor SDK); which backends load is decided by configuration.

## 1. Build and run (nothing to source)

Requires a C++17 compiler, CMake ≥ 3.16 and `yaml-cpp` (used only by the management layer to parse manifests and configuration; not in the public headers).

```bash
# from the repository root
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=$PWD/install -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cmake --install build
```

From another project:

```cmake
find_package(plugin_discovery REQUIRED)     # -DCMAKE_PREFIX_PATH=<install directory>
target_link_libraries(my_app PRIVATE plugin_discovery::plugin_discovery)
```

## 2. Define a domain interface, write a backend

**Domain interface** (one per domain, a header): derives from `plugin_discovery::Backend` and declares its domain and version.

```cpp
#include "plugin_discovery/backend.hpp"

namespace hello {
class Greeter : public plugin_discovery::Backend {
public:
  virtual std::string greet() = 0;
};
}
PLUGIN_DISCOVERY_INTERFACE(hello::Greeter, "hello", 1, 0)   // global scope; bump minor only when appending virtual functions at the end, otherwise bump major
```

**Backend**: implements the interface and answers in `probe()` whether this machine is usable and which devices it can serve.

```cpp
plugin_discovery::ProbeResult probe() override {
  if (!my_vendor_sdk_runs_here()) {
    return plugin_discovery::ProbeResult::unsupported("vendor SDK does not run on this SoC");
  }
  return plugin_discovery::ProbeResult::ok({{"0", "/dev/video0", "camera 0"}});   // id, device node (may be empty), description
}
PLUGIN_DISCOVERY_REGISTER_BACKEND(hello::IQ8Greeter, hello::Greeter)   // namespace scope of a .cpp
```

| `probe()` result | Meaning | What plugin_discovery does |
|---|---|---|
| `ok(devices)` | usable on this machine | keeps it, hands the devices to the caller |
| `unsupported(why)` | not this machine's software stack | unloads it, logs INFO |
| `fault(why)` | should work but failed | unloads it, logs ERROR; the other backends still load |

Without an override, `probe()` returns `ok` with one device whose id is `default` (suits domains with nothing to enumerate, such as imu).
When unsupported, a backend must fail cleanly: no crash, no devices or threads left open.

The registration macro records, inside the backend library, the interface version and ABI tag (compiler major version + `_GLIBCXX_USE_CXX11_ABI`) it was built with. On load these are compared with the application's; on a mismatch the backend is rejected with the reason, for example:

```
skipped sensor/Old [incompatible]: built against test::Sensor 1.0, the application needs 1.1 or newer
skipped sensor/AbiX [incompatible]: built with gcc12-cxx11abi1, the application with gcc13-cxx11abi1; rebuild the backend with the SDK toolchain
```

Rule (provisional): same major, backend minor ≥ application minor, same ABI tag.

## 3. Generating manifests

Manifests (`*.plugin.yaml`) are **not written by hand**; CMake generates them and installs them with the backend:

```cmake
add_library(hello_backend_iq8 SHARED src/iq8_greeter.cpp)
target_link_libraries(hello_backend_iq8 PRIVATE hello_interface)
plugin_discovery_add_plugin(
  TARGET hello_backend_iq8  DOMAIN hello
  NAME hello/IQ8Greeter  CLASS hello::IQ8Greeter  BASE hello::Greeter
  BACKEND iq8            # the name configuration uses for this backend
  [ORIGIN qcom]          # qcom for Qualcomm backends; omitted = customer
  [PRIORITY 0]  [CAPABILITIES ...])
```

The function also links the target against `plugin_discovery::plugin_discovery` (where the registration records live). It installs:

- `lib/<DOMAIN>/lib<TARGET>.so`
- `share/<DOMAIN>/plugins/<TARGET>.plugin.yaml`: `library` is relative to the manifest, so the install tree can be moved as a whole

There is no `PLATFORMS` argument (the library knows nothing about SoCs); passing one is an error. To export the manifests only: `cmake --install build --component manifests --prefix /some/dir`.

### Manifest format

```yaml
plugins:
  - name: hello/IQ8Greeter            # required, unique id
    class: hello::IQ8Greeter          # required, the Derived in the registration macro
    base: hello::Greeter              # required, the domain interface (same as in PLUGIN_DISCOVERY_INTERFACE)
    library: ../../../lib/hello/libhello_backend_iq8.so   # required
    domain: hello                     # required
    backend: iq8                      # optional: the name used in configuration
    origin: qcom                      # optional: qcom | customer, default customer
    priority: 0                       # optional, default 0
    capabilities: []                  # optional
```

Entries with a `platforms` field are skipped with an error. Duplicate names: customer replaces qcom (INFO); any other duplicate keeps the first one found (WARN).

## 4. Configuration: which backends load

On demand: only the backends listed in the configuration load.

### File names

Only files named `*.config.yaml` are read; other files in the directory are ignored (a stray `*.yaml` gets a WARN). The suffix mirrors `*.plugin.yaml`, so a manifest is never read as configuration. In the platform tier the file is `<domain>.config.yaml` (a link made by `platform-detect`, no `NN`); in the customer tier it is `NN-<domain>-<source>.config.yaml`, where `NN` is two digits that order the files in that tier (default `50`), for example `50-camera-acme.config.yaml`.

```yaml
origin: customer        # optional; if omitted, the tier decides: the customer directory is customer, any other qcom
domains:
  camera:
    reset: true                 # optional: forget what earlier files set for this domain
    backends: [qcarcam, camx]   # backend names to put at the front, in this order
    disable: [v4l2]             # optional: remove these now
    compat: true                # optional: compatibility mode (default false)
```

| Source | Directory | Notes |
|---|---|---|
| Platform configuration (Qualcomm) | `/etc/plugin_discovery/platform.d/` | `<domain>.config.yaml` links, made on the first boot by `platform-detect` to the candidate for this machine and OS (see below) |
| Customer configuration | `/etc/plugin_discovery/config.d/` | applied after the platform tier, so it wins |
| Manual | `load({"a","b"})` or `PLUGIN_DISCOVERY_BACKEND_<DOMAIN>=a,b` | replaces both of the above |

`PLUGIN_DISCOVERY_CONFIG_PATH` (`:`-separated) replaces the two directories, for tests and trials; every directory given counts as the platform tier unless it is `/etc/plugin_discovery/config.d` or the file says `origin: customer`.

### Platform configuration candidates and `platform-detect`

A platform package installs one **candidate** per machine, OS type and domain into `<prefix>/share/plugin_discovery/platforms.d/` (with `plugin_discovery_install_config()`), named `<machine>-<platform>-<domain>.config.yaml`, for example `QCS8275-Ubuntu-camera.config.yaml`. The library never reads that directory. `<machine>` is the string in `/sys/devices/soc0/machine`, `<platform>` the OS type (`Ubuntu`: `ID=ubuntu`; `QCLinux`: `NAME` starts with "Qualcomm Linux"); names are compared ignoring case.

`platform-detect` (an executable of this package) reads the machine and the OS type and links each matching candidate as `/etc/plugin_discovery/platform.d/<domain>.config.yaml`. The systemd oneshot `plugin-discovery-platform-detect.service` runs it **only on the first boot** (stamp file `/var/lib/plugin_discovery/platform-detect.done`), so links changed by hand stay; application units order themselves `After=` it. With no match or an unknown OS it links nothing and warns: the domain stays unconfigured. Options: `--force` (run again and refresh the links it made), `--dry-run`, `--candidates`, `--platform-dir`, `--stamp`, `--machine-file`, `--os-release`. See [`CONFIG_AND_PROBE.md`](CONFIG_AND_PROBE.md) for the reasoning.

### How files combine

- **Order.** All files are merged at `scan()` into one final list per domain, the last file winning (as with systemd drop-ins): all platform-tier files first, then all customer-tier files (whatever the names), each group in ascending file-name order (`NN` in the customer tier). In one domain block the keys act as `reset`, then `backends`, then `disable`.
- **`backends`** puts the names at the front of the list, in the file's own order; a name already listed moves to the front. **`disable`** removes them from the list so far; a later file that lists the backend again enables it again. **`reset: true`** empties the list the earlier files built for this domain, then the block's own `backends` follow.
- **Result.** The first entry of the final list is the default backend. Example: a `50` file with `[qmmf, qcarcam]` and a `60` file with `[v4l2]` give `v4l2, qmmf, qcarcam`.
- **`compat`** (per domain, default `false`) is a scalar: the last file that sets it wins and `reset` restores `false`. See *Selecting a backend* below.
- **Every file takes part.** There is no replacing a file by one of the same name and no masking with an empty file or a link to `/dev/null`; a customer overrides only with `reset` and `disable`.

Example, with a platform configuration listing `[a, b]` and `disable: [c]`:

| Customer file | Backends that load |
|---|---|
| none | a, b |
| `backends: [my]` | my, a, b |
| `backends: [my]`, `disable: [a]` | my, b |
| `backends: [c]` (undoes the platform's disable) | c, a, b |
| `reset: true`, `backends: [my]` | my |

All of the following are provisional (they may change in detailed design; the list is in section 2 of `OPEN_QUESTIONS.md` in plugin-discovery-design-note):
- When a domain has **no configuration at all**, every compatible backend of the domain loads, with a WARN that no platform configuration matched this machine.
- An installed backend that is not in the configuration does not load; it is reported as `not_listed`.
- When a customer backend and a Qualcomm backend share a `backend` name, the customer one replaces the Qualcomm one.
- When two backends report the same non-empty device node, the earlier one keeps it; the later one no longer serves that device and is unloaded if that leaves it with none.

### Selecting a backend

`load()` loads and probes every listed backend (an `Unsupported` or `Fault` result unloads it, as before). A caller that names no backend then uses the **first entry of the final list** (`BackendSet::first`); probe results are not used to choose. If that backend is not loaded (not installed, unsupported, fault) or cannot open the device, the call fails; a name that is configured but not installed counts as the first entry too and is reported as `missing`. The domain's `compat: true` is parsed and exposed (`BackendSet::compat`, `pluginlib-detect`), but selection ignores it for now: there is no fallback to the next entry. A manual choice (`load({...})`, `PLUGIN_DISCOVERY_BACKEND_<DOMAIN>`) bypasses the configuration and `compat`.

### Seeing the merged result: `pluginlib-detect`

```
$ pluginlib-detect                 # every domain; -d camera for one
Configuration files:
  applied  /etc/plugin_discovery/platform.d/camera.config.yaml  (platform)
  applied  /etc/plugin_discovery/config.d/50-camera-acme.config.yaml  (customer)

Domain camera
  load order (first = default):
    1. mycam  [customer] /etc/plugin_discovery/config.d/50-camera-acme.config.yaml
    2. qcarcam  [platform] /etc/plugin_discovery/platform.d/camera.config.yaml
  compat: off
  disabled: qmmf  by /etc/plugin_discovery/config.d/50-camera-acme.config.yaml
```

It lists the directories, every file found with its state (`applied` or `invalid`), and for each domain the load order and the file behind every entry, like `systemctl cat`. It only reads manifests and configuration: no library is loaded and nothing is probed. It honours `PLUGIN_DISCOVERY_PATH` / `PLUGIN_DISCOVERY_CONFIG_PATH` and takes `--path` / `--config-path`; if `PLUGIN_DISCOVERY_BACKEND_<DOMAIN>` is set it says so. The same data is available to programs as `PluginRegistry::config_files()` and `config_view()`.

### Usage

```cpp
#include "plugin_discovery/plugin_registry.hpp"

plugin_discovery::PluginRegistry registry;   // manifests: $PLUGIN_DISCOVERY_PATH; configuration: see the table above
registry.scan();                             // reads manifests and configuration only, loads no .so

auto set = registry.load<hello::Greeter>();  // load per configuration, probe, collect
for (auto & b : set.backends) {              // configuration order; set.first is the default
  b.handle->greet();                         // b.devices: the devices this backend can serve
}
for (auto & s : set.skipped) { /* s.name, to_string(s.kind), s.reason */ }

registry.load<hello::Greeter>({"iq9"});      // manual choice
registry.create<hello::Greeter>("hello/IQ8Greeter");   // create one by name (version / ABI are checked too)
registry.plan("hello::Greeter", "hello");    // only show what would load, load nothing
```

`load()` does not throw when an individual backend fails; everything ends up in `skipped` and is summarised once on stderr. `PluginHandle` holds the object and its library; `reset()` destroys the object before releasing the library.

Environment variables:

| Variable | Purpose |
|---|---|
| `PLUGIN_DISCOVERY_PATH` | manifest search directories, `:`-separated |
| `PLUGIN_DISCOVERY_CONFIG_PATH` | configuration directories, `:`-separated, replace the defaults |
| `PLUGIN_DISCOVERY_BACKEND_<DOMAIN>` | manually choose which backends of the domain load, `,`-separated |
| `PLUGIN_LOADER_LOG_LEVEL` | loading-layer log level |

### Loading layer

```cpp
#include "plugin_loader/plugin_loader.hpp"
plugin_loader::ClassLoader loader("/path/libfoo.so");              // loads immediately; throws LibraryLoadException on failure
auto p = loader.createUniqueInstance<Base>("Derived");             // also createInstance / createUnmanagedInstance
loader.getAvailableClasses<Base>();  loader.isClassAvailable<Base>("Derived");

plugin_loader::MultiLibraryClassLoader multi(false);
multi.loadLibrary(path);  multi.createInstance<Base>("Derived");   // several libraries
```

When using the loading layer alone, the registration macro is `PLUGIN_REGISTER_CLASS(Derived, Base)` (no version check). Plugins and host **must link the same** `libplugin_loader.so`.

## 5. Mapping to class_loader

Class names, method names and the exception hierarchy match `class_loader`; the namespace is `plugin_loader` and the registration macro is `PLUGIN_REGISTER_CLASS` (plus `_WITH_MESSAGE`).

| class_loader | plugin_loader | Status |
|---|---|---|
| `ClassLoader` (on-demand, counts, managed / unmanaged instances) | same name | aligned |
| `MultiLibraryClassLoader` | same name (its internal helpers are private, as upstream) | aligned |
| `ClassLoaderException` / `LibraryLoadException` / `LibraryUnloadException` / `CreateClassException` / `NoClassLoaderExistsException` | same names | aligned |
| `systemLibraryFormat` | same name (plus `systemLibrarySuffix`) | aligned |
| `CLASS_LOADER_REGISTER_CLASS(_WITH_MESSAGE)` | `PLUGIN_REGISTER_CLASS(_WITH_MESSAGE)` | aligned |
| Name collision: the later registration wins, with a warning | same | aligned |
| A library opened outside any ClassLoader (linked directly) → its classes have no owner and no library is unloaded afterwards | same | aligned |
| Logging through console_bridge | stderr, `PLUGIN_LOADER_LOG_LEVEL=debug\|info\|warn\|error\|none` | replaced |

### **Deliberate** differences from upstream (pinned by the differential tests in `test/expected/`)

1. **`isLibraryLoaded()` means "this loader holds the library".** Upstream returns true for any open library, contradicting its own documentation.
   Consequences: a loader that gave a library up reloads it automatically when it creates an object again; an on-demand loader on a shared library loads it on first creation
   (upstream throws `CreateClassException` there).
2. **Calls after a failed `MultiLibraryClassLoader::loadLibrary` do not crash** (upstream segfaults reliably after a specific call sequence).

### Incompatibilities

- **Not binary compatible**: the factory table lives in `libplugin_loader.so`. Existing `.so` files built with `PLUGINLIB_EXPORT_CLASS` / `CLASS_LOADER_REGISTER_CLASS`
  do not register here; switch the macro to `PLUGIN_REGISTER_CLASS` and rebuild.
- Cannot share one factory table with upstream class_loader in the same process.
- Linux only (`dlfcn`).
- As upstream: a `ClassLoader` must outlive the objects it created (the management layer's `PluginHandle` guarantees that);
  a relative and an absolute path are two different library keys.

## 6. Tests

`test/scenarios.cpp` is one source that compiles against `plugin_loader` or, with `-DUSE_UPSTREAM`, against the real class_loader
(the same API is itself evidence of compatibility). Each scenario runs in its own process, prints a transcript, and the transcripts are compared line by line.

```bash
# plugin_loader only (no ROS needed)
cmake -S . -B build-test -DPLUGIN_LOADER_BUILD_TESTS=ON
cmake --build build-test -j
test/run_diff.sh build-test
build-test/test/registry_test build-test/test/backends   # PluginRegistry: configuration, probe, version / ABI, de-duplication
test/platform_detect_test.sh build-test                   # platform-detect
# also diff against upstream class_loader (only this needs a ROS environment)
source /opt/ros/jazzy/setup.bash
cmake -S . -B build-test -DPLUGIN_LOADER_COMPARE_UPSTREAM=ON
cmake --build build-test -j && test/run_diff.sh build-test
```

Three builds are compared:

- **upstream** — ROS class_loader.
- **ours** — `plugin_loader`, test libraries linked with `-z nodelete`: upstream's test libraries contain `STB_GNU_UNIQUE` symbols and
  are never really unmapped by `dlclose`, so `-z nodelete` puts both in the same situation (and covers the "library still in memory" revival path).
- **deletable** — `plugin_loader`, ordinary libraries; with `mapped=` masked it must match ours, and it is asserted to really unload.

27 scenarios; without upstream, 34 checks (ours vs deletable per scenario, plus the "really unloaded" assertions), 4 of which compare against `test/expected/` because of the deliberate differences in section 5.
Covered: the three ways to create, scope filtering, several loaders sharing a library, load / unload counts, unloading with live objects, unloading in the destructor,
on-demand lazy load and auto unload, unmanaged instances suppressing unload, exception types, name collisions, reload after unload, threads,
every public method of `MultiLibraryClassLoader`, a directly linked (non-pure) library.
