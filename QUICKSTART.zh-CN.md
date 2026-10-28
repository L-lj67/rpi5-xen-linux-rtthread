# 中文部署入口

**推荐先读 [Agent 部署工作流](AGENT_WORKFLOW.zh-CN.md)**：包含可直接复制给 agent 的任务说明、主机工具/SDK 安装、分阶段通过标准、写盘操作交接和断点恢复。本页提供精简构建命令；完整复现应继续完成工作流中的 IPC、服务恢复与整机冷启动验收。

适用目标：4 GiB 树莓派 5，SD 卡启动 Xen，USB 存放 Linux 根文件系统。Linux 负责设备、应用和 AI；RT-Thread 分配 256 MiB、独占 CPU 3 的硬亲和性，运行周期控制与共享内存通信。目前控制对象为仿真对象，真实摄像头、屏幕和执行器需要后续接入。

首次构建需要 Linux 构建环境、较多磁盘空间和下载时间。Windows 用户使用 WSL2，Yocto 工作目录放在 Linux 文件系统内（例如 `~/rpi5-rtt-work`），保留下载和 sstate 缓存。WSL 中看到的虚拟磁盘剩余量不能代替 Windows 系统盘实际剩余量。

## 1. 准备工具和源码

按 [Xen-Troops 上游要求](https://github.com/xen-troops/meta-xt-prod-devel-rpi5/tree/008a70e2c6de0f3aeb872497ccea8c0c181e8198) 安装 Yocto、Zephyr 和 Moulin 的主机依赖。这里验证过 Moulin 0.32、Zephyr SDK 0.16.8、Python 3.12；脚本要求 Python 3.11 及以上，还需要 `git`、`ninja`、`west`、`mcopy`、`fdtget` 和 `fdtput`。

```sh
mkdir -p ~/rpi5-rtt-work
cd ~/rpi5-rtt-work
git clone https://github.com/L-lj67/rpi5-xen-linux-rtthread port
git clone https://github.com/xen-troops/meta-xt-prod-devel-rpi5 xen-troops
git -C xen-troops checkout 008a70e2c6de0f3aeb872497ccea8c0c181e8198
git clone --filter=blob:none --sparse https://github.com/RT-Thread/rt-thread rt-thread
git -C rt-thread sparse-checkout set include src components/libc libcpu/aarch64 bsp/raspberry-pi/raspi5
git -C rt-thread checkout e6905134d511900f5c3f0e880a014e2c5903ac87
```

在当前 shell 设置 SDK 路径，以下示例假定 SDK 位于 `~/zephyr-sdk-0.16.8`：

```sh
export ZEPHYR_SDK_INSTALL_DIR="$HOME/zephyr-sdk-0.16.8"
cd ~/rpi5-rtt-work/port
python3 rtt-xen/build.py --rtthread ../rt-thread \
  --toolchain-prefix "$ZEPHYR_SDK_INSTALL_DIR/aarch64-zephyr-elf/bin/aarch64-zephyr-elf-"
python3 integration/apply_xen_troops.py ../xen-troops build-xen/rtthread-xen.bin --check
python3 integration/apply_xen_troops.py ../xen-troops build-xen/rtthread-xen.bin
```

## 2. 获取依赖，集成 Dom0 和 Linux 服务

```sh
cd ~/rpi5-rtt-work/xen-troops
moulin rpi5.yaml --DOMD_ROOT usb --ENABLE_HDMI yes
ninja fetch-dom0 fetch-domd yocto/build-domd/conf/moulin.conf
cd ../port
python3 -m pip install -r ../xen-troops/zephyr/zephyr/scripts/requirements-base.txt
python3 dom0-extension/apply_patch.py ../xen-troops/zephyr/zephyr-dom0-xt
```

在 `../xen-troops/yocto/build-domd/conf/local.conf` 中加入自己的 SSH **公钥**，保留在本机，不提交到 Git。没有密钥时，在电脑上用 `ssh-keygen -t ed25519` 创建；`.pub` 文件内容是公钥，另一份是私钥，私钥只留在电脑。

```conf
RPI_XEN_SSH_PUBKEY = "ssh-ed25519 替换为自己的公钥编码"
RPI_XEN_STATIC_ADDR = "192.168.77.2/24"
BB_NUMBER_THREADS = "4"
PARALLEL_MAKE = "-j4"
INHERIT += "rm_work"
```

```sh
cd ~/rpi5-rtt-work/xen-troops
ninja full.img rootfs.img
```

若构建失败，修复具体报错后重跑同一命令。集成脚本已加入本项目使用的 pseudo 1.9.11 修复，不必删除现有下载或缓存。空间不足先暂停构建，不要随意清空构建目录。

## 3. 组装资源配置和 SD 镜像

```sh
cd ~/rpi5-rtt-work/port
python3 integration/configure_domd_dtbo.py \
  ../xen-troops/yocto/build-domd/tmp/deploy/images/raspberrypi5/bcm2712-raspberrypi5-xen.dtbo \
  ../xen-troops/domd-rtt.dtbo
python3 integration/assemble_full_image.py ../xen-troops/full.img ../xen-troops/full-rtt.img \
  --zephyr ../xen-troops/zephyr/build-dom0/zephyr/zephyr.bin \
  --dtbo ../xen-troops/domd-rtt.dtbo --rtt build-xen/rtthread-xen.bin
```

输出为新的 `full-rtt.img`，每份写入的 FAT 文件都会读回核对 SHA-256。与它配套的 USB 镜像为同次构建的 `rootfs.img`。输出已存在时选择新文件名；脚本不会覆盖旧输出，也不会写物理磁盘。

## 4. 写入介质与免键盘验收

用镜像写入工具将 `full-rtt.img` 写入 SD 卡、`rootfs.img` 写入 USB。写入前按型号、容量、序列号和分区核实两块物理设备，排除系统盘；写后读回校验。4 GiB 根文件系统镜像还包含分区表等开销，USB 实际容量必须大于整个 `rootfs.img` 文件。

SD 和 USB 插到树莓派，网线直连电脑。电脑该网口设置为 `192.168.77.1/24`，无需网关；树莓派将使用 `192.168.77.2/24`。电脑可以继续通过其他网卡上网。SSH 使用第 2 步对应的私钥，树莓派无需本地键盘登录。

先在这条已确认的直连链路上建立 SSH 主机信任，再运行验收：

```sh
ssh -o StrictHostKeyChecking=accept-new -i /path/to/private-key root@192.168.77.2 true
python3 integration/verify_target.py --ssh root@192.168.77.2 \
  --identity /path/to/private-key --output acceptance.json
```

`passed: true` 表示当次在线检查通过，退出码为 0。完整复现验收还需整机断电再上电后重复检查；Linux 域内 `reboot` 不等于整机重启。新生成介质的冷启动尚未在本项目本轮验证中执行。

交给 agent 时，让它先读 [AGENTS.md](AGENTS.md)，按 [DEPLOYMENT.md](DEPLOYMENT.md) 执行，并参考[实际验证记录](evidence/reproduction-checks.md)。报告须区分已通过、失败和未测试，不能把当前在线系统通过当作新镜像已经启动通过。
