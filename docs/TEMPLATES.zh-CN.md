# 配置模板

[English](TEMPLATES.md) | **中文**

给平台 tech owner 和客户部署用的两份可直接复制的模板。它们只回答**加载哪些 backend**;backend 在这台机器上能不能用,仍然由它自己的 `probe()` 判断。格式和合并规则见 [`LIBRARY.zh-CN.md`](LIBRARY.zh-CN.md) 第 4 节。

| 模板 | 给谁 | 安装到 | 目录 |
|---|---|---|---|
| 平台配置 | 平台的 tech owner:列出 Qualcomm 在该平台完全支持的 backend | 候选文件在 `<prefix>/share/plugin_discovery/platforms.d/`;`platform-detect` 把匹配的那个链接到 `/etc/plugin_discovery/platform.d/` | [`templates/platform-config/`](../templates/platform-config) |
| 客户配置 | 客户:加自己的 backend,或去掉某个 Qualcomm backend | `/etc/plugin_discovery/config.d/` | [`templates/customer-config/`](../templates/customer-config) |

填好的示例(复制后按需修改):[`templates/platform-config/examples/QCS8300-Ubuntu-camera.config.yaml`](../templates/platform-config/examples/QCS8300-Ubuntu-camera.config.yaml)(QCS8300 + Ubuntu:`qmmf`、`qcarcam`)和 [`templates/customer-config/examples/50-camera-v4l2.config.yaml`](../templates/customer-config/examples/50-camera-v4l2.config.yaml)(客户文件把 `v4l2` 放到最前面,合并后是 `v4l2, qmmf, qcarcam`,默认 backend 为 `v4l2`)。

## 文件名

库读取两个目录。平台层的文件先应用,客户层的文件后应用,与文件名无关;同一层内按文件名升序应用,最后一个说了算。

- 平台层,`/etc/plugin_discovery/platform.d/`:`<域>.config.yaml`,例如 `camera.config.yaml`。它们是 `platform-detect` 创建的符号链接,不需要手写。
- 客户层,`/etc/plugin_discovery/config.d/`:`NN-<域>-<来源>.config.yaml`。`NN` 是两位数字,不需要排序时用 `50`;`<域>` 是这个文件配置的域;`<来源>` 写客户或用途,例如 `50-camera-acme.config.yaml`。
- 平台候选文件,`<prefix>/share/plugin_discovery/platforms.d/`:`<machine>-<platform>-<域>.config.yaml`,例如 `QCS8275-Ubuntu-camera.config.yaml`。`<machine>` 是 `/sys/devices/soc0/machine` 里的字符串;`<platform>` 是 OS 类型,`Ubuntu` 或 `QCLinux`;比较时忽略大小写。没有 `NN`,因为这个组合是唯一的。库从不读这个目录。

其他名字的文件会被忽略(并给一条 WARN),清单(`*.plugin.yaml`)也不会被当成配置读。

## 平台配置

每个平台一个包。里面没有代码,构建也不需要 plugin_discovery 库。它为每个机型、OS 类型和域安装一个候选文件。首次启动时,`platform-detect` 可执行文件(随 plugin_discovery 安装,由 systemd 单元 `plugin-discovery-platform-detect.service` 运行一次)读取机型和 OS 类型,把每个匹配的候选文件以 `<域>.config.yaml` 为名链接到 `/etc/plugin_discovery/platform.d/`。没有匹配时不创建链接,该域保持未配置,并给一条 WARN。应用的 unit 应当用 `After=` 排在这个服务之后。

```
platform-config/
├── CMakeLists.txt                       把 config/*.config.yaml 安装到 share/plugin_discovery/platforms.d/
└── config/
    └── MACHINE-PLATFORM-camera.config.yaml   每个机型、OS 类型和域一个文件
```

```yaml
domains:
  camera:                              # 域名,即 PLUGIN_DISCOVERY_INTERFACE 里声明的名字
    backends: [<BACKEND_A>, <BACKEND_B>]   # 按此顺序加入
    # disable: [<BACKEND_C>]               # 可选
```

使用步骤:

