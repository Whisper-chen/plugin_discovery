# plugin_discovery 实现说明

[English](DESIGN.md) | **中文**

本文描述**当前代码**怎么工作。设计依据见 [`CROSS_PLATFORM_DESIGN.zh-CN.md`](CROSS_PLATFORM_DESIGN.zh-CN.md);API 参考见 [`LIBRARY.zh-CN.md`](LIBRARY.zh-CN.md);标为"实现暂定"的行为对应留给详细设计的问题(清单在 plugin-discovery-design-note 的 `OPEN_QUESTIONS.md` 第二节),可以推翻。

目标：不依赖 ROS(无 colcon / ament / pluginlib / class_loader),按配置决定加载哪些 backend。每个 backend 通过自己的 vendor SDK 报出这台机器上能用的设备。用于 camera / imu / audio / lidar 等多个**相互独立**的域：每个域有自己的接口和一组 backend。

## 1. 分层与组件

| 层 | 组件 | 职责 | 对标 |
|---|---|---|---|
| L4 应用层 | App(客户应用、客户 middleware;ROS 节点) | 调用域接口，按设备使用;**不认 SoC、不做设备探测** | — |
| L3 管理层 | `libplugin_discovery.so`:`PluginRegistry`、`BackendSet<T>`、`PluginHandle<T>`、`PluginInfo`、`Skipped`、`Backend` 基类与宏 | 读清单和配置，决定加载哪些 backend,校验接口版本与 ABI,调用 `probe()`,汇总设备 | pluginlib |
| L2 加载层 | `libplugin_loader.so`:`ClassLoader`、`MultiLibraryClassLoader`、全局工厂表、异常 | dlopen / dlclose、注册宏、工厂表、引用计数、受管 / 未受管实例 | class_loader |
| L1 插件层 | 各 backend 的 `.so`(`PLUGIN_DISCOVERY_REGISTER_BACKEND(Derived, Interface)`)及其私有 vendor 库 | 实现域接口;`probe()` 问自己的 vendor SDK | — |

旁路角色：

- **域接口**:派生自 `plugin_discovery::Backend` 的抽象类，用 `PLUGIN_DISCOVERY_INTERFACE(Interface, "domain", major, minor)` 声明域名和版本。App 和 backend 都依赖它，App 不依赖任何 backend。
- **清单**(`*.plugin.yaml`):每个 backend 一份，描述名字、类、接口、库路径、domain、backend 名、origin、priority、capabilities。由 CMake 生成，安装到 `share/<域>/plugins/`。
- **配置**(`*.config.yaml`):决定每个域加载哪些 backend。来源有三种:
  - 平台配置，在 `/etc/plugin_discovery/platform.d/`:指向候选文件的链接,候选文件由平台包安装在 `share/plugin_discovery/platforms.d/`(库从不读候选文件)。`platform-detect` 可执行文件由 systemd oneshot 单元在首次启动时运行一次,选出与机型和 OS 类型匹配的候选文件;
  - 客户配置，在 `/etc/plugin_discovery/config.d/`;
  - 人工指定。

  平台层的文件先应用,客户层的文件后应用;同一层内按文件名升序,最后一个说了算。每个文件都会参与;客户只能用 `reset` 和 `disable` 覆盖。

依赖关系：

```
App ──> 域接口 <── backend .so ──> vendor SDK
App ──> libplugin_discovery.so ──> libplugin_loader.so <── backend .so
backend .so ──> libplugin_discovery.so      (注册宏把版本记录写在这里)
libplugin_discovery.so ──读──> *.plugin.yaml、platform.d/*.config.yaml、config.d/*.config.yaml
```

App 与 backend 必须链接**同一个** `libplugin_loader.so` 和 `libplugin_discovery.so`。

## 2. 关键类型

- `Backend`:所有 backend 的基类，只有一个虚函数 `ProbeResult probe()`。默认返回 `ok`,带一个 id 为 `default` 的设备。
- `ProbeResult { status, devices, message }`:`status` 取 `Ok` / `Unsupported` / `Fault`;`DeviceInfo { id, node, description }`。
- `PluginRegistry`
  - `scan()`:读所有清单和配置，不加载任何 `.so`
  - `plan(base, domain, manual, caps)`:只计算会加载哪些 backend,返回 `LoadPlan`
  - `load<T>(manual, caps)`:依次 `create`、`probe`、去重设备，返回 `BackendSet<T>`
  - `create<T>(info | name)`:加载一个 backend,先做版本和 ABI 校验
  - `list()`、`find()`、`all()`
