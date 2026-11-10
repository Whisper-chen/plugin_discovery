# 配置为什么这样设计,以及 `probe()` 的逻辑

本页解释配置规则和 `probe()` 背后的理由。具体格式见 [`LIBRARY.zh-CN.md`](LIBRARY.zh-CN.md);设计原则 D1–D9 见 [`CROSS_PLATFORM_DESIGN.zh-CN.md`](CROSS_PLATFORM_DESIGN.zh-CN.md)。

## 1. 配置要解决什么问题

- 配置有多个所有者:QCOM 每个 platform 发一个包,客户在其上增删,开发者为一次运行临时覆盖(D6、D8)。
- 任何一方都不应该去改别人的文件。包只安装自己的文件,互不打补丁。
- 库不认 SoC(D5)。哪些 backend 属于这台机器,由配置说了算,不是由代码。
- 结果是每个域**一份有序列表**。第一项就是默认 backend,所以顺序本身就是语义。

## 2. 规则与理由

| 规则 | 理由 |
|---|---|
| 每个包一个小文件,命名 `NN-<域>-<来源>.config.yaml`(平台层:链接 `<域>.config.yaml`,见第 5 节) | 一个包增删自己的文件,不碰别人的。后缀与 `*.plugin.yaml` 对称,清单不会被当成配置 |
| 两层:先平台,后客户 | D8 客户优先。不管双方用什么 `NN`,客户文件总是压过 QCOM 的 |
| 层内按 `NN` 升序,后者说了算 | 与 systemd drop-in 的习惯一致,大家已经熟悉 |
| 一个文件的 `backends` 放到列表**最前面** | “后者说了算”和“第一项是默认”要同时成立。如果追加到末尾,最早的文件就会一直占着默认位置 |
| `disable` 立即生效,后面再列出就重新启用 | 客户可以去掉 QCOM 的 backend,后面的文件又能撤销,谁都不用改别人的文件 |
| `reset: true` | 从零开始,不继承平台的列表(相当于 systemd 里的空赋值) |
| `compat` 按域设置,是标量 | 能不能回退取决于域,不取决于机器。标量只有一个明确的主人:最后设置它的文件 |
| 没有同名替换和 `/dev/null` 屏蔽:客户只能用 `reset` 和 `disable` 覆盖 | 只有一种覆盖机制,不是三种;每个文件都参与合并,`pluginlib-detect` 能显示每项出自哪个文件 |
| 运行时(`scan()`)合并成每个域一份最终列表 | 加载和选择只看这一份结果,没有别的东西再做决定。`pluginlib-detect` 显示的就是这份结果,以及每项出自哪个文件 |
| 人工指定整体替换 | 开发者不改文件就能为一次运行固定 backend |
| 默认取第一项,失败就是错误 | 用户能看到配置错误,而不是悄悄回退到另一套软件栈。`compat: true` 是显式选择回退 |

## 3. 为什么不适合 `sshd_config` 这种风格

`sshd_config` / `ssh_config` 对每个关键字取**第一个**值。它们的场景是一个主文件,把具体的 `Host` / `Match` 块写在通用块前面,这种取法合适。放到这里不合适:

1. **多个互相独立的所有者。** 包文件是投放到目录里的。first-wins 下,后加的文件永远覆盖不了先到的,客户文件就得起一个排在所有 QCOM 文件之前的名字。last-wins 加分层,客户只要排在后面即可。
2. **前缀的含义正好相反。** Ubuntu 默认在 `sshd_config` 开头 `Include`,于是 `10-` 优先于 `50-`;systemd 和本库是 `50-` 压过 `10-`。两种习惯混用会让管理员困惑,我们沿用 drop-in 的那一种。
3. **列表带语义。** ssh 的设置多是标量或累加的集合。这里列表顺序决定默认项,条目要能删除(`disable`)、能调整位置(移到最前),ssh 没有这些操作。
4. **不需要条件匹配。** `Host` / `Match` 按“谁在连接”选择设置。这里“这台机器是什么”的答案来自 `probe()`(第 4 节),不是文件里的匹配模式。
5. **只有一种覆盖方式。** systemd 还允许同名文件替换、用 `/dev/null` 链接屏蔽。我们两样都没有采用:`reset` 和 `disable` 已经能表达客户的意图,只保留一条规则也更容易讲清楚。

与 systemd 有意不同的地方:没有同名替换和 `/dev/null` 屏蔽;客户层不管 `NN` 一律在平台层之后应用(D8);`backends` 插到最前(顺序即语义);有显式的 `disable`。

## 4. `probe()` 的逻辑

配置说的是哪些 backend **可以**加载;`probe()` 是每个 backend 自己回答它**能不能**在这台机器上运行。库不替它判断(D5、D9)。

### 约定

`probe()` 在 backend 对象创建后调用一次,向 vendor SDK 询问,并返回:

| 结果 | 含义 | 库的处理 |
|---|---|---|
| `Ok` + 设备 | 这里能用;给出它能服务的设备 | 保留 backend,记录设备 |
| `Unsupported` | 不属于这台机器(SDK 不能在这个 SoC 上运行) | 卸载,INFO |
| `Fault` | 应该能用但出了问题(设备错误、守护进程没起来),抛异常也算 | 卸载,ERROR;其他 backend 照常加载 |

返回 `Unsupported` 时,backend 不能崩溃,也不能留下任何残留。默认的 `probe()` 返回 `Ok` 和一个叫 `default` 的设备,给没有设备可枚举的 backend 用。

