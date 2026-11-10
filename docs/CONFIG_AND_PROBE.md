# Why the configuration works this way, and how `probe()` works

This page explains the reasons behind the configuration rules and the `probe()` logic. The exact formats are in [`LIBRARY.md`](LIBRARY.md); the principles D1–D9 are in [`CROSS_PLATFORM_DESIGN.md`](CROSS_PLATFORM_DESIGN.md).

## 1. What the configuration has to do

- Several parties own configuration: QCOM ships one package per platform, the customer adds or overrides, a developer overrides for one run (D6, D8).
- None of them should edit another party's file. Packages install files; they never patch each other.
- The library never knows the SoC (D5). The configuration, not code, says which backends belong to a machine.
- The result is an **ordered list per domain**. The first entry is the default backend, so order is meaning, not decoration.

## 2. The rules and why

| Rule | Reason |
|---|---|
| One small file per package, named `NN-<domain>-<source>.config.yaml` (platform tier: the link `<domain>.config.yaml`, section 5) | A package can add or remove its file without touching others. The suffix mirrors `*.plugin.yaml`, so a manifest is never read as configuration |
| Two tiers: platform first, customer after | D8, customer first. A customer file always wins over QCOM's, whatever `NN` either uses |
| Inside a tier, ascending `NN`, last file wins | The same convention as systemd drop-ins; people already know it |
| A file's `backends` go to the **front** of the list | "Last wins" and "first entry is the default" must both hold. Appending would let the earliest file keep the default |
| `disable` removes at once; a later listing re-enables | The customer can remove a QCOM backend, and a later file can undo that, without editing anyone's file |
| `reset: true` | Start from nothing instead of inheriting the platform list (like an empty assignment in systemd) |
| `compat` per domain, a scalar | Whether trying fallbacks is acceptable depends on the domain, not the machine. A scalar has one clear owner: the last file |
| No same-name replacement and no `/dev/null` masking: a customer overrides only through `reset` and `disable` | One overriding mechanism instead of three; every file always takes part in the merge, so `pluginlib-detect` can show the file behind each entry |
| Merge at run time (`scan()`), one final list per domain | Loading and selection read one result; nothing else decides. `pluginlib-detect` shows that same result and the file behind each entry |
| A manual choice replaces everything | A developer can pin a backend for one run without touching files |
| Defaults: first entry, failure is an error | The user sees the misconfiguration instead of a silent fallback to a different stack. `compat: true` is the explicit opt-in to fallback |

## 3. Why not an `sshd_config` style

`sshd_config` and `ssh_config` take the **first** value found for each keyword, and that suits their job: one main file, specific `Host` / `Match` blocks written before the general ones. It does not fit here.

1. **Several independent owners.** Package files get dropped into a directory. With first-wins, a file added later can never override an earlier one, so a customer file would need a name that sorts before every QCOM file. Last-wins with tiers lets the customer simply be later.
2. **The prefix means opposite things.** With `Include` at the top of `sshd_config` (the Ubuntu default), `10-` beats `50-`. systemd and this library use `50-` over `10-`. Mixing the two conventions confuses administrators; we follow the one used for drop-ins.
3. **Lists with meaning.** ssh settings are mostly scalars or accumulating sets. Here the list order selects the default, and entries must be removable (`disable`) and re-orderable (move to front). ssh has no such operations.
4. **No conditions needed.** `Host` / `Match` choose settings by who is connecting. Here the machine's identity is the answer to `probe()` (section 4), not a pattern in the file.
5. **One way to override.** systemd also lets a same-named file replace another and a `/dev/null` link mask it. We left both out: `reset` and `disable` already say what the customer wants, and keeping one rule is easier to explain.

We deliberately differ from systemd in these ways: there is no same-name replacement and no `/dev/null` masking; the customer tier is applied after the platform tier regardless of `NN` (D8); `backends` are inserted at the front (order is meaning); and there is an explicit `disable`.

## 4. The `probe()` logic

The configuration says which backends *may* load. `probe()` is each backend's own answer to whether it *can* run here. The library never decides that (D5, D9).

### Contract

`probe()` is called once, right after the backend object is created. It asks the vendor SDK and returns:

| Result | Meaning | Library action |
|---|---|---|
| `Ok` + devices | Works here; the devices it can serve | keep the backend; record the devices |
| `Unsupported` | Not for this machine (the SDK does not run on this SoC) | unload, INFO |
| `Fault` | Should work but failed (device error, daemon down); also a thrown exception | unload, ERROR; the other backends still load |

A backend must not crash, and must leave nothing behind, when the answer is `Unsupported`. The default `probe()` returns `Ok` with one device `default`, for backends with nothing to enumerate.

### Flow in `load<T>()`

