# Robotics SDK cross-platform: plugin_discovery high-level design (HLD)

**English** | [中文](CROSS_PLATFORM_DESIGN.zh-CN.md)

Status: high-level design, agreed on 2026-10-28. The code implements it (`include/`, `src/` at the repository root); it has **not been verified on real hardware**.

- This document covers the top level only: where plugin_discovery sits in the system, its responsibilities, and how it relates to backends, vendor SDKs and configuration. Process details, fields and formats are left to detailed design (section 7).
- Where each conclusion came from, the questions left to detailed design, the per-domain / per-backend work items and the architecture diagram are in a separate repository, plugin-discovery-design-note.
- Implementation notes for the current code: [`DESIGN.md`](DESIGN.md); API reference: [`LIBRARY.md`](LIBRARY.md).

## 1. The problem

The Robotics SDK must offer **the same interfaces** on several chipsets (IQ8 / IQ9 / IQ10 …) and two OSes (Ubuntu, QLI). Hardware access, however, goes through each vendor's SDK (QCarCam, CamX, RealSense, Orbbec …), and these differ.

| Constraint | What it means |
|---|---|
| One Ubuntu image for every platform | one image and one apt repository serve IQ8 / IQ9 / IQ10 alike, so which vendor SDK to use cannot be fixed at build time |
| One QLI image per machine | the mechanism must be the same, just with fewer candidates |
| Some customers do not use ROS | they use their own middleware; ROS is only one optional way in |
| Customers extend it | customers can write their own backends or modify QCOM's open-source ones, and the customer's take precedence |
| Several devices on one machine | e.g. a MIPI camera and a USB depth camera at the same time, served by different vendor SDKs |

Domains: camera / video / audio / imu / lidar. Each has its own interface and backends, independent of the others; plugin_discovery is the only part they share.

## 2. Base model: backend / vendor SDK / device / chipset

| Relation | Fact |
|---|---|
| backend → vendor SDK | today a backend wraps exactly one vendor SDK; leave room for wrapping several later |
| chipset → vendor SDK | a chipset always runs several SDKs |
| identity of a vendor SDK | name + version; a different version gets a different name and counts as another SDK |
| backend → device | device nodes are visible to every backend, but only a backend **adapted to that device** can use it |
| device identity | every SDK reports device node paths, but they are not consistent across SDKs |

Three consequences:
1. Several backends working on one machine at the same time is the normal case.
2. Devices are discovered by backends through their SDKs; plugin_discovery does not discover devices itself.
3. Chipsets do not appear in plugin_discovery.

## 3. Layers and responsibilities

```
 Consumers: customer applications · customer middleware · ROS nodes (e.g. the single qrb_ros_camera)
        │  call only the domain interface, use devices
        ▼
 Domain interface (one public, versioned C++ interface per domain)
        ▲  implemented by every backend
        │
 plugin_discovery ──reads──▶ configuration (platform configuration · customer config file · manual)
        │  loads backends per configuration, collects the devices they report
        ▼
 backend A        backend B        customer backend    … (side by side in one process)
        │                │                │
 vendor SDK A     vendor SDK B     customer / third-party SDK
        │                │                │
 hardware / kernel
```

| Layer | Responsible for | Not responsible for |
|---|---|---|
| Consumer | calling only the domain interface, using devices | knowing which backends exist, knowing the SoC |
| Domain interface | one public C++ interface per domain, implemented by every backend | — |
| plugin_discovery | reading configuration, deciding which backends load, loading them, collecting the devices they report for consumers | knowing the SoC, discovering devices itself, backend internals |
| Backend | wrapping one vendor SDK, reporting through it the devices it can use, handling SoC differences | — |
| Vendor SDK | discovering and driving hardware | — |
| Configuration | deciding which backends load on a machine | — |

## 4. Runtime

plugin_discovery reads the configuration and loads only the backends it lists (**on demand**). Each loaded backend reports, through its own vendor SDK, the devices it can use; plugin_discovery collects those devices for consumers, which then use them per device. Several backends work side by side in one process.

The configuration has three sources:

| Source | Provided by | Purpose |
|---|---|---|
| Platform configuration | QCOM, one per machine and OS | lists the fully supported backends of that platform; a service outside plugin_discovery picks the one that matches this machine on the first boot, so it works out of the box |
| Customer config file | customer | adds the customer's own backends; takes precedence over the platform configuration |
| Manual configuration | integrator / user | overrides both of the above |

## 5. Deployment

| Case | How |
|---|---|
| Ubuntu | one unified image runs on IQ8 / IQ9 / IQ10. The image ships the platform configurations of all machines; on the first boot a `platform-detect` service reads the machine and the OS type and activates the matching one. plugin_discovery only reads what was activated and never learns the SoC |
| QLI | same mechanism; the image may ship only the configurations it needs |
| Customer | installs its own backends and adds a configuration file |

Backends are packaged per vendor SDK (software stack), not per chipset. For example, one CamX package serves iq8 and iq9.

## 6. Design principles

| # | Principle |
|---|---|
| D1 | **Runtime selection is needed**: one Ubuntu image covers several platforms; QLI just has fewer candidates and uses the same mechanism |
| D2 | **Own implementation, no ROS dependency**: neither plugin_discovery nor backends depend on ROS; a ROS node is just one consumer |
| D3 | **Open to customers**: domain interfaces, the plugin mechanism and the configuration format are public API; customers can write backends or modify QCOM's open-source backends |
| D4 | **Compatibility**: QCOM's validated toolchain by default, so interfaces may use `std::` types; version and ABI are checked on load; QCOM owns the versioning rules, judged by compatibility for implementers |
| D5 | **Software stacks, not SoCs**: SoC differences are handled inside backends; plugin_discovery has no notion of a SoC |
| D6 | **On-demand loading**: configuration decides which backends load (platform configuration / customer configuration / manual) |
| D7 | **Several backends side by side**: one domain interface can have several backends loaded at once, each serving different devices, in one process |
| D8 | **Customers first**: customer backends and configuration take precedence over QCOM's |
| D9 | **Contract for backends**: implement the domain interface; report through its own SDK the devices it can use; when unsupported, say so clearly without crashing or leaving side effects; coexist with other backends in one process |

## 7. Out of scope (left to detailed design)

| Topic | Notes |
|---|---|
| Selection and configuration details | configuration format and location, how several configurations merge, whether customers can replace QCOM backends, the fallback without configuration, two backends adapted to the same device, telling "unsupported" from "faulty". List: section 2 of `OPEN_QUESTIONS.md` in plugin-discovery-design-note |
| Manifest fields and search paths | which fields stay, directory conventions |
| Data plane | frame model, zero copy, fd passing |
| Lifetime and threading | destruction order of objects and libraries, callback thread rules |
| Daemon-style SDKs and out-of-process backends | connection ownership, disconnects, whether process isolation is needed |
| Error handling and diagnostic tools | |
