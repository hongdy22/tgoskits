# 阶段 A 报告：基线环境和可观测性

日期：2026-05-09
分支：`exp4_codex`
提交：`504fd3755`

## 结论

阶段 A 已完成。当前 `x86_64` StarryOS 基线环境可以正常启动，能运行相关 QEMU 测试用例，可以使用 Alpine rootfs，DNS 可解析，`curl` 可以完成 HTTPS 请求，并且在尝试 Codex 二进制之前所需的基础文件系统、设备节点和环境变量能力已经验证过。

## 本阶段文件变化

本阶段没有修改仓库中的 tracked 源码文件；`git status --short` 为空。

本阶段新增或准备的内容都在 `target/codex/` 下，属于本地实验产物：

```text
target/codex/assets/
target/codex/overlay/
target/codex/rootfs/rootfs-x86_64-codex.img
target/codex/logs/
target/codex/qemu/
target/codex/notes/stage_a_report.md
target/codex/qemu-build-x86_64/qemu-system-x86_64
target/codex/qemu-build-x86_64-user/qemu-x86_64
```

另外，为了验证 `/dev/zero`、`/dev/urandom` 等阶段 A 前置条件，我临时新增过：

```text
test-suit/starryos/normal/qemu-smp1/codex-stage-a-prereq/qemu-x86_64.toml
```

该临时测试用例跑完后已删除，没有留下 tracked 改动。

## 使用的宿主机环境

宿主机当前没有直接暴露系统级 `qemu-system-x86_64` / `qemu-x86_64`，所以本阶段在 `target/codex/` 下构建了本地 QEMU 7.0.0：

```bash
target/codex/qemu-build-x86_64/qemu-system-x86_64
target/codex/qemu-build-x86_64-user/qemu-x86_64
```

宿主机运行 `cargo xtask` 还需要使用本地 `libudev.pc` shim：

```bash
export PKG_CONFIG_PATH=/home/threetu33/os_biglabB_task2/tgoskits/target/local-pkgconfig:/home/threetu33/os_biglabB_task2/tgoskits/target/host-libs/pkgconfig
export PATH=/home/threetu33/os_biglabB_task2/tgoskits/target/codex/qemu-build-x86_64-user:/home/threetu33/os_biglabB_task2/tgoskits/target/codex/qemu-build-x86_64:$PATH
```

## 准备好的目录和镜像

```text
target/codex/assets/
target/codex/overlay/
target/codex/rootfs/rootfs-x86_64-codex.img
target/codex/logs/
target/codex/qemu/
target/codex/notes/
```

`target/codex/rootfs/rootfs-x86_64-codex.img` 是托管版 x86_64 Alpine rootfs 的副本，后续阶段 B 会把 Codex 专用 overlay 注入到这个镜像里，避免污染默认 rootfs。

## 已运行命令

```bash
cargo xtask starry test qemu -l
cargo xtask starry rootfs --arch x86_64
cargo xtask starry test qemu --arch x86_64 -c smoke
cargo xtask starry test qemu --arch x86_64 -c busybox
cargo xtask starry test qemu --arch x86_64 -c grep
cargo xtask starry test qemu --arch x86_64 -c apk-curl
cargo xtask starry test qemu --arch x86_64 -c test-pipe-syscalls
cargo xtask starry test qemu --arch x86_64 -c syscall
```

以上命令均在前面列出的环境变量下成功完成。

另外还跑过一个临时本地 smoke case，验证完成后已删除。它检查了：

```text
HOME=/root
PATH 传递
/tmp 可读写
/root 可读写
/dev/null
/dev/zero
/dev/urandom
/proc/self/exe 可通过 readlink 读取
/etc/resolv.conf
CA 证书路径
```

该临时测试输出了 `STARRY_CODEX_STAGE_A_PREREQ_PASSED`。

## 覆盖范围说明

- `smoke`：验证 StarryOS 能启动到 shell，并能执行基本 stdout 命令。
- `busybox`：覆盖 shell 工具、env、`/tmp`、`/proc/self/exe`、`/dev/null` 和基础文件操作。
- `grep`：覆盖宿主机构建的 x86_64 用户程序注入，以及 grep/sed/awk 风格的文件处理。
- `apk-curl`：覆盖 `apk update`、包安装、DNS、TCP、TLS、CA，以及 HTTPS 链路中依赖的系统时间和随机数。
- `test-pipe-syscalls`：覆盖 pipe、fd 和进程 I/O 基础能力。
- `syscall`：覆盖 `getrandom`、`clock_gettime`、`mmap`、`epoll`、signal/timer/stat 等 syscall 家族，其中也包含 `stat /dev/null`。

## 已观察到但不阻塞阶段 A 的问题

- 部分 rootfs staging 过程中，`debugfs rdump` 会打印 ownership-change 相关 warning。受影响用例仍然通过，因此本阶段不把它视为阻塞项。
- `busybox` 大覆盖测试中出现过一些兼容性 warning，例如 `adjtimex`、`semctl`、`personality`、`mknod`、`readahead`、`setpriority`。这些没有阻塞阶段 A，但如果后续 Codex 运行时第一条 fatal error 命中其中之一，需要优先回头处理。

## 进入阶段 B 的建议

现在可以从这个状态进入阶段 B。下一步建议按以下顺序推进：

1. 在普通 Linux 上采集 Codex 的 `strace` 日志，包括 `codex --version`、`codex --help` 和 `codex exec --help`。
2. 把 Codex native x86_64 musl 二进制和 `rg` 放进 `target/codex/overlay/`。
3. 把 overlay 注入到 `target/codex/rootfs/rootfs-x86_64-codex.img`。
4. 使用 Codex rootfs 启动 StarryOS，先运行离线 Codex smoke 命令，再考虑任何在线 API 请求。
