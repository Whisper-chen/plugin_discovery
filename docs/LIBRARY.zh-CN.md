# plugin_discovery 库参考

[English](LIBRARY.md) | **中文**

库的 API、清单与配置格式、CMake 函数和测试。项目概览见仓库根目录的 [`README.zh-CN.md`](../README.zh-CN.md)。

标准 CMake 项目，**不依赖 ROS**(不用 colcon / ament / pluginlib / class_loader / rcpputils / console_bridge),分两层：

```
App / 客户 middleware / ROS 节点     ← 只依赖域接口 + plugin_discovery
  └── plugin_discovery    管理层：按配置决定加载哪些 backend,校验版本与 ABI,收集各 backend 报出的设备
        └── plugin_loader 加载层:dlopen、注册宏、工厂表、ClassLoader、引用计数(与 class_loader 对齐)
              └── backend .so     PLUGIN_DISCOVERY_REGISTER_BACKEND(Derived, Interface)
```

设计依据见 [`CROSS_PLATFORM_DESIGN.zh-CN.md`](CROSS_PLATFORM_DESIGN.zh-CN.md);实现说明见 [`DESIGN.zh-CN.md`](DESIGN.zh-CN.md)。

```
<仓库根目录>/
├── include/plugin_loader/        加载层公开头文件(plugin_loader.hpp 汇总)
├── include/plugin_discovery/     管理层公开头文件:backend.hpp(Backend 基类与宏)、plugin_registry.hpp
├── src/                          impl.cpp class_loader.cpp multi_library_class_loader.cpp plugin_registry.cpp
├── cmake/                        plugin_discovery_add_plugin() 等 CMake 函数
└── test/                         场景测试 + 与上游 class_loader 的差分测试 + registry_test
```

**plugin_discovery 不认 SoC。** 一个 backend 能不能在这台机器上用，由它自己的 `probe()` 回答(通常就是问它的 vendor SDK);加载哪些 backend,由配置决定。

## 1. 构建与运行(不需要 source 任何东西)

依赖:C++17 编译器、CMake ≥ 3.16、`yaml-cpp`(仅管理层解析清单和配置用，不在公开头文件里)。

```bash
# 在仓库根目录
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=$PWD/install -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cmake --install build
```


外部项目使用：

```cmake
find_package(plugin_discovery REQUIRED)     # -DCMAKE_PREFIX_PATH=<install 目录>
target_link_libraries(my_app PRIVATE plugin_discovery::plugin_discovery)
```

## 2. 定义域接口、写 backend

**域接口**(各域一份，头文件):派生自 `plugin_discovery::Backend`,并声明域名和版本。

```cpp
#include "plugin_discovery/backend.hpp"

namespace hello {
class Greeter : public plugin_discovery::Backend {
public:
  virtual std::string greet() = 0;
};
}
PLUGIN_DISCOVERY_INTERFACE(hello::Greeter, "hello", 1, 0)   // 全局作用域;只在末尾追加虚函数时升 minor,否则升 major
```

**backend**:实现接口，并在 `probe()` 里回答"这台机器能不能用、能服务哪些设备"。

```cpp
plugin_discovery::ProbeResult probe() override {
  if (!my_vendor_sdk_runs_here()) {
    return plugin_discovery::ProbeResult::unsupported("vendor SDK does not run on this SoC");
  }
  return plugin_discovery::ProbeResult::ok({{"0", "/dev/video0", "camera 0"}});   // id、设备节点(可空)、描述
}
PLUGIN_DISCOVERY_REGISTER_BACKEND(hello::IQ8Greeter, hello::Greeter)   // .cpp 的命名空间作用域
```

| `probe()` 结果 | 含义 | plugin_discovery 的处理 |
|---|---|---|
| `ok(devices)` | 这台机器能用 | 保留，设备交给调用方 |
| `unsupported(why)` | 不是这台机器的软件栈 | 卸载，记 INFO |
| `fault(why)` | 本该能用但出了错 | 卸载，记 ERROR;其他 backend 照常加载 |

不重写 `probe()` 时默认 `ok`,报一个 id 为 `default` 的设备(适合没有设备可枚举的域，例如 imu)。
不支持时必须干净地失败：不崩溃、不留下打开的设备或线程。