```
plan:      final list for the domain (manual choice > config > all compatible)
for each planned backend, in list order:
    create object      -- fails: load_failed / incompatible (version, ABI), skip
    probe()
        Unsupported -> skipped[unsupported], unload
        Fault       -> skipped[fault], unload
        Ok          -> drop devices whose non-empty node an earlier backend already reported
                       (skipped[duplicate_device]); none left -> unload
                       else keep: backend + its devices
result:    backends in list order, skipped with reasons, first = first entry of the list
```

### What probe does *not* do

- **It does not choose the default.** The default is the first entry of the list, not "the first whose probe is Ok". A successful probe says the SDK is present, not that the device will open.
- **It is not compatibility mode.** `compat` concerns selection at open time only (try in order, first that opens wins; switched off for now). Handling of `Fault` is the same either way.
- **It does not read the SoC from the library side.** If a backend's SDK is absent or refuses to run, it says `Unsupported`; the library only reports that.

### Choosing a backend in the application

1. A named backend: use it.
2. Otherwise `BackendSet::first`. If it is not loaded (Unsupported, Fault, not installed) or cannot open the device, the call fails.
3. The domain's `compat: true` is meant to try the loaded backends in list order (first that opens wins). It is parsed but switched off for now, so there is no fallback.

Not verified on real hardware: Unsupported and Fault from real vendor SDKs, and node de-duplication with QMMF (it reports no device node).

## 5. Choosing the platform configuration on first boot (`platform-detect`)

One image has to run on several machines (D1), but the library never knows the SoC (D5). So the choice is made **outside** the library, once, on the first boot, and the library only reads the result.

```
/usr/share/plugin_discovery/platforms.d/       # candidates, installed by the platform package; never read by the library
├── QCS8275-Ubuntu-camera.config.yaml
├── QCS8275-QCLinux-camera.config.yaml
└── <machine>-<platform>-<domain>.config.yaml

/etc/plugin_discovery/platform.d/              # platform tier: the library reads this directory (provisional name)
└── camera.config.yaml  ->  /usr/share/plugin_discovery/platforms.d/QCS8275-Ubuntu-camera.config.yaml

/etc/plugin_discovery/config.d/                # customer tier, unchanged
└── 50-camera-acme.config.yaml
```

- **Candidates.** One file per machine, OS and domain, named `<machine>-<platform>-<domain>.config.yaml`. There is no `NN`: the combination is unique, so there is nothing to order. `<machine>` is the string in `/sys/devices/soc0/machine` (for example `QCS8275`); `<platform>` is the OS type. Comparison ignores case. The directory name `platforms.d/` is provisional.
- **Where the links live.** In their own directory under `/etc`, which stays writable when `/usr` is read-only, and which survives reboots. It must not be the customer directory: the library would treat the links as customer files, and `camera.config.yaml` sorts after `50-…` names, so the platform would win over the customer (against D8). The library's platform tier is therefore this directory, and a file's default `origin` follows its tier, not its path.
- **The service.** `platform-detect` ships with plugin_discovery as a separate executable (the library itself still knows nothing about machines). A systemd oneshot runs it **once, on the first boot** (guarded by a stamp file, so a second boot does nothing). It reads the machine and the OS type and links each matching candidate as `platform.d/<domain>.config.yaml`. Application units order themselves `After=` this service.
- **Why once.** The links persist in `/etc`, a person can change a link without the next boot undoing it, and nothing runs on a normal boot. The cost: nothing re-runs by itself when the image is moved to another machine or a package adds a candidate for a new domain. `platform-detect` can be run by hand to redo the links, and a package that adds candidates should run it in its post-install step (to be settled in detailed design).
- **What the library sees.** Ordinary `*.config.yaml` files in the two tier directories. It does not parse the candidate names. Nothing in the merge rules changes: platform tier first, customer tier after, `reset` is the only way to drop what the platform says.
- **No match.** No link is created, the platform tier has no file for that domain, and the library behaves as for an unconfigured domain (every compatible backend loads, with a WARN). `platform-detect` logs which machine and OS it could not match.
- **OS types.** Ubuntu and QCLinux are defined now; DSUbuntu and DSLE are reserved for later. The type comes from `/etc/os-release`, by rules checked in order:

| OS type | Rule |
|---|---|
| QCLinux | `NAME` starts with "Qualcomm Linux" (case-insensitive); the sample seen is `ID=qcom-robotics-ros2-jazzy`, `NAME="Qualcomm Linux Robotics Reference Distro with ROS"` |
| Ubuntu | `ID=ubuntu` |
| anything else | unknown: no link, WARN |

Open (see `OPEN_QUESTIONS.md` in plugin-discovery-design-note): how to tell DSUbuntu from Ubuntu and DSLE from QCLinux, naming future OS types, telling boards of the same machine apart (not possible at run time today), the final directory names, and how to refresh links after the image moves or a package adds candidates.