- `BackendSet<T> { backends, skipped, how, first, compat }`:
  - `backends` 的每一项是 `LoadedBackend { PluginHandle<T> handle; vector<DeviceInfo> devices; }`;
  - `how` 取 `manual` / `config` / `all`;
  - `first` 是最终列表的第一项(默认 backend)，不论是否加载成功、是否安装(配置了但没安装的名字原样返回);`compat` 是该域的兼容模式开关(仅来自配置)。
- `Skipped { name, kind, reason }`:`kind` 取 `not_listed`、`disabled`、`missing`、`lacks_capability`、`incompatible`、`load_failed`、`unsupported`、`fault`、`duplicate_device` 之一。
- `PluginHandle<T>`:同时持有对象和它的 `ClassLoader`;`reset()` 和析构都保证先销毁对象、再释放库。

## 3. 构建期

1. 接口头文件里写 `PLUGIN_DISCOVERY_INTERFACE`。
2. backend 的 `.cpp` 里写 `PLUGIN_DISCOVERY_REGISTER_BACKEND(Derived, Interface)`,它会展开成两部分：
   - `PLUGIN_REGISTER_CLASS(Derived, Interface)`:写入 plugin_loader 的工厂表;
   - 一个静态对象：库被 dlopen 时调用 `detail::record_backend(...)`,在 `libplugin_discovery.so` 里登记 {库路径(由 `dladdr` 取得)、接口名、类名、编译时的 major.minor、ABI tag}。
3. CMake 调用 `plugin_discovery_add_plugin(TARGET … DOMAIN … NAME … CLASS … BASE … [BACKEND] [ORIGIN] [PRIORITY] [CAPABILITIES])`,它做三件事：
   - 给 target 链接 `plugin_discovery::plugin_discovery`;
   - 把 `.so` 安装到 `lib/<域>/`;
   - 用 `file(GENERATE)` 生成清单，安装到 `share/<域>/plugins/`(独立 component `manifests`)。

   传入 `PLATFORMS` 会报 `FATAL_ERROR`。
4. 平台包用 `plugin_discovery_install_config(<file>...)` 把候选配置文件安装到 `share/plugin_discovery/platforms.d/`。首次启动时 `platform-detect` 把与机型和 OS 类型匹配的候选文件链接到 `/etc/plugin_discovery/platform.d/`。

安装目录：

```
<prefix>/
├── bin/<app>
├── lib/libplugin_loader.so  libplugin_discovery.so
├── lib/<域>/lib<backend>.so  lib<vendor>.so
├── share/<域>/plugins/<backend>.plugin.yaml
├── bin/platform-detect
├── share/plugin_discovery/platforms.d/*.config.yaml     (平台包的候选文件;库不读)
/etc/plugin_discovery/platform.d/<域>.config.yaml        (platform-detect 创建的链接)
/etc/plugin_discovery/config.d/*.config.yaml             (客户配置)
/var/lib/plugin_discovery/platform-detect.done           (stamp 文件)
```

## 4. 运行期

### 4.1 决定加载哪些 backend(`PluginRegistry::plan`)

```
候选池 = base 等于该接口、且具备所有 required capabilities 的插件
        排序：customer 在前 → priority 降序 → name 升序(仅在没有别的顺序可依时使用)

wanted(要加载的 backend 名，有序):
  1. load() 的 manual 参数                         how=manual
  2. 否则 PLUGIN_DISCOVERY_BACKEND_<DOMAIN>=a,b    how=manual
  3. 否则配置:合并后的最终列表(后面的文件说了算，文件里的 backend 放到最前面)，去掉 disable   how=config;兼容开关取自该域配置
  4. 都没有 → 候选池全部，并 WARN 提示没有匹配的平台配置   how=all   (实现暂定)

对每个 wanted 名：取候选池里 backend 名相同的插件;一个都没有 → skipped[missing]
同一个 backend 名同时有 customer 和 qcom 插件 → 只留 customer 的，qcom 的记 skipped[disabled]
没被选中的插件 → skipped[not_listed] 或 skipped[disabled]                        (实现暂定)
```

### 4.2 加载、probe、汇总(`PluginRegistry::load<T>`)

