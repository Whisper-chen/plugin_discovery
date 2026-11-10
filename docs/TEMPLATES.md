# Configuration templates

**English** | [中文](TEMPLATES.zh-CN.md)

Two ready-to-copy templates for the people who own a platform or a customer deployment. They only say **which backends load**; whether a backend works on the machine is still answered by its own `probe()`. Format and combination rules are in [`LIBRARY.md`](LIBRARY.md) section 4.

| Template | For | Installed to | Directory |
|---|---|---|---|
| Platform configuration | the tech owner of a platform: lists the backends Qualcomm fully supports there | candidates in `<prefix>/share/plugin_discovery/platforms.d/`; `platform-detect` links the matching one into `/etc/plugin_discovery/platform.d/` | [`templates/platform-config/`](../templates/platform-config) |
| Customer configuration | a customer: adds its own backends, or takes a Qualcomm one out | `/etc/plugin_discovery/config.d/` | [`templates/customer-config/`](../templates/customer-config) |

## File names

The library reads two directories. Platform-tier files apply first and customer-tier files after, whatever their names; within a tier files apply in ascending file-name order, and the last one wins.

- Platform tier, `/etc/plugin_discovery/platform.d/`: `<domain>.config.yaml`, for example `camera.config.yaml`. These are symlinks created by `platform-detect`; nobody writes them by hand.
- Customer tier, `/etc/plugin_discovery/config.d/`: `NN-<domain>-<source>.config.yaml`. `NN` is two digits, `50` unless you need to order files; `<domain>` is the domain the file configures; `<source>` is the customer or purpose, for example `50-camera-acme.config.yaml`.
- Platform candidates, `<prefix>/share/plugin_discovery/platforms.d/`: `<machine>-<platform>-<domain>.config.yaml`, for example `QCS8275-Ubuntu-camera.config.yaml`. `<machine>` is the string in `/sys/devices/soc0/machine`; `<platform>` is the OS type, `Ubuntu` or `QCLinux`; comparison ignores case. There is no `NN`, because the combination is unique. The library never reads this directory.

Files with any other name are ignored (with a WARN), and a manifest (`*.plugin.yaml`) is never read as configuration.

## Platform configuration

One package per platform. It contains no code and does not need the plugin_discovery library to build. It installs one candidate file per machine, OS type and domain. On the first boot the `platform-detect` executable (installed with plugin_discovery, run once by the systemd unit `plugin-discovery-platform-detect.service`) reads the machine and the OS type and links each matching candidate into `/etc/plugin_discovery/platform.d/` as `<domain>.config.yaml`. If nothing matches, no link is made, the domain stays unconfigured and a WARN is logged. Application units should order themselves `After=` that service.

```
platform-config/
├── CMakeLists.txt                       installs config/*.config.yaml into share/plugin_discovery/platforms.d/
└── config/
    └── MACHINE-PLATFORM-camera.config.yaml   one file per machine, OS type and domain
```

```yaml
domains:
  camera:                              # the domain name declared by PLUGIN_DISCOVERY_INTERFACE
    backends: [<BACKEND_A>, <BACKEND_B>]   # added in this order
    # disable: [<BACKEND_C>]               # optional
```

To use it:

1. Copy `templates/platform-config/` to a new package directory and replace every `<MACHINE>`, `<PLATFORM>` and `<BACKEND_*>` in the file names, `CMakeLists.txt` and the YAML.
2. Fill in `backends` with the **`backend` names from the manifests** (`*.plugin.yaml`) of the backends this platform fully supports, not with library names.
3. Add one file per domain (video, audio, imu ...) for each machine and OS type; remove the commented lines you do not need.
4. Build and install it like any CMake package; package it as its own recipe or deb so that it can be installed per platform. The unified Ubuntu image does not contain it.

Yocto recipe sketch:

```
SUMMARY = "plugin_discovery platform configuration for <MACHINE>"
LICENSE = "BSD-3-Clause-Clear"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/BSD-3-Clause-Clear;md5=<md5 of the BSD-3-Clause-Clear file in your layer>"
inherit cmake
SRC_URI = "file://<platform-config-dir>/"
S = "${UNPACKDIR}/<platform-config-dir>"
RDEPENDS:${PN} = "plugin-discovery <the backend packages listed in the yaml>"
```

Notes:

- A backend named in the file but not installed is reported as skipped ("configured but not installed"); a backend installed but not named is not loaded (`not_listed`).
- Versions and ABI are not part of the file. They are recorded in each backend's `.so` and checked when it loads.
- If a file cannot be parsed it is skipped with a message on stderr and the other files still apply.
- `platform-detect --force` redoes the links (for example after the image moves to another machine or a package adds a candidate); `--dry-run` shows what it would link without changing anything; `--candidates`, `--platform-dir`, `--stamp`, `--machine-file` and `--os-release` point it at other paths, for testing. The stamp file is `/var/lib/plugin_discovery/platform-detect.done`.

## Customer configuration

```yaml
domains:
  camera:
    # reset: true                  # use only the backends below, none of the platform's
    backends: [<MY_BACKEND>]       # the `backend` name in your backend's manifest
    disable: [<PLATFORM_BACKEND>]  # optional
```

Copy it to `/etc/plugin_discovery/config.d/` as, for example, `50-camera-acme.config.yaml`. Files there are customer-origin by default, so no `origin` field is needed (a file in any other directory is Qualcomm-origin by default; an `origin:` key overrides this). Customer files apply after the platform ones, whatever the names, and the last file wins; what a file lists goes to the front of the result, and the first entry is the default backend.

| You want to | Write |
|---|---|
| add a backend | `backends: [my]` |
| remove a Qualcomm backend | `disable: [a]` |
| turn on a backend the platform disabled | `backends: [c]` |
| replace a Qualcomm backend by your own build of it | give your manifest the same `backend` name |
| use only your backends | `reset: true` and your `backends` |
| drop everything the platform lists for a domain | `reset: true`, then your own `backends` |

Every file takes part: a customer file cannot replace or hide a platform file, and overrides it only through `reset` and `disable`. `pluginlib-detect` shows each file as `applied` or `invalid`.

Example with a platform configuration that lists `[a, b]` and disables `c`:

| Customer file | Backends that load |
|---|---|
| none | a, b |
| `backends: [my]` | my, a, b |
| `backends: [my]`, `disable: [a]` | my, b |
| `backends: [c]` | c, a, b |
| `reset: true`, `backends: [my]` | my |
| manual `PLUGIN_DISCOVERY_BACKEND_CAMERA=a` | a (replaces every layer, the customer file included) |

Whether a listed backend actually serves the machine is decided by its `probe()`; an unsupported one is skipped and reported.

## Trying a template

```bash
PLUGIN_DISCOVERY_CONFIG_PATH=<platform dir>:<customer dir> <your app>
```

`PLUGIN_DISCOVERY_CONFIG_PATH` (`:`-separated) replaces the two default directories; later directories take precedence over earlier ones, so put the platform directory first. A directory outside `/etc/` needs `origin: customer` in the customer file.