注册宏在 backend 库里记下它编译时的接口版本和 ABI tag(编译器主版本 + `_GLIBCXX_USE_CXX11_ABI`)。加载时与 App 比对，不一致就拒绝加载并说明原因，例如：

```
skipped sensor/Old [incompatible]: built against test::Sensor 1.0, the application needs 1.1 or newer
skipped sensor/AbiX [incompatible]: built with gcc12-cxx11abi1, the application with gcc13-cxx11abi1; rebuild the backend with the SDK toolchain
```

规则(实现暂定):major 相等，且 backend 的 minor ≥ App 的 minor;ABI tag 相等。

## 3. 生成清单

清单(`*.plugin.yaml`)**不用手写**,由 CMake 生成，随 backend 一起安装：

```cmake
add_library(hello_backend_iq8 SHARED src/iq8_greeter.cpp)
target_link_libraries(hello_backend_iq8 PRIVATE hello_interface)
plugin_discovery_add_plugin(
  TARGET hello_backend_iq8  DOMAIN hello
  NAME hello/IQ8Greeter  CLASS hello::IQ8Greeter  BASE hello::Greeter
  BACKEND iq8            # 配置里用来指代这个 backend 的名字
  [ORIGIN qcom]          # Qualcomm 的 backend 写 qcom;不写 = customer
  [PRIORITY 0]  [CAPABILITIES ...])
```

这个函数还会给 target 链接 `plugin_discovery::plugin_discovery`(注册记录在那里)。安装结果：

- `lib/<DOMAIN>/lib<TARGET>.so`
- `share/<DOMAIN>/plugins/<TARGET>.plugin.yaml`:`library` 是相对清单的路径，安装目录可整体搬走

不支持 `PLATFORMS` 参数(库不认 SoC),传入会直接报错。只想导出清单：`cmake --install build --component manifests --prefix /some/dir`。

### 清单格式

```yaml
plugins:
  - name: hello/IQ8Greeter            # 必填，唯一 id
    class: hello::IQ8Greeter          # 必填，注册宏里的 Derived
    base: hello::Greeter              # 必填，域接口(与 PLUGIN_DISCOVERY_INTERFACE 一致)
    library: ../../../lib/hello/libhello_backend_iq8.so   # 必填
    domain: hello                     # 必填
    backend: iq8                      # 可选：配置里用的名字
    origin: qcom                      # 可选:qcom | customer,默认 customer
    priority: 0                       # 可选，默认 0
    capabilities: []                  # 可选
```

含 `platforms` 字段的条目会被跳过并报错。同名插件：customer 覆盖 qcom(打 INFO);其他同名先扫到的生效(打 WARN)。

## 4. 配置：加载哪些 backend

按需加载：只加载配置里列出的 backend。

### 文件名

只读命名为 `*.config.yaml` 的文件，目录里的其他文件被忽略(多余的 `*.yaml` 会给一条 WARN)。后缀与 `*.plugin.yaml` 对称，所以清单不会被当成配置读。平台层的文件是 `<域>.config.yaml`(由 `platform-detect` 做的链接，没有 `NN`);客户层的文件是 `NN-<域>-<来源>.config.yaml`,`NN` 是两位数字，决定该层内文件的应用顺序(默认 `50`),例如 `50-camera-acme.config.yaml`。

```yaml
origin: customer        # 可选;不写时按所在层:客户目录里的算 customer,其他目录算 qcom
domains:
  camera:
    reset: true                 # 可选:忘掉前面文件对这个域设置的内容
    backends: [qcarcam, camx]   # 放到列表最前面的 backend 名，按此顺序
    disable: [v4l2]             # 可选：立即去掉这些
    compat: true                # 可选：兼容模式(默认 false)
```

| 来源 | 目录 | 说明 |
|---|---|---|
| platform 配置(Qualcomm) | `/etc/plugin_discovery/platform.d/` | `<域>.config.yaml` 链接，第一次开机时由 `platform-detect` 链接到与本机和 OS 匹配的候选文件(见下) |
| 客户配置 | `/etc/plugin_discovery/config.d/` | 在平台层之后应用，所以它说了算 |
| 人工指定 | `load({"a","b"})` 或 `PLUGIN_DISCOVERY_BACKEND_<DOMAIN>=a,b` | 整体替换上面两者 |