```
App          PluginRegistry           ClassLoader          backend .so          vendor SDK
 │ scan() ──────>│ 读清单、配置              │                     │                     │
 │ load<T>() ───>│ plan()                    │                     │                     │
 │               │ 对每个计划中的插件：      │                     │                     │
 │               │  new ClassLoader(path) ──>│ dlopen ────────────>│ 静态初始化：         │
 │               │                           │                     │  注册工厂 + 版本记录 │
 │               │  校验版本 / ABI(查记录)  │                     │                     │
 │               │  createUniqueInstance ───>│ new 对象            │                     │
 │               │  probe() ─────────────────────────────────────> │ 问 SDK ────────────>│
 │               │  <── Ok(devices) / Unsupported / Fault          │                     │
 │               │  去掉已被前面 backend 报过的同一设备节点                               │
 │ <── BackendSet<T>(backends + 设备、skipped、how);打印一次汇总                         │
 │ b.handle->...(按设备使用)                                                              │
 │ BackendSet 析构：每个 handle 先销毁对象，再 dlclose                                      │
```

每个结果怎么处理：

| 情况 | 处理 |
|---|---|
| 库加载失败 / 类未注册 | `skipped[load_failed]`,继续下一个 |
| 未用新宏注册、major 不同、minor 比 App 旧、ABI tag 不同 | `skipped[incompatible]`,不创建对象;报错写明双方的版本或工具链 |
| `probe()` 返回 `Unsupported` | 卸载,`skipped[unsupported]`,INFO |
| `probe()` 返回 `Fault` 或抛异常 | 卸载,`skipped[fault]`,ERROR;其他 backend 照常(域的 `compat` 只影响选择，不影响这里) |
| 报出的设备节点已被前面的 backend 报过 | 不给它这个设备,`skipped[duplicate_device]`;如果它因此没有设备了，就卸载(实现暂定) |

调用方的选择(如 `CameraManager`):不指定 backend → 用 `first`;它没加载或打不开设备，调用失败。`compat` 目前只读取、不生效:不回退。指定了名字就按名字用。

`load()` 不因单个 backend 失败而抛异常;`create()` 失败抛 `PluginError`。

### 4.3 版本与 ABI 规则(实现暂定)

- **major**:必须相等。**minor**:backend 编译时的 minor 不能低于 App 编译时的 minor。
- **ABI tag**:`gcc<主版本>` 或 `clang<主版本>`,加上 `-cxx11abi<_GLIBCXX_USE_CXX11_ABI>`,两边必须相同。可以预定义 `PLUGIN_DISCOVERY_ABI_TAG` 覆盖，测试用这一点伪造不一致。
- **校验依据**:用的是 `.so` 自己登记的记录，不是清单，所以清单和 `.so` 不会对不上。
- **按库区分记录**:记录按 `dladdr` 得到的库路径区分，因此同一个类名出现在两个库里也不会混淆。

## 5. 与 class_loader / pluginlib 的对应

| 本项目 | 对标 | 说明 |
|---|---|---|
| `plugin_loader` | `class_loader` | 接口对齐(类名、方法名、异常;注册宏换名为 `PLUGIN_REGISTER_CLASS`);27 个场景与上游做差分测试 |
| `plugin_discovery` | `pluginlib` | 职责对应(发现 + 创建),API 不同：按配置加载多个 backend、`probe()`、版本 / ABI 校验;不依赖 ROS 包系统 |

不兼容的地方：
- 不是二进制兼容：用 `PLUGINLIB_EXPORT_CLASS` 编出的 `.so` 注册不进来;
- 用旧宏 `PLUGIN_REGISTER_CLASS` 注册的 backend 能被 plugin_loader 加载，但 plugin_discovery 会拒绝它，原因是缺少版本记录;
- 只支持 Linux。

## 6. 测试

- `test/run_diff.sh <build>`:plugin_loader 的 27 个场景，共 34 项检查。
- `test/registry_test <build>/test/backends`:34 项检查，覆盖：
  - 合并结果视图(`config_view()`,`pluginlib-detect` 用的);
  - 无配置时的兜底;
  - platform 配置、客户配置与 `disable`;
  - 人工指定(参数和环境变量)、缺失的 backend 名;
  - capability 过滤;
  - customer 替换同名 qcom backend;
  - probe 的三种结果;
  - minor 过旧、ABI 不一致;
  - 同一设备节点去重;
  - 带 `platforms` 字段的清单被拒绝;
  - 按名字创建;
  - 库文件不存在时报 `load_failed`,其他 backend 照常加载。
- 测试用的 backend 由 `test/backends/test_backend.cpp` 用不同的宏定义编出 8 个库。
