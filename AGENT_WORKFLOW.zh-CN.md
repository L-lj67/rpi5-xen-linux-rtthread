# 推荐的 agent 部署与完整复现流程

本项目采用**用户在自己的电脑上交给 agent 从源码构建、部署和验收**的方式，不提供成品镜像。Agent 需要能运行本地终端、访问构建文件、下载依赖，并通过 SSH 访问树莓派；只有网页聊天能力的 agent 无法直接操作硬件。首次部署仍需要用户插拔 SD/USB、接线和上电。

最终目标是：树莓派 5（4 GiB）启动 Xen + Zephyr Dom0 + Linux DomD + 真实 RT-Thread Guest；Linux 可通过 SSH 管理，RTT 自动启动、分配 256 MiB/1 vCPU、固定 CPU 3，并与 Linux 双向通信。Linux 分配 2 GiB/3 vCPU、使用 CPU 0–2。摄像头、MIPI 屏幕、AI 应用和物理执行器接入不属于这个基础复现目标。

## 给 agent 的可复制任务说明

将下面内容交给有本地终端权限的 agent。开始前告诉它主机系统、树莓派内存、可使用的 SD/USB，以及是否允许覆盖这两块介质。公钥和设备身份由 agent 在本机检查，不需要把密码或私钥粘到聊天中。

```text
请在我的电脑上从源码部署 https://github.com/L-lj67/rpi5-xen-linux-rtthread 。
先读 AGENTS.md、AGENT_WORKFLOW.zh-CN.md、QUICKSTART.zh-CN.md、DEPLOYMENT.md
和 evidence/reproduction-checks.md。以仓库固定版本和实测边界为准。

请先检查主机、空间、工具、目标硬件和现有构建缓存，再持续执行构建、
集成和验收；遇到可修复的构建错误自行排查，保留下载及 sstate 缓存。
需要管理员权限、用户操作或覆盖介质时，说明具体操作和目标设备。
没有明确获得覆盖授权前，仅生成镜像文件，不写物理设备。

为我使用本机专用 SSH 密钥，只把公钥加入私有 local.conf。
配置直连网口和 SSH，使树莓派无需键盘登录；不要把私钥放入镜像或 Git。
按阶段保存命令、版本、产物路径、哈希、失败原因和下一条可执行命令。
不要把等待用户手动输入树莓派命令作为默认流程。

完成首次开机、双向 IPC、服务恢复和整机断电上电验收。
输出通过/失败/未测试清单和证据；不得把编译通过当作新镜像启动通过。
若只获准构建，完成镜像和文档后停止在写盘之前，并明确尚未验收的项目。
```

## 阶段 0：确认环境、权限和输出目录

推荐在 x86-64 Linux 上构建；本项目实际使用 Ubuntu 24.04 / WSL2。WSL2 属于本项目实测环境，不代表固定 Poky 版本官方验证了该宿主。Windows 上的 Yocto 工作目录放在 WSL 的 Linux 文件系统，例如 `~/rpi5-rtt-work`，不要放在 `/mnt/c`。

先记录 `uname -a`、`python3 --version`、`git --version`、CPU/RAM、`df -h`；WSL 用户另外在 Windows 检查 `Get-PSDrive C`。首次构建所需空间远大于最终镜像体积。原部署保留 40 GiB 主机空余作为暂停线；这是保护余量，不是“只需 40 GiB 就能从零构建”的承诺。Agent 应根据实际下载、缓存和并发监测空间，不足时暂停并保留缓存。

硬件需要 4 GiB 树莓派 5、合适的供电、SD、USB、读卡方式以及电脑与树莓派之间的网线。两块介质的真实容量分别必须大于最终对应 `.img` 文件；4 GiB rootfs 还含额外镜像开销。其他内存型号要重新审查 DT 内存银行和资源配置，不能直接当作已适配。

把日志和交接状态放在 `~/rpi5-rtt-work/session/`，与源码分开。记录本仓库 `git rev-parse HEAD`，后续复现固定在该提交；不要在构建中途自动升级源码。

## 阶段 1：安装主机工具、SDK 和依赖