`PLUGIN_DISCOVERY_CONFIG_PATH`(`:` 分隔)可以替换这两个目录，供测试和试用;给出的目录一律算平台层，除非它是 `/etc/plugin_discovery/config.d`，或文件里写了 `origin: customer`。

### platform 配置候选文件与 `platform-detect`

platform 包为每个 machine、OS 类型、域各安装一份**候选文件**到 `<prefix>/share/plugin_discovery/platforms.d/`(用 `plugin_discovery_install_config()`),命名 `<machine>-<platform>-<域>.config.yaml`,例如 `QCS8275-Ubuntu-camera.config.yaml`。库不读这个目录。`<machine>` 取自 `/sys/devices/soc0/machine`,`<platform>` 是 OS 类型(`Ubuntu`:`ID=ubuntu`;`QCLinux`:`NAME` 以 "Qualcomm Linux" 开头);比较时不区分大小写。

`platform-detect`(本包的一个可执行文件)读取 machine 和 OS 类型，把每个匹配的候选文件链接成 `/etc/plugin_discovery/platform.d/<域>.config.yaml`。systemd oneshot 服务 `plugin-discovery-platform-detect.service` **只在第一次开机时**运行它(戳文件 `/var/lib/plugin_discovery/platform-detect.done`),所以人手改过的链接会保留;应用的 unit 用 `After=` 排在它之后。没有匹配或 OS 未知时不建链接并给出 WARN:这个域保持未配置。选项:`--force`(重新运行，刷新它建过的链接)、`--dry-run`、`--candidates`、`--platform-dir`、`--stamp`、`--machine-file`、`--os-release`。设计理由见 [`CONFIG_AND_PROBE.zh-CN.md`](CONFIG_AND_PROBE.zh-CN.md)。

### 文件怎么合并

- **顺序**：所有文件在 `scan()` 时合并成每个域一份最终列表，后者覆盖前者(与 systemd drop-in 一致)：先所有平台层文件，再所有客户层文件(不论文件名)，各自按文件名升序(客户层即 `NN` 升序)。同一个域块里，各键按 `reset`、`backends`、`disable` 的次序生效。
- **`backends`** 把名字放到列表最前面，保持该文件内的顺序;已在列表中的名字移到最前面。**`disable`** 把名字从当前列表去掉;后面的文件再次列出该 backend 就重新启用。**`reset: true`** 清空前面文件为这个域建立的列表，然后接上本块自己的 `backends`。
- **结果**：最终列表的第一项就是默认 backend。例：`50` 文件 `[qmmf, qcarcam]`，`60` 文件 `[v4l2]`，结果是 `v4l2, qmmf, qcarcam`。
- **`compat`**(每个域一个，默认 `false`)是标量：最后设置它的文件生效，`reset` 恢复为 `false`。见下面“选择 backend”。
- **每个文件都参与**：不存在同名文件替换，也不存在用空文件或指向 `/dev/null` 的链接屏蔽;客户只能用 `reset` 和 `disable` 覆盖。

示例，平台配置列出 `[a, b]` 并 `disable: [c]`:

| 客户文件 | 加载的 backend |
|---|---|
| 无 | a、b |
| `backends: [my]` | my、a、b |
| `backends: [my]`、`disable: [a]` | my、b |
| `backends: [c]`(撤销平台的禁用) | c、a、b |
| `reset: true`、`backends: [my]` | my |

以下都是实现暂定(详细设计时可能改变;清单在 plugin-discovery-design-note 的 `OPEN_QUESTIONS.md` 第二节):
- 某个域**没有任何配置**时，加载该域所有兼容的 backend,并 WARN 提示没有 platform 配置与本机匹配。
- 装了但没列进配置的 backend 不加载，记为 `not_listed`。
- 客户 backend 与 Qualcomm backend 的 `backend` 名相同时，客户的替换 Qualcomm 的。
- 两个 backend 报出同一个非空设备节点时，排在前面的保留;后面的不再服务该设备，如果它因此没有设备了就卸载。

### 选择 backend

