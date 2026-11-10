# Robotics SDK 跨平台：plugin_discovery 顶层设计(HLD)

[English](CROSS_PLATFORM_DESIGN.md) | **中文**

状态：顶层设计，2026-10-28 确认。代码已按本文实现(仓库根目录的 `include/`、`src/`),**尚未在真机验证**。

- 本文只讲顶层：plugin_discovery 在系统里的位置、职责，以及它和 backend、vendor SDK、配置之间的关系。流程细节、字段、格式留到详细设计(第 7 节)。
- 设计结论的来源、留到详细设计的问题、各域 / backend 的工作项和架构图，都在单独的仓库 plugin-discovery-design-note。
- 当前代码的实现说明见 [`DESIGN.zh-CN.md`](DESIGN.zh-CN.md),API 参考见 [`LIBRARY.zh-CN.md`](LIBRARY.zh-CN.md)。

## 1. 要解决的问题

Robotics SDK 要在多种 chipset(IQ8 / IQ9 / IQ10 …)和两种 OS(Ubuntu、QLI)上提供**同一套接口**。而底层的硬件访问由各家 vendor SDK 完成(QCarCam、CamX、RealSense、Orbbec …),它们彼此不同。

| 约束 | 内容 |
|---|---|
| Ubuntu 一个镜像覆盖所有 platform | 一个镜像加一个 apt 源同时给 IQ8 / IQ9 / IQ10 使用，因此不能在构建时就定死用哪个 vendor SDK |
| QLI 每个 machine 一个镜像 | 机制要相同，只是候选更少 |
| 有不用 ROS 的客户 | 客户使用自己的 middleware;ROS 只是可选的接入方式之一 |
| 客户要扩展 | 客户可以写自己的 backend,也可以修改 QCOM 开源的 backend,且客户的优先 |
| 一台机器上多个设备 | 例如 MIPI camera 和 USB 深度相机同时存在，分别由不同的 vendor SDK 服务 |

域:camera / video / audio / imu / lidar。各域有自己的接口和 backend,互相独立;plugin_discovery 是各域共用的唯一部分。

## 2. 基础模型:backend / vendor SDK / device / chipset

| 关系 | 事实 |
|---|---|
| backend → vendor SDK | 目前一个 backend 只封装一个 vendor SDK;要保留以后封装多个的扩展性 |
| chipset → vendor SDK | 一个 chipset 上一定能跑多个 SDK |
| vendor SDK 的身份 | 名字 + 版本;版本不同就改名，算作另一个 SDK |
| backend → device | 设备节点对所有 backend 都可见，但只有**适配了这个设备**的 backend 才能用它 |
| 设备标识 | 各 SDK 都会报出设备节点路径，但彼此不统一 |

由此得出三点：
1. 一台机器上同时有多个 backend 在工作，这是常态。
2. 设备由 backend 通过自己的 SDK 发现,plugin_discovery 不直接发现设备。
3. chipset 不进入 plugin_discovery。

## 3. 分层与职责

```
 Consumer：客户应用 · 客户 middleware · ROS 节点(例如唯一的 qrb_ros_camera)
        │  只调用域接口，按设备使用
        ▼
 域接口(每个域一个公开 C++ 接口，版本化)
        ▲  被所有 backend 实现
        │
 plugin_discovery ──读──▶ 配置(platform 配置 · 客户配置文件 · 人工配置)
        │  按配置加载 backend,汇总它们报出的设备
        ▼
 backend A        backend B        客户 backend    …(同一进程里并存)
        │                │                │
 vendor SDK A     vendor SDK B     客户 / 第三方 SDK
        │                │                │
 硬件 / 内核
```

| 层 | 职责 | 不负责 |
|---|---|---|
| Consumer | 只调用域接口，按设备使用 | 不知道有哪些 backend,不认 SoC |
| 域接口 | 每个域一个公开 C++ 接口，所有 backend 都实现它 | — |
| plugin_discovery | 读配置，决定加载哪些 backend;加载它们;把各 backend 报出的设备汇总给 consumer | 不认 SoC,不直接发现设备，不关心 backend 内部实现 |
| backend | 封装一个 vendor SDK;通过 SDK 报出自己能用的设备;处理 SoC 差异 | — |
| vendor SDK | 发现并驱动硬件 | — |
| 配置 | 决定一台机器上加载哪些 backend | — |