### `load<T>()` 里的流程

```
计划:    该域的最终列表(人工指定 > 配置 > 全部兼容的)
按列表顺序,对每个计划中的 backend:
    创建对象        -- 失败:load_failed / incompatible(版本、ABI),跳过
    probe()
        Unsupported -> skipped[unsupported],卸载
        Fault       -> skipped[fault],卸载
        Ok          -> 去掉前面的 backend 已经报过的相同非空 node 的设备
                       (skipped[duplicate_device]);一个都不剩 -> 卸载
                       否则保留:backend + 它的设备
结果:    backends 按列表顺序;skipped 带原因;first = 列表第一项
```

### probe 不做什么

- **不决定默认项。** 默认项是列表的第一项,不是“第一个 probe 成功的”。probe 成功只说明 SDK 在,不说明设备一定能打开。
- **不是兼容模式。** `compat` 只涉及打开设备时的选择(按顺序尝试,第一个能打开的生效;目前关闭)。`Fault` 的处理两种模式下相同。
- **库这边不去读 SoC。** backend 的 SDK 不存在或拒绝运行时,由它自己回答 `Unsupported`,库只负责报告。

### 应用侧怎么选

1. 指定了名字:就用它。
2. 否则用 `BackendSet::first`。如果它没有加载(Unsupported、Fault、没安装)或打不开设备,调用失败。
3. 该域的 `compat: true` 原本用于按列表顺序尝试已加载的 backend(第一个能打开的生效)。目前只解析、不生效,所以不回退。

未在真机验证的:真实 vendor SDK 给出的 Unsupported / Fault,以及 QMMF 的 node 去重(QMMF 不报设备节点)。

## 5. 第一次开机时选出 platform 配置(`platform-detect`)

同一个镜像要跑在多台机器上(D1),但库不认识 SoC(D5)。所以这个选择放在库**之外**,只在第一次开机时做一次,库只读结果。

```
/usr/share/plugin_discovery/platforms.d/       # 候选文件,由 platform 包安装;库不读
├── QCS8275-Ubuntu-camera.config.yaml
├── QCS8275-QCLinux-camera.config.yaml
└── <machine>-<platform>-<域>.config.yaml

/etc/plugin_discovery/platform.d/              # 平台层:库读这个目录(名字暂定)
└── camera.config.yaml  ->  /usr/share/plugin_discovery/platforms.d/QCS8275-Ubuntu-camera.config.yaml

/etc/plugin_discovery/config.d/                # 客户层,不变
└── 50-camera-acme.config.yaml
```

- **候选文件。** 每个 machine、OS、域各一份,命名 `<machine>-<platform>-<域>.config.yaml`。没有 `NN`:这个组合是唯一的,没有要排序的东西。`<machine>` 取自 `/sys/devices/soc0/machine`(例如 `QCS8275`);`<platform>` 是 OS 类型。比较时不区分大小写。目录名 `platforms.d/` 是暂定的。
- **链接放哪。** 放在 `/etc` 下它自己的目录里:`/usr` 只读时 `/etc` 仍然可写,重启后也还在。不能放进客户目录:库会把链接当成客户文件,而且 `camera.config.yaml` 按字典序排在 `50-…` 之后,平台就压过了客户(违反 D8)。所以库的平台层就是这个目录,文件默认的 `origin` 由它所在的层决定,不看路径。
- **服务。** `platform-detect` 随 plugin_discovery 一起发布,是独立的可执行文件(库本身仍不认识 machine)。由 systemd 的 oneshot 在**第一次开机时运行一次**(用一个标记文件保证,第二次开机什么都不做)。它读取 machine 和 OS 类型,把每个匹配的候选文件链接成 `platform.d/<域>.config.yaml`。应用的 unit 用 `After=` 排在这个服务之后。
- **为什么只做一次。** 链接保存在 `/etc`,人手改过的链接不会在下次开机被覆盖,平时开机也不用再运行。代价:镜像被搬到另一台机器,或者某个包新增了别的域的候选文件时,不会自动重做。可以手动运行 `platform-detect` 重建链接;新增候选文件的包应在安装后脚本里运行它(详细设计里再定)。
- **库看到什么。** 两个层目录里普通的 `*.config.yaml`。库不解析候选文件的名字。合并规则一点没变:先平台层、后客户层,想去掉平台的内容只能靠 `reset`。
- **没有匹配。** 不建链接,平台层里没有这个域的文件,库按“该域没有配置”处理(加载所有兼容的 backend 并打 WARN)。`platform-detect` 在日志里写明没能匹配的 machine 和 OS。
- **OS 类型。** 现在定义 Ubuntu 和 QCLinux;DSUbuntu、DSLE 留给以后。类型来自 `/etc/os-release`,按顺序检查下面的规则:

| OS 类型 | 规则 |
|---|---|
| QCLinux | `NAME` 以 "Qualcomm Linux" 开头(不区分大小写);已见样本是 `ID=qcom-robotics-ros2-jazzy`、`NAME="Qualcomm Linux Robotics Reference Distro with ROS"` |
| Ubuntu | `ID=ubuntu` |
| 其他 | 未知:不建链接,打 WARN |

待定(见 plugin-discovery-design-note 的 `OPEN_QUESTIONS.md`):怎样区分 DSUbuntu 与 Ubuntu、DSLE 与 QCLinux,以后新增 OS 类型怎么命名,同一 machine 上不同的板子怎么区分(目前运行时做不到),目录最终名称,以及镜像搬机器或包新增候选文件后怎样刷新链接。