`load()` 会加载并 probe 列表里的每个 backend(`Unsupported` / `Fault` 照旧卸载)。调用方不指定 backend 时，用**最终列表的第一项**(`BackendSet::first`)，不会用 probe 结果去挑；如果它没有加载(没安装、Unsupported、Fault)，或打不开设备，调用就失败;配置里写了但没安装的名字同样算第一项，并报告为 `missing`。该域的 `compat: true` 会被解析并暴露(`BackendSet::compat`、`pluginlib-detect`)，但目前选择时不使用它:不会回退到下一项。人工指定(`load({...})`、`PLUGIN_DISCOVERY_BACKEND_<DOMAIN>`)绕过配置和 `compat`。

### 查看合并结果:`pluginlib-detect`

```
$ pluginlib-detect                 # 所有域;-d camera 只看一个
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

列出各目录、找到的每个配置文件及状态(`applied` 或 `invalid`),以及每个域的加载顺序和每一项出自哪个文件,类似 `systemctl cat`。它只读清单和配置:不加载任何库，不做 probe。认 `PLUGIN_DISCOVERY_PATH` / `PLUGIN_DISCOVERY_CONFIG_PATH`,也可用 `--path` / `--config-path`;设置了 `PLUGIN_DISCOVERY_BACKEND_<DOMAIN>` 时会提示。程序里可用 `PluginRegistry::config_files()` 和 `config_view()` 得到同样的数据。

### 使用

```cpp
#include "plugin_discovery/plugin_registry.hpp"

plugin_discovery::PluginRegistry registry;   // 清单：$PLUGIN_DISCOVERY_PATH;配置：见上表
registry.scan();                             // 只读清单和配置，不加载任何 .so

auto set = registry.load<hello::Greeter>();  // 按配置加载、probe、汇总
for (auto & b : set.backends) {              // 按配置顺序;set.first 是默认项
  b.handle->greet();                         // b.devices：这个 backend 能服务的设备
}
for (auto & s : set.skipped) { /* s.name, to_string(s.kind), s.reason */ }

registry.load<hello::Greeter>({"iq9"});      // 人工指定
registry.create<hello::Greeter>("hello/IQ8Greeter");   // 按名字创建一个(也做版本 / ABI 校验)
registry.plan("hello::Greeter", "hello");    // 只看会加载哪些，不加载
```

`load()` 对单个 backend 的失败不抛异常，结果都在 `skipped` 里，并汇总打印一次。`PluginHandle` 持有对象和它所在的库，`reset()` 先销毁对象再释放库。

环境变量：

| 变量 | 作用 |
|---|---|
| `PLUGIN_DISCOVERY_PATH` | 清单搜索目录，`:` 分隔 |
| `PLUGIN_DISCOVERY_CONFIG_PATH` | 配置目录，`:` 分隔，替换默认目录 |
| `PLUGIN_DISCOVERY_BACKEND_<DOMAIN>` | 人工指定该域加载哪些 backend,`,` 分隔 |
| `PLUGIN_LOADER_LOG_LEVEL` | 加载层日志级别 |

### 加载层

```cpp
#include "plugin_loader/plugin_loader.hpp"
plugin_loader::ClassLoader loader("/path/libfoo.so");              // 立即加载;失败抛 LibraryLoadException
auto p = loader.createUniqueInstance<Base>("Derived");             // 也有 createInstance / createUnmanagedInstance
loader.getAvailableClasses<Base>();  loader.isClassAvailable<Base>("Derived");

