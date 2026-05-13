# 阶段 B 报告：Codex 二进制注入和离线 smoke

日期：2026-05-09
分支：`exp4_codex`
提交：`504fd3755`

## 结论

阶段 B 已完成。Codex native x86_64 musl 二进制已经注入到 StarryOS 专用 rootfs，并且在 StarryOS guest 内完成了离线 smoke：

```text
codex --version
codex --help
codex exec --help
codex login status
rg --version
```

最终成功标记：

```text
STARRY_CODEX_STAGE_B_OFFLINE_PASSED
```

## 本阶段文件变化

本阶段没有修改仓库中的 tracked 源码文件；`git status --short` 为空。

本阶段新增或修改的内容都在 `target/codex/` 下，属于本地实验产物：

```text
target/codex/assets/codex
target/codex/assets/rg
target/codex/overlay/usr/local/bin/codex
target/codex/overlay/usr/local/bin/rg
target/codex/overlay/root/.codex/auth.json
target/codex/overlay/root/.codex/config.toml
target/codex/overlay/root/workspace/hello/README.md
target/codex/rootfs/rootfs-x86_64-codex.img
target/codex/qemu/qemu-x86_64-codex-stage-b-offline.toml
target/codex/notes/inject_stage_b_overlay.debugfs
target/codex/notes/stage_b_report.md
target/codex/logs/linux-codex-version.out
target/codex/logs/linux-codex-help.out
target/codex/logs/linux-codex-exec-help.out
target/codex/logs/qemu-user-codex-version.strace.log
target/codex/logs/qemu-user-codex-help.strace.log
target/codex/logs/qemu-user-codex-exec-help.strace.log
target/codex/logs/starry-codex-stage-b-offline.log
```

`target/codex/overlay/root/.codex/auth.json` 是从 `target/auth.json` 复制而来，仅用于本地测试，不应提交或移动到 tracked 路径。

## 使用的 Codex 和 rg

Codex 来源：本机 npm 安装的 `@openai/codex` native binary。

```text
codex-cli 0.115.0
codex: ELF 64-bit LSB pie executable, x86-64, static-pie linked, stripped
sha256: 440269f35afeb90d38115af844629d98705fb7266fdcd5fe7c040a78ebc75b85
```

`rg` 来源：同一个 Codex npm 平台包内的 native `rg`。

```text
ripgrep 15.1.0
rg: ELF 64-bit LSB pie executable, x86-64, static-pie linked, stripped
sha256: ebeaf56f8a25e102e9419933423738b3a2a613a444fd749d695e15eba53f71f2
```

## Linux 侧基线

宿主机没有安装 `strace`，所以没有生成真正的 Linux `strace -ff` 日志。作为替代，本阶段完成了两类基线记录：

```text
target/codex/logs/linux-codex-version.out
target/codex/logs/linux-codex-help.out
target/codex/logs/linux-codex-exec-help.out
```

以及 QEMU user `-strace` 形式的 syscall 采样：

```text
target/codex/logs/qemu-user-codex-version.strace.log
target/codex/logs/qemu-user-codex-help.strace.log
target/codex/logs/qemu-user-codex-exec-help.strace.log
```

这三条 QEMU user `-strace` 命令返回码均为 0。

## Rootfs 注入

注入目标：

```text
target/codex/rootfs/rootfs-x86_64-codex.img
```

注入脚本：

```text
target/codex/notes/inject_stage_b_overlay.debugfs
```

注入后的关键 guest 路径：

```text
/usr/local/bin/codex
/usr/local/bin/rg
/root/.codex/auth.json
/root/.codex/config.toml
/root/workspace/hello/README.md
```

`debugfs stat` 已确认 `/usr/local/bin/codex`、`/usr/local/bin/rg`、`/root/.codex/auth.json`、`/root/.codex/config.toml` 和 workspace README 均存在于 rootfs 中，其中 `auth.json` 在镜像内 mode 为 `0600`。

## StarryOS 离线 smoke

本次使用的 QEMU 由用户确认可信的本地 QEMU 7.0.0 源码构建，作为当前阶段的可信宿主机实验工具；它不属于 guest rootfs 或 StarryOS 运行时内容。

运行命令：

```bash
PATH=/home/threetu33/os_biglabB_task2/tgoskits/target/codex/qemu-build-x86_64-user:/home/threetu33/os_biglabB_task2/tgoskits/target/codex/qemu-build-x86_64:$PATH \
PKG_CONFIG_PATH=/home/threetu33/os_biglabB_task2/tgoskits/target/local-pkgconfig:/home/threetu33/os_biglabB_task2/tgoskits/target/host-libs/pkgconfig \
cargo xtask starry qemu \
  --target x86_64-unknown-none \
  --qemu-config target/codex/qemu/qemu-x86_64-codex-stage-b-offline.toml \
  --rootfs target/codex/rootfs/rootfs-x86_64-codex.img
```

guest 内环境：

```sh
export HOME=/root
export USER=root
export SHELL=/bin/sh
export TERM=xterm-256color
export PATH=/usr/local/bin:/usr/bin:/bin
export CODEX_HOME=/root/.codex
cd /root/workspace/hello
```

验证结果：

```text
codex --version        -> codex-cli 0.115.0
codex --help           -> 成功打印命令列表
codex exec --help      -> 成功打印 exec 帮助
codex login status     -> Logged in using ChatGPT, CODEX_LOGIN_STATUS_RC=0
rg --version           -> ripgrep 15.1.0
```

完整 QEMU 输出保存在：

```text
target/codex/logs/starry-codex-stage-b-offline.log
```

## 已观察到但不阻塞阶段 B 的问题

- 每次启动 Codex 时都会出现多条 `sys_prctl: unsupported option 23` warning，但 Codex 命令均正常退出，阶段 B 不把它视为阻塞项。
- `rg --version` 打印了 jemalloc 关于 `MADV_DONTNEED` 的提示，并明确说明这是 QEMU 下的预期行为；命令正常退出。
- 日志中没有 fatal page fault、kernel panic、segmentation fault 或导致 smoke 失败的 unimplemented syscall。

## 进入阶段 C 的建议

现在可以进入阶段 C：认证、TLS、HTTPS 和最小在线 Codex。

建议先在 guest 内不用 Codex 直接确认：

```sh
cat /etc/resolv.conf
ls /etc/ssl/certs/ca-certificates.crt
curl -Iv https://api.openai.com
```

然后再运行最小在线 Codex：

```sh
codex exec --sandbox danger-full-access --skip-git-repo-check \
  'Reply with exactly: STARRY_CODEX_ONLINE_OK'
```

如果阶段 C 失败，优先根据 `target/codex/logs/starry-codex-stage-b-offline.log` 已知的正常启动路径，对比第一条新增 fatal error。