## 4. 运行时

plugin_discovery 读取配置，只加载其中列出的 backend(**按需加载**)。每个被加载的 backend 通过自己的 vendor SDK 报出它能用的设备;plugin_discovery 把这些设备汇总交给 consumer,consumer 按设备使用。多个 backend 在同一个进程里同时工作。

配置有三个来源：

| 来源 | 谁提供 | 作用 |
|---|---|---|
| platform 配置 | QCOM,每个 machine 和 OS 一份 | 列出该 platform 上完全支持的 backend;第一次开机时由 plugin_discovery 之外的一个服务选出与本机匹配的那份,开箱即用 |
| 客户配置文件 | 客户 | 把客户自己的 backend 加进来;优先于 platform 配置 |
| 人工配置 | 集成方 / 用户 | 覆盖上面两者 |

## 5. 部署

| 场景 | 方式 |
|---|---|
| Ubuntu | 一个统一镜像跑 IQ8 / IQ9 / IQ10。镜像里带上所有 machine 的 platform 配置;第一次开机时由 `platform-detect` 服务读取 machine 和 OS 类型，启用匹配的那份。plugin_discovery 只读被启用的配置，始终不认识 SoC |
| QLI | 机制相同;镜像可以只带自己需要的配置 |
| 客户 | 安装自己的 backend,再加一个配置文件 |

backend 按 vendor SDK(软件栈)打包，不按 chipset 打包。例如 CamX 的一个包服务 iq8 和 iq9。

## 6. 设计原则

| # | 原则 |
|---|---|
| D1 | **需要运行时选择**:Ubuntu 一个镜像覆盖多个 platform;QLI 只是候选更少，走同一套机制 |
| D2 | **自研，不依赖 ROS**:plugin_discovery 和 backend 都不依赖 ROS;ROS 节点只是 consumer 之一 |
| D3 | **对外开放**:域接口、插件机制、配置格式都是公开 API;客户可以写 backend,也可以修改 QCOM 开源的 backend |
| D4 | **兼容性**:默认使用 QCOM 验证过的工具链，接口可以使用 `std::` 类型;加载时校验版本和 ABI;版本规则由 QCOM 定，并以对实现方是否兼容为准 |
| D5 | **只认软件栈，不认 SoC**:SoC 差异由 backend 处理;plugin_discovery 中没有 SoC 的概念 |
| D6 | **按需加载**:加载哪些 backend 由配置决定(platform 配置 / 客户配置 / 人工配置) |
| D7 | **多个 backend 并存**:一个域接口可以同时加载多个 backend,各自服务不同的设备，在同一个进程里工作 |
| D8 | **客户优先**:客户的 backend 和配置优先于 QCOM 的 |
| D9 | **对 backend 的契约**:实现域接口;通过自己的 SDK 报出能用的设备;不支持时明确告知，不崩溃、不留副作用;能和其他 backend 同进程共存 |

## 7. 不在本文范围内(留到详细设计)

| 主题 | 说明 |
|---|---|
| 选择与配置的细节 | 配置格式与位置、多份配置的合并规则、客户能否替换 QCOM 的 backend、没有配置时的兜底、两个 backend 适配同一设备时的处理、"不支持"与"故障"的区分。清单见 plugin-discovery-design-note 的 `OPEN_QUESTIONS.md` 第二节 |
| 清单字段与搜索路径 | 哪些字段保留、目录约定 |
| 数据面 | frame 模型、零拷贝、fd 传递 |
| 生命周期与线程 | 对象与库的销毁顺序、回调线程约定 |
| daemon 类 SDK 与进程外 backend | 连接所有权、掉线处理、是否需要进程隔离 |
| 错误处理与诊断工具 | |