1. 把 `templates/platform-config/` 复制成新的包目录,替换文件名、`CMakeLists.txt` 和 YAML 里所有的 `<MACHINE>`、`<PLATFORM>`、`<BACKEND_*>`。
2. `backends` 填该平台完全支持的 backend 在清单(`*.plugin.yaml`)里的 **`backend` 名**,不是库文件名。
3. 每个机型和 OS 类型下,每个域(video、audio、imu ……)加一个文件,不用的注释行删掉。
4. 像普通 CMake 包一样构建安装;单独打成一个 recipe 或 deb,按平台选装。统一的 Ubuntu 镜像里不带它。

Yocto recipe 示意:

```
SUMMARY = "plugin_discovery platform configuration for <MACHINE>"
LICENSE = "BSD-3-Clause-Clear"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/BSD-3-Clause-Clear;md5=<md5 of the BSD-3-Clause-Clear file in your layer>"
inherit cmake
SRC_URI = "file://<platform-config-dir>/"
S = "${UNPACKDIR}/<platform-config-dir>"
RDEPENDS:${PN} = "plugin-discovery <yaml 里列出的 backend 包>"
```

说明:

- 文件里写了但没装的 backend,会报告为跳过("configured but not installed");装了但没写的 backend 不加载(`not_listed`)。
- 版本和 ABI 不写在文件里,它们记录在每个 backend 的 `.so` 中,加载时检查。
- 某个文件解析失败时,在 stderr 给出提示并跳过,其他文件照常生效。
- `platform-detect --force` 重新建立链接(例如镜像换到另一台机器,或某个包新增了候选文件);`--dry-run` 只显示会链接什么,不做修改;`--candidates`、`--platform-dir`、`--stamp`、`--machine-file`、`--os-release` 可把它指向其他路径,用于测试。stamp 文件是 `/var/lib/plugin_discovery/platform-detect.done`。

## 客户配置

```yaml
domains:
  camera:
    # reset: true                  # 只用下面的 backend,不要平台的
    backends: [<MY_BACKEND>]       # 你的 backend 清单里的 `backend` 名
    disable: [<PLATFORM_BACKEND>]  # 可选
```

复制到 `/etc/plugin_discovery/config.d/`,例如命名为 `50-camera-acme.config.yaml`。该目录下的文件默认是客户来源,不需要写 `origin`(其他目录里的文件默认是 Qualcomm 来源;写了 `origin:` 则以它为准)。客户文件在平台文件之后应用,与文件名无关,最后一个说了算;文件里列出的 backend 放到结果最前面，第一项就是默认 backend。

| 想做的事 | 怎么写 |
|---|---|
| 加一个 backend | `backends: [my]` |
| 去掉一个 Qualcomm backend | `disable: [a]` |
| 启用平台禁用的 backend | `backends: [c]` |
| 用自己编译的版本替换某个 Qualcomm backend | 清单里写同样的 `backend` 名 |
| 只用自己的 backend | `reset: true` 加自己的 `backends` |
| 去掉平台为某个域列出的全部内容 | `reset: true` 加自己的 `backends` |

每个文件都会参与:客户文件不能替换或隐藏平台文件,只能通过 `reset` 和 `disable` 覆盖。`pluginlib-detect` 把每个文件显示为 `applied` 或 `invalid`。

示例,平台配置列出 `[a, b]` 并禁用 `c`:

| 客户文件 | 加载的 backend |
|---|---|
| 无 | a、b |
| `backends: [my]` | my、a、b |
| `backends: [my]`、`disable: [a]` | my、b |
| `backends: [c]` | c、a、b |
| `reset: true`、`backends: [my]` | my |
| 人工指定 `PLUGIN_DISCOVERY_BACKEND_CAMERA=a` | a(整体替换所有层,包括客户文件) |

列出的 backend 是否真的服务这台机器,由它的 `probe()` 决定;不支持的会被跳过并报告。

## 试用模板

```bash
PLUGIN_DISCOVERY_CONFIG_PATH=<平台目录>:<客户目录> <你的应用>
```

`PLUGIN_DISCOVERY_CONFIG_PATH`(用 `:` 分隔)会替换两个默认目录;后面的目录优先于前面的,所以平台目录放前面。目录不在 `/etc/` 下时,客户文件里要加 `origin: customer`。