plugin_loader::MultiLibraryClassLoader multi(false);
multi.loadLibrary(path);  multi.createInstance<Base>("Derived");   // 多库
```

只用加载层时，注册宏是 `PLUGIN_REGISTER_CLASS(Derived, Base)`(不做版本校验)。插件和宿主**必须链接同一个** `libplugin_loader.so`。

## 5. 与 class_loader 的对应

类名、方法名、异常层级与 `class_loader` 一致，命名空间换成 `plugin_loader`，注册宏换成 `PLUGIN_REGISTER_CLASS`（另有 `_WITH_MESSAGE`）。

| class_loader | plugin_loader | 状态 |
|---|---|---|
| `ClassLoader`（含 on-demand、计数、受管 / 未受管实例） | 同名 | 对齐 |
| `MultiLibraryClassLoader` | 同名（其内部辅助方法与上游一样是 private） | 对齐 |
| `ClassLoaderException` / `LibraryLoadException` / `LibraryUnloadException` / `CreateClassException` / `NoClassLoaderExistsException` | 同名 | 对齐 |
| `systemLibraryFormat` | 同名（另有 `systemLibrarySuffix`） | 对齐 |
| `CLASS_LOADER_REGISTER_CLASS(_WITH_MESSAGE)` | `PLUGIN_REGISTER_CLASS(_WITH_MESSAGE)` | 对齐 |
| 同名类冲突：后注册覆盖并告警 | 同 | 对齐 |
| 库在 ClassLoader 之外被打开（直接链接）→ 类无主、之后任何库都不再卸载 | 同 | 对齐 |
| 日志走 console_bridge | 走 stderr，`PLUGIN_LOADER_LOG_LEVEL=debug\|info\|warn\|error\|none` | 替代 |

### 与上游**有意不同**的地方（由差分测试固化在 `test/expected/`）

1. **`isLibraryLoaded()` 表示"本 loader 持有该库"**。上游对任何已打开的库都返回 true，与它自己的文档相矛盾。
   连带结果：放弃库的 loader 再创建对象时会自动重新加载；共享库的 on-demand loader 首次创建时会自动加载
   （上游此时抛 `CreateClassException`）。
2. **`MultiLibraryClassLoader::loadLibrary` 失败之后继续调用不会崩溃**（上游在特定调用序列后稳定段错误）。

### 不兼容的部分

- **不是二进制兼容**：工厂表在 `libplugin_loader.so` 里。用 `PLUGINLIB_EXPORT_CLASS` / `CLASS_LOADER_REGISTER_CLASS`
  编出的已有 `.so` 注册不进来，需要把注册宏换成 `PLUGIN_REGISTER_CLASS` 后重编。
- 不能与上游 class_loader 在同一进程共享一张表。
- 只支持 Linux（`dlfcn`）。
- 与上游一样:`ClassLoader` 必须比它创建的对象活得久(管理层的 `PluginHandle` 已经保证了这一点);
  相对路径与绝对路径是两个不同的库键。

## 6. 测试

`test/scenarios.cpp` 是同一份源码，既能用 `plugin_loader` 编译，也能用 `-DUSE_UPSTREAM` 针对真正的 class_loader 编译
（API 相同本身就是兼容性证据）。每个场景在独立进程里运行，打印转录，逐行比较。

```bash
# 只测 plugin_loader（不需要 ROS）
cmake -S . -B build-test -DPLUGIN_LOADER_BUILD_TESTS=ON
cmake --build build-test -j
test/run_diff.sh build-test
build-test/test/registry_test build-test/test/backends   # PluginRegistry:配置、probe、版本 / ABI、去重
test/platform_detect_test.sh build-test                   # platform-detect

# 同时与上游 class_loader 差分（只有这一步需要 ROS 环境）
source /opt/ros/jazzy/setup.bash
cmake -S . -B build-test -DPLUGIN_LOADER_COMPARE_UPSTREAM=ON
cmake --build build-test -j && test/run_diff.sh build-test
```

三种构建对比：

- **upstream** —— ROS class_loader。
- **ours** —— `plugin_loader`，测试库带 `-z nodelete`：上游的测试库含 `STB_GNU_UNIQUE` 符号，
  `dlclose` 后并不会真正卸载，加 `-z nodelete` 让两边处于同一情形（同时覆盖"库仍在内存里"的复活路径）。
- **deletable** —— `plugin_loader`，普通库；`mapped=` 屏蔽后必须与 ours 一致，并断言它真的被卸载。

27 个场景;不与上游比较时共 34 项检查(ours 与 deletable 逐场景比对，加上 "确实被卸载" 的断言),其中 4 个场景按第 5 节的有意差异与 `test/expected/` 比对。
场景覆盖：三种创建方式、作用域过滤、多 loader 共享库、加载 / 卸载计数、存活对象时卸载、析构卸载、
on-demand 惰性加载与自动卸载、未受管实例抑制卸载、异常类型、同名冲突、卸载后重载、多线程、
`MultiLibraryClassLoader` 全部公开方法、直接链接的库（非纯）。