Agent 应先读 [Yocto 5.0 主机要求](https://docs.yoctoproject.org/5.0/ref-manual/system-requirements.html) 和 [Moulin 安装说明](https://moulin.readthedocs.io/en/latest/about.html)。下面是 Ubuntu 环境的准备命令；软件源缺包时应检查该 Ubuntu 版本的对应包，不能静默跳过依赖。

```sh
sudo apt update
sudo apt install build-essential gawk wget git diffstat unzip texinfo chrpath socat cpio \
  python3 python3-pip python3-venv python3-pexpect python3-git python3-jinja2 \
  python3-subunit xz-utils debianutils iputils-ping zstd liblz4-tool file locales \
  libacl1 libegl1 libsdl2-dev mesa-common-dev cmake ninja-build gperf \
  device-tree-compiler mtools dosfstools gdisk bmap-tools curl openssh-client \
  libssl-dev libffi-dev libgit2-dev
sudo locale-gen en_US.UTF-8
export LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8
python3 -m venv ~/.venvs/rpi5-rtt
. ~/.venvs/rpi5-rtt/bin/activate
python -m pip install 'moulin==0.32' 'west==1.5.0'
mkdir -p ~/rpi5-rtt-work/session
```

从 [Zephyr SDK 0.16.8 官方发布页](https://github.com/zephyrproject-rtos/sdk-ng/releases/tag/v0.16.8) 下载与**主机**架构匹配的 Linux SDK，核对发布页提供的校验信息。x86-64 主机可以选 minimal 包，解压为 `~/zephyr-sdk-0.16.8` 后，仅安装所需 AArch64 工具链及主机工具：

```sh
cd ~/zephyr-sdk-0.16.8
./setup.sh -t aarch64-zephyr-elf -h -c
export ZEPHYR_SDK_INSTALL_DIR="$HOME/zephyr-sdk-0.16.8"
"$ZEPHYR_SDK_INSTALL_DIR/aarch64-zephyr-elf/bin/aarch64-zephyr-elf-gcc" --version
```

新终端或新 agent 会话中重新激活 venv、导出 SDK 和 locale。使用普通 Linux 用户构建，不要运行 `sudo ninja` 或 `sudo bitbake`。Python 要求 3.11 及以上；软件安装命令不构成已验证的完整宿主依赖锁。

## 阶段 2：固定源码、编译 RTT、暂存集成

执行 [QUICKSTART 第 1 节](QUICKSTART.zh-CN.md#1-准备工具和源码) 的 clone、固定提交、RTT 编译和 `apply_xen_troops.py --check` / 正式暂存命令。记录两个上游提交和 RTT 二进制 SHA-256。

通过标准：RTT 编译命令退出 0，`port/build-xen/rtthread-xen.bin` 存在；暂存脚本验证固定上游提交成功，并生成 access/support 配方和 pseudo 修复。暂存脚本不会获取全部上游依赖；Dom0 patch 必须在下一阶段 fetch 完成后执行。

## 阶段 3：获取依赖、配置免键盘 SSH、构建两个系统镜像

按 [QUICKSTART 第 2 节](QUICKSTART.zh-CN.md#2-获取依赖集成-dom0-和-linux-服务) 执行 Moulin 和 Ninja fetch 命令。fetch 完成后、Dom0 编译前，在已激活的同一个 venv 安装实际检出的 Zephyr 的构建依赖：

```sh
cd ~/rpi5-rtt-work
python -m pip install -r xen-troops/zephyr/zephyr/scripts/requirements-base.txt
python port/dom0-extension/apply_patch.py xen-troops/zephyr/zephyr-dom0-xt
```

若其他 Zephyr 工具报缺少模块，按该检出版本的 requirements 补齐并记录版本。检查 `git -C xen-troops/zephyr/zephyr-dom0-xt rev-parse HEAD`；本项目编译验证的 Dom0 为 `03b961479a6b56c99228aedbe206ba1ed6668b54`。

在**实际运行 SSH 客户端的一侧**创建或选用专用密钥：Windows 客户端可用 Windows OpenSSH 的密钥；WSL 客户端可用 WSL 的密钥。不要把两个环境的私钥路径混用。如果新建，可运行 `ssh-keygen -t ed25519 -f ~/.ssh/id_ed25519_rpi5_rtt`；交互和 passphrase 按用户偏好处理，用 ssh-agent 支持后续 BatchMode 验收。

将 `.pub` 的公钥内容按 QUICKSTART 写进私有 `xen-troops/yocto/build-domd/conf/local.conf`，同时设置静态网段和资源节约配置。不要覆盖原 local.conf 的 `require moulin.conf`。随后执行：

```sh
cd ~/rpi5-rtt-work/xen-troops
set -o pipefail
ninja full.img rootfs.img 2>&1 | tee ../session/build.log
```

通过标准：Ninja/BitBake 最终退出 0，而非仅 `tee` 成功；`full.img`、`rootfs.img` 和 Dom0 `zephyr.bin` 均存在。必要时检查 rootfs manifest 中包含 `rpi-xen-access`、`rtt-guest-support`，并在生成的 rootfs/tar 中检查 SSH 配置、公钥权限、networkd、RTT 服务启用和 AArch64 bridge。遇到固件文件未部署，按固定 Xen-Troops README 的 Known Issues 处理，不能只复制旧镜像掩盖失败。

## 阶段 4：组装和检查最终镜像

完整执行 [QUICKSTART 第 3 节](QUICKSTART.zh-CN.md#3-组装资源配置和-sd-镜像)。SD 最终使用 `full-rtt.img`，USB 使用**同次构建**的 `rootfs.img`；不要把原始 `full.img` 当作已经修改过内存配置的最终 SD 镜像。

```sh
cd ~/rpi5-rtt-work/xen-troops
sgdisk -v full-rtt.img
sgdisk -v rootfs.img
sha256sum full-rtt.img rootfs.img | tee ../session/image-sha256.txt
```

通过标准：组装脚本三个 FAT 文件读回 SHA-256 均匹配，两个镜像 GPT 检查无错误，记录大小、哈希和来源提交。DTBO/镜像输出必须是新路径；重试时选择新输出名或核实后处理上次输出，不要改为无条件覆盖。

到这里可以完成“只构建、不覆盖”的交付。之后的步骤必须有用户对目标测试介质的覆盖授权。

## 阶段 5：识别介质、写入、读回

先列出物理设备，比较插入前后的变化；随后核对型号、真实容量、唯一标识/序列号、分区和系统盘身份。Windows 可用 `Get-Disk` 和 `Get-Partition`；Linux 可用 `lsblk -o NAME,PATH,MODEL,SERIAL,SIZE,TRAN,FSTYPE,MOUNTPOINTS`。盘符和磁盘编号可能改变，不能照抄作者的 E:/F: 或磁盘 1/2。无法唯一识别目标时暂停在写入前，请用户配合插拔确认。

给用户展示 SD→`full-rtt.img`、USB→`rootfs.img` 的明确映射和将被覆盖的内容。在确认的物理目标上使用系统可用的镜像写入工具；卸载/锁定目标卷、申请所需管理员权限，等待写入和刷盘完成。写入对象是整个设备，不能把 GPT 整盘镜像当作文件复制进 FAT 分区，也不能把它写进某个分区。

对两块目标设备从第 0 字节读取与对应镜像**长度相同**的范围，计算 SHA-256 并与源镜像比较；介质尾部未被镜像覆盖的容量不参与比较。仅比较文件系统目录或看到“写入完成”不足以替代这项校验。保存结果后安全弹出，再请用户断电插回树莓派。此仓库不带跨平台自动写盘器；agent 应按实际主机实现并审查这一步，不应把未实现的工具描述为仓库功能。

## 阶段 6：直连网络、首次 SSH 和自动验收

树莓派插入两块介质，用网线连接电脑并上电。电脑选择**实际连接树莓派的网口**配置 `192.168.77.1/24`，不设默认网关；Pi 默认 `192.168.77.2/24`。其他上网网卡可以继续使用。若这个网段冲突，在构建前调整 local.conf，并同步修改电脑和验收命令。

Agent 在 Windows 用 `Get-NetAdapter` / `Get-NetIPAddress` 或网络设置配置对应网口；Linux 用 `ip -br link` / `ip -br addr` 识别接口，再通过本机网络管理器配置。不要替换整个默认路由来建立直连。随后按 [QUICKSTART 第 4 节](QUICKSTART.zh-CN.md#4-写入介质与免键盘验收) 建立首次主机信任并运行 `integration/verify_target.py`。这一步从电脑完成，不要求 Pi 本地登录。

新装系统可能生成新 SSH 主机密钥。若遇到与旧记录冲突，先核对这是刚写入、直接连接的目标设备，再更新该设备对应的信任记录或使用独立 known_hosts 文件；不要全局关闭主机密钥检查。

通过标准：验收退出 0、JSON `passed: true`，12 项检查全部通过，包括正确资源、服务启用、pin 操作成功、控制心跳和零 FP 错误。脚本只确认管理通道报告的 pin 操作结果，不是独立实时调度分析仪。首次连接失败先查链路/地址/供电，再查公钥路径、镜像是否配对和服务状态；不要未经诊断反复重写介质。

## 阶段 7：双向 IPC、服务恢复和整机重启

通过 SSH 在目标上执行以下命令，动态读取域 ID，不能写死为 2 或作者测试过的其他编号：

```sh
systemctl is-active rtt-guest.service
domid=$(cat /run/rtt-guest.domid)
/root/xen-tests/rtt-linux-bridge "$domid" --snapshot
/root/xen-tests/rtt-linux-bridge "$domid" --demo 5
```

demo 只用于当前的合成控制示例，检查 `input_seq` 更新、`control` 增长及返回状态变化，并保持 `fp_failures=0`、`deadline_misses=0`；接入实际执行器后不能未经评估运行测试目标。再执行 `systemctl restart rtt-guest.service`，重读域 ID，从电脑运行验收脚本保存为新的 `acceptance-service-restart.json`。

记录 `/proc/sys/kernel/random/boot_id`，通过 SSH 执行 `sync` 和 `systemctl poweroff`，让用户在 Linux 停机后切断整板供电，再重新上电。Linux 域内 `reboot` 不等同于重启 Xen 和整个板。重新连接后核对 boot ID 变化、启动时间及 RTT 服务自动恢复，**不要先手动启动服务再宣称自动启动通过**；再次保存 `acceptance-coldboot.json`。

如需复现历史压力测试，从电脑将公开脚本复制到目标后执行（下面使用 WSL/Linux SSH 客户端的密钥路径）：

```sh
cd ~/rpi5-rtt-work/port
scp -i ~/.ssh/id_ed25519_rpi5_rtt rtt-xen/validate_live.py root@192.168.77.2:/root/xen-tests/validate_live.py
ssh -i ~/.ssh/id_ed25519_rpi5_rtt root@192.168.77.2 'python3 /root/xen-tests/validate_live.py'
scp -i ~/.ssh/id_ed25519_rpi5_rtt root@192.168.77.2:/root/xen-tests/rtt-stress-summary.json ../session/
```

该脚本使用三个 Linux worker、合计 192 MiB 内存及 60 秒演示通信，校验控制计数、输入序号、FP 错误和截止期违约。完整原始样本在目标 `/root/xen-tests/rtt-stress-samples.jsonl`。可与 `evidence/rtt-stress-summary.json` 比较，但原作者的 78 µs 数值不是所有硬件/负载的承诺。首次启动、服务恢复、整板冷启动和压力测试分别报告。

## 完成标准与中断交接

完整复现报告应包含主机/板型号、全部源码提交、工具版本、构建结果、两份镜像哈希、写入映射与读回结果、首次/服务重启/整板冷启动的验收 JSON，以及 IPC 测试结果。报告可以保存在用户本机；对外发布前移除机器身份和凭据。

长任务中断前更新 `session/STATE.md`：当前阶段、已通过检查、最后退出码、日志尾部摘要、产物路径、剩余空间、下一条命令、介质是否已授权/已写入、是否正在等用户操作。新 agent 先核对文件、进程和硬件现状，再从断点继续；不能依赖旧聊天中的盘号或假设上一条命令成功。关闭 agent 窗口前确认构建是否仍在运行；需要后台运行时使用本机可持久运行的任务并保留日志，不假设所有 agent 都能自动续作。

本仓库已验证原部署的断电恢复，并验证公开源码组件、缓存辅助 rootfs 构建与镜像组装；尚未验证另一台全新主机上的完整构建和新生成介质冷启动。用户或 agent 在自己设备完成上述各阶段，才能将那次部署标为完整复现成功。具体证据见 [reproduction-checks.md](evidence/reproduction-checks.md)。
