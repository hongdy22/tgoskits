# 在 TGOSKits / StarryOS 上跑起 OpenAI Codex CLI 的分阶段开发计划

> 调研日期：2026-05-09  
> 当前工作分支：`exp4_codex`  
> codex 源码在 `tgoskits/target/openai_codex_inspect`  
> 当前 TGOSKits 基线：`504fd3755 Fix(sys fadvise64): reject invalid/closed fd with ebadf, negative len with einval (#444)`  
> 目标：先让 Codex CLI 在 `https://github.com/rcore-os/tgoskits` 当前 StarryOS 环境里可靠跑起来；暂不追求多个 Codex 会话并发、完整沙箱隔离或完整 TUI 体验。

## 0. 结论先行

最稳的第一条路线是：

1. 选择 StarryOS `x86_64` QEMU 路径作为首个目标。
2. 不在 guest 里跑 npm/Node 包装器，直接使用 OpenAI Codex 官方发布的 Linux musl 静态二进制。
3. 先跑 `codex --version`、`codex --help`、`codex exec --help`、`codex login status` 这类无网络或低风险命令。
4. 然后用用户提供的 `target/auth.json` 或 API key 跑 `codex exec` 的最小在线请求。
5. 再让 Codex 在 guest 内的小 workspace 里先读写文件，随后再验证 `rg`、`sed`、`git diff` 和简单 shell 命令。
6. 最后再把目标工作区扩大到 `tgoskits` 源码，并补测试套件、syscall 缺口和文档。

这样做的理由：

- OpenAI 官方仓库说明 Codex CLI 是本地终端 coding agent，Linux 发布包包含 `x86_64-unknown-linux-musl` 和 `aarch64-unknown-linux-musl` 二进制；musl 静态二进制最适合先绕开动态链接器和 npm/Node 依赖问题。
- Codex 官方安装文档写明系统要求包含 Linux 桌面发行版、Git 2.23+、至少 4 GB RAM。StarryOS 现有 QEMU 默认 `x86_64` 配置只有 `512M`，所以 Codex case 需要单独加大内存。
- StarryOS 当前已经有 ELF loader、`/proc/self/exe`、procfs、devfs、tmpfs、ext rootfs、TCP/UDP/Unix socket、epoll/poll/select、futex、clone/exec/wait、TTY/PTY 和 apk/curl 测试。它不像“从零开始跑 Linux 程序”，更像“把少数真实应用缺口逐个补齐”。
- 当前明显风险是：`inotify` 还未实现、Codex 内置 Linux sandbox/landlock 不应作为第一阶段目标、TUI 对终端 ioctl/raw mode 要求更高、以及如何把大仓库和 Codex 资产稳定放入 rootfs。

### 0.1 最小闭环定义

阶段一不要把目标写成“完整支持 Codex”。本项目的第一性目标是一个单次非交互闭环：

```sh
codex --version
codex --help
CODEX_HOME=/root/.codex codex exec --sandbox danger-full-access --skip-git-repo-check "Reply with exactly: STARRY_CODEX_ONLINE_OK"
```

如果当前 Codex 版本或账号策略要求额外参数，可用等价的保守写法：

```sh
codex exec \
  --skip-git-repo-check \
  --dangerously-bypass-approvals-and-sandbox \
  -C /root \
  'Reply with exactly: STARRY_CODEX_ONLINE_OK'
```

这里刻意先不做 TUI、不做默认 sandbox、不做多 session、不做 MCP、不跑大仓库扫描，也不让 Codex 一上来执行复杂 `git`、`cargo`、`python` 工作流。只要能启动、读取配置、通过 HTTPS 拿到一次模型回复、在小 workspace 里读写文件，就算第一阶段成功。

### 0.2 本地实验工作区

`/home/threetu33/os_biglabB_task2/tgoskits/target/codex/` 是本轮 Codex-on-StarryOS 的本地实验工作区，用来集中保存不会提交到仓库的资产、镜像、QEMU 配置、日志和阶段报告。它的作用是把 Codex 专用 rootfs、认证缓存、二进制注入和运行记录同默认 `target/rootfs`、正式测试套件和 tracked 源码隔离开，方便反复实验和复现阶段 A 到 I 的结果。

当前主要子目录约定：

```text
target/codex/assets/   # host 侧准备好的 codex、rg 等 native 二进制
target/codex/overlay/  # 注入 guest rootfs 的临时 overlay，包括 /root/.codex 和小 workspace
target/codex/rootfs/   # Codex 专用 rootfs 副本，例如 rootfs-x86_64-codex.img
target/codex/qemu/     # Codex 阶段实验用 QEMU 配置
target/codex/logs/     # StarryOS/QEMU/Codex 运行日志和对照输出
target/codex/notes/    # 阶段报告、注入脚本和排障记录等
```

其中 `target/codex/overlay/root/.codex/auth.json` 和 `target/auth.json` 都应按敏感凭据处理，只用于本地实验，不提交、不贴日志。

## 1. 外部 Codex 事实画像

### 1.1 官方发布与运行方式

从 `openai/codex` 官方仓库和文档可确认：

- Codex CLI 可以通过 `npm install -g @openai/codex`、Homebrew 或 GitHub Release 二进制安装。
- Linux Release 直接提供：
  - `codex-x86_64-unknown-linux-musl.tar.gz`
  - `codex-aarch64-unknown-linux-musl.tar.gz`
- Release archive 里实际是单个平台名二进制，解压后通常改名为 `codex`。
- 当前调研时官方 GitHub latest release 为 `0.130.0`，发布时间为 2026-05-08。
- 本机当前 npm 安装的 `codex` 是 `codex-cli 0.115.0`，比官方 latest 旧；后续实验应明确记录使用哪个版本。

官方资料：

- https://github.com/openai/codex
- https://github.com/openai/codex/blob/main/docs/install.md
- https://github.com/openai/codex/releases/latest
- https://developers.openai.com/codex/auth

用户给的 ChatGPT 分享链接当前只能读取到标题“Codex运行在StarryOS可行性”和登录提示，无法读取完整正文，所以本文档不依赖其中未公开内容作结论。

### 1.2 Codex CLI 源码依赖轮廓

本次浅克隆 `openai/codex` main 后观察到：

- 主 CLI 位于 `codex-rs/cli`，二进制名为 `codex`。
- 交互 TUI 位于 `codex-rs/tui`。
- 非交互模式位于 `codex-rs/exec`，命令形态为 `codex exec ...`。
- 重要依赖包括：
  - `tokio`：进程、信号、multi-thread runtime、stdin/stdout。
  - `reqwest`、`rustls`、`tokio-tungstenite`：HTTPS/WebSocket。
  - `crossterm`、`ratatui`：交互终端。
  - `portable-pty`：PTY 相关能力。
  - `notify`：文件监听，Linux 上可能触发 inotify。
  - `gix`、外部 `git` 命令、`rg`：仓库识别、搜索与 patch/worktree 操作。
  - `sqlx` + sqlite feature：本地状态和日志数据库。
  - `keyring`：凭据存储；第一阶段应强制走 file 存储。

### 1.3 首阶段应避免的 Codex 功能

这些功能不是不能做，而是不应该作为“先跑起来”的门槛：

- npm/Node 包装器：会额外要求 Node runtime、npm 包布局和 optional package 解析。
- 浏览器登录：需要本地 browser/open callback，对 headless QEMU 不友好。
- Codex Linux sandbox：可能涉及 landlock、seccomp、bpf、namespace、权限模型；StarryOS 自身已经是隔离环境，先用 Codex 的 bypass/danger-full-access 路径。
- TUI：终端 raw mode、alternate screen、键盘增强探测、窗口大小变化都比 `codex exec` 更敏感。
- 多 Codex 会话并发：先验证单会话，避免把文件锁、sqlite 并发、网络连接池和进程调度问题混在一起。

## 2. 当前 TGOSKits / StarryOS 已有基础

### 2.1 相关目录

| 路径 | 当前作用 | 与 Codex 的关系 |
| --- | --- | --- |
| `os/StarryOS/kernel/` | StarryOS 内核与 Linux syscall 兼容层 | 主要补 syscall、进程、文件、网络、TTY |
| `os/StarryOS/starryos/` | StarryOS 启动包，默认启动 `/bin/sh -c init.sh` | 后续可加 Codex 专用 init 或 example |
| `components/starry-process` | PID、进程组、session | Codex spawn 子进程、job control 会依赖 |
| `components/starry-signal` | 信号与 signal trampoline | Ctrl-C、子进程、tokio signal 会依赖 |
| `components/starry-vm` | 用户内存访问辅助 | syscall 参数读写会依赖 |
| `components/axpoll` | poll/epoll 基础 | tokio/net/pipe/eventfd 会依赖 |
| `components/axfs-ng-vfs`、`components/rsext4` | VFS 与 ext rootfs | Codex 配置、日志、workspace 文件读写会依赖 |
| `scripts/axbuild/src/starry/*` | rootfs、QEMU、测试套件编排 | Codex 资产注入和正式 case 最终落点 |
| `test-suit/starryos/` | StarryOS QEMU/board 测试 | 后续加 `codex-smoke`、`codex-help`、`codex-offline` |

### 2.2 StarryOS 当前已经具备的关键能力

#### 用户程序加载

- 支持 ELF 装载。
- 支持动态解释器 `PT_INTERP`。
- 支持静态 PIE/musl 二进制应走的普通 ELF 装载路径。
- 支持 shebang 脚本重定向到解释器。
- `execve` 会重建地址空间、关闭 CLOEXEC fd、更新 `/proc/self/exe` 和 cmdline。

#### 地址空间和内存

- 支持 `brk`，默认最大 heap 扩展约 512 MB。
- 支持 `mmap`：
  - anonymous/private/shared
  - file-backed private/shared
  - `MAP_FIXED`
  - `MAP_FIXED_NOREPLACE`
  - `MAP_POPULATE`
  - `mprotect`、`munmap`、`mremap`、`madvise`、`msync`、`mlock`
- 这对 Rust/musl 程序的 allocator、sqlite mmap、ELF 映射是正向基础。

#### 文件系统

已有 syscall 包括：

- `openat`、`close`、`close_range`、`dup`、`dup3`、`fcntl`
- `read`、`write`、`readv`、`writev`
- `pread64`、`pwrite64`、`preadv`、`pwritev`
- `lseek`、`truncate`、`ftruncate`
- `fallocate`、`fadvise64`
- `fsync`、`fdatasync`、`sync_file_range`
- `getdents64`
- `mkdirat`、`linkat`、`unlinkat`、`renameat2`
- `stat`/`fstat`/`fstatat`/`statx`/`statfs`
- `pipe2`、`eventfd2`、`timerfd_*`、`signalfd4`
- `memfd_create`

已存在 pseudo fs：

- `/proc`
  - `/proc/self`
  - `/proc/[pid]/exe`
  - `/proc/[pid]/fd`
  - `/proc/[pid]/maps`
  - `/proc/meminfo`
  - `/proc/cpuinfo`
  - `/proc/stat`
  - `/proc/sys/kernel/pid_max`
- `/dev`
  - TTY/PTY 相关节点
  - random/urandom 来自 devfs
- `/tmp` tmpfs。

#### 进程、线程、信号和同步

已有能力：

- `clone`、`clone3`、`fork`/`vfork` x86_64 路径。
- `execve`、`exit`、`exit_group`、`wait4`。
- `set_tid_address`、`set_robust_list`、`get_robust_list`。
- `futex` 支持 wait/wake/wait_bitset/wake_bitset/requeue/cmp_requeue。
- `membarrier` 有可用 stub。
- `rseq` 注册当前返回 `ENOSYS`，这是很多 libc 可接受的 fallback，但需在 Codex 实测中确认。
- `rt_sigaction`、`rt_sigprocmask`、`kill`、`tgkill`、`sigaltstack` 等信号路径已存在。

#### 网络

已有能力：

- `AF_INET` TCP、UDP。
- `AF_UNIX` stream、dgram、socketpair。
- `AF_NETLINK` raw uevent。
- raw ICMP。
- `connect`、`bind`、`listen`、`accept4`、`send*`、`recv*`、`getsockopt`、`setsockopt`。
- 测试套件已经存在 `apk-curl`、`apt`、`dhcp` 这类需要真实网络和 DNS 的 case。

#### I/O 多路复用与 TTY

已有能力：

- `poll`、`ppoll`。
- `select`、`pselect6`。
- `epoll_create1`、`epoll_ctl`、`epoll_pwait`、`epoll_pwait2`。
- `/dev/ptmx`、`/dev/pts`、PTY 分配。
- TTY ioctl 已支持：
  - `TCGETS`
  - `TCSETS` / `TCSETSF` / `TCSETSW`
  - `TCGETS2`
  - `TCSETS2` / `TCSETSF2` / `TCSETSW2`
  - `TIOCGPGRP`
  - `TIOCSPGRP`
  - `TIOCGWINSZ`
  - `TIOCSWINSZ`
  - `TIOCGPTN`
  - `TIOCSPTLCK`
  - `TIOCSCTTY`
  - `TIOCNOTTY`

这说明后续跑 Codex TUI 有基础，但第一阶段仍然建议从 `codex exec` 开始。

### 2.3 当前已有测试信号

`test-suit/starryos` 中已有很多可复用基线：

- `smoke`
- `busybox`
- `findutils`
- `grep`
- `python-hello`
- `apk-curl`
- `apt`
- `postgresql`
- `stress-ng`
- 文件 I/O、pipe、mremap、fallocate、session、Unix socket 等专项回归。

这对 Codex 很重要：它说明 StarryOS 已经跑过复杂用户态、包管理器和真实网络，不需要把所有问题都当成未知。

## 3. 差距矩阵

| 领域 | Codex 需要 | StarryOS 现状 | 风险 | 第一阶段策略 |
| --- | --- | --- | --- | --- |
| 二进制分发 | Linux x86_64/aarch64 musl native binary | 能跑 x86_64/aarch64 ELF，rootfs 是 Alpine | 低 | 先用 `x86_64-unknown-linux-musl` release，不跑 npm |
| 内存 | 官方写 4 GB minimum | QEMU x86_64 默认 `512M` | 高 | Codex 专用 QEMU config 使用 `-m 2G` 起步，必要时 `-m 4G` |
| rootfs 注入 | 需要 `codex`、`rg`、CA、可写 `$HOME` | 有 rootfs、debugfs 注入 helper、apk | 中 | 先手工/脚本注入到 `target/codex/rootfs-*.img` |
| HTTPS | OpenAI API、TLS CA、DNS | `apk-curl` 说明网络路径已有基础 | 中 | 先验证 `curl https://api.openai.com`，再跑 Codex |
| 凭据 | API key 或 ChatGPT auth cache | 文件系统可写；keyring 不应依赖 | 中 | 强制 `cli_auth_credentials_store="file"`，优先使用用户提供的 `target/auth.json`，API key 作为备选 |
| 子进程 | Codex 会跑 shell、git、rg | clone/exec/pipe/fd 基础存在 | 中 | 初期用小 prompt 限制命令；缺什么补什么 |
| git/rg | repo 检测和搜索 | rootfs 不一定有 git；Codex 包带 `rg` | 中 | 注入 `rg`，`apk add git` 或先 `--skip-git-repo-check` |
| sqlite/state | Codex state/log db | 文件 mmap、fcntl lock stub、fsync dummy | 中 | 先观察是否能创建 state；锁语义不足后再补 |
| inotify | `notify` crate 可能触发 | `inotify_init1` 当前返回 unsupported | 高 | 若 Codex 启动触发，先做最小 inotify dummy fd，再做真实 watcher |
| Linux sandbox | Codex landlock/seccomp/bpf | `seccomp` dummy，bpf dummy fd，landlock 未见实现 | 高 | 首阶段禁用/绕过 Codex sandbox |
| TUI | raw mode、alternate screen、keyboard probes | TTY/PTY 有基础但未验证 crossterm 全量 | 中高 | `codex exec` 先行，TUI 单独阶段 |
| 大仓库 workspace | 需要访问 tgoskits 源码 | 默认没有 host 目录共享 | 中高 | 先小 workspace，再注入/复制 tgoskits 子集，最后完整仓库 |
| 并发 | Codex 内部多线程；用户不追求多会话 | futex/clone 有基础 | 中 | 单会话运行，但不能禁用 Codex 内部线程 |

## 4. 分阶段路线图

每个阶段都应该能独立产出一个小 commit 或一个明确的实验记录。只有上一阶段达成“进入下一阶段条件”，才继续推进。

## 阶段 A：基线环境和可观测性

### 目标

确认当前 `exp4_codex` 上 StarryOS x86_64 的启动、网络、rootfs 和日志链路健康；为后续 Codex 失败定位准备好观测手段。

### 具体任务

0. 先确认 host 侧 `xtask` 工具链本身可用。

   ```bash
   cargo xtask starry test qemu -l
   ```

   这一步只列出用例，不启动 QEMU。若这里就失败，先修 host 环境再进入阶段 A 的运行验证。当前复审时观察到一个实际风险：`libudev-sys` 构建会因为找不到 `libudev.pc` 失败，需要在宿主机安装 libudev 开发包，或正确设置 `PKG_CONFIG_PATH` 指向包含 `libudev.pc` 的目录。这个问题属于宿主机依赖缺失，不是 StarryOS/Codex 兼容性问题。

1. 确认 x86_64 StarryOS 能启动到 shell。

   ```bash
   cargo xtask starry rootfs --arch x86_64
   cargo xtask starry qemu --target x86_64-unknown-none
   ```

2. 运行现有最接近 Codex 的回归：

   ```bash
   cargo xtask starry test qemu --target x86_64-unknown-none -c smoke
   cargo xtask starry test qemu --target x86_64-unknown-none -c busybox
   cargo xtask starry test qemu --target x86_64-unknown-none -c grep
   cargo xtask starry test qemu --target x86_64-unknown-none -c apk-curl
   cargo xtask starry test qemu --target x86_64-unknown-none -c test-pipe-syscalls
   ```

3. 在不引入 Codex 的前提下，先跑一组“现代 CLI 前置条件”小程序或等价 shell smoke：

   ```text
   hello                 # 只验证 ELF 启动和 stdout
   args_test a b c       # 验证 argv
   env_test              # 验证 envp、HOME、PATH、CODEX_HOME
   file_rw_test          # 验证 /tmp、/root、普通文件读写
   getrandom_test        # 验证 /dev/urandom 与 getrandom
   dns_test              # 验证 DNS/resolv.conf
   https_test            # 验证 TCP、TLS、CA、系统时间、随机数
   ```

   这些测试不是要新造一套大框架；已有 `smoke`、`apk-curl` 能覆盖的部分直接复用。只有当 Codex 失败原因不清晰时，才把失败 syscall 抽成小 C case，避免一上来让 Codex 成为唯一复现器。

4. 为 Codex 专用实验准备独立目录，不污染默认 rootfs：

   ```text
   target/codex/
     assets/
     overlay/
     rootfs/
     logs/
     qemu/
     notes/
   ```

5. 准备一个 Codex 专用 QEMU 配置，初始建议：

   - arch：`x86_64`
   - memory：先 `2G`，如果 HTTPS 请求或 sqlite/state 明显内存不足，再升到 `4G`
   - netdev：沿用 `virtio-net-pci` + QEMU user net
   - disk：使用 `target/codex/rootfs/rootfs-x86_64-codex.img`
   - fail regex：保留 panic，同时加入 unimplemented syscall 标记
   - timeout：help 类命令 30 秒，网络命令 300 到 600 秒

6. 建立 syscall 观测方式：

   - 首选：在 StarryOS build config 里把 `log = "Debug"` 或临时 `Trace"`，观察 `Unimplemented syscall`、`Unsupported ioctl`。
   - 如果日志太吵，给 `handle_syscall` 增加按进程名/可执行名过滤的临时 trace 开关，例如只对 `codex` 打 syscall 号。
   - 记录所有失败命令的：
     - Codex 命令行
     - rootfs 镜像路径
     - QEMU config
     - StarryOS commit
     - Codex version
     - 第一条 fatal syscall/ioctl/error

### 进入下一阶段条件

- `x86_64` StarryOS 能稳定进入 shell。
- `smoke`、`busybox`、`grep` 至少通过。
- `apk-curl` 通过，或失败原因明确不是 StarryOS 网络栈问题。
- `/tmp`、`/root`、`/dev/null`、`/dev/zero`、`/dev/urandom`、`/proc/self/exe`、基础 env 传递和 CA/DNS 路径被验证过。
- 能拿到含 syscall 日志的 QEMU 输出。

### 本阶段可能需要补的代码

正常不需要改内核。如果现有 x86_64 baseline 失败，先修 baseline，不引入 Codex。

### 验证要求

如果改了 StarryOS 内核或相关 crate：

```bash
cargo fmt
cargo xtask clippy --package starry-kernel
cargo xtask starry test qemu --target x86_64-unknown-none -c smoke
```

如果改的是 `scripts/axbuild`：

```bash
cargo fmt
cargo xtask clippy --package axbuild
```

## 阶段 B：Codex 二进制注入和离线 smoke

### 目标

让 Codex native binary 在 StarryOS guest 中完成最小进程启动，不涉及 OpenAI 网络请求。

### 具体任务

1. 先在普通 Linux 上采集 Codex 的基准 syscall 轨迹，用来和 StarryOS 日志对照。

   ```bash
   strace -ff -o target/codex/logs/linux-codex-version.log codex --version
   strace -ff -o target/codex/logs/linux-codex-help.log codex --help
   strace -ff -o target/codex/logs/linux-codex-exec-help.log codex exec --help
   ```

   等在线凭据准备好后，再补一条：

   ```bash
   CODEX_HOME=/home/threetu33/os_biglabB_task2/tgoskits/target \
   strace -ff -o target/codex/logs/linux-codex-exec-online.log \
     codex exec --sandbox danger-full-access --skip-git-repo-check \
     'Reply with exactly: STARRY_CODEX_ONLINE_OK'
   ```

   `strace` 是辅助输入，不是阶段 B 的硬门槛；如果宿主机暂时没有 `strace`，可以先用 StarryOS 的 syscall/debug 日志推进，后续再补 Linux 对照日志。对照时只按真实阻塞排序补能力：P0 是启动必需 syscall，P1 是配置/认证读取，P2 是 HTTPS，P3 是 workspace 文件读写，P4 才是 shell 子命令执行。不要为了 Codex 大包一次性补所有 syscall。

2. 获取 Codex native binary。

   推荐使用 GitHub Release 的 `codex-x86_64-unknown-linux-musl.tar.gz`。如果为了快速实验，也可以先复制本机 npm 安装中的 native binary，但必须在记录里标注版本。

   本机当前可观察到：

   ```text
   codex-cli 0.115.0
   codex: ELF 64-bit LSB pie executable, x86-64, static-pie linked, stripped
   rg:    ELF 64-bit LSB pie executable, x86-64, static-pie linked, stripped
   ```

3. 构造 overlay：

   ```text
   target/codex/overlay/
     usr/local/bin/codex
     usr/local/bin/rg
     root/.codex/auth.json      # 从 target/auth.json 复制，仅本地测试使用，不提交
     root/.codex/config.toml
     root/workspace/hello/README.md
   ```

4. `config.toml` 初始建议：

   ```toml
   cli_auth_credentials_store = "file"
   ```

   如果后续版本必须显式指定模型，再补当前账号确认可用的模型。不要把模型名当成 StarryOS 兼容性的前置条件。

   ```toml
   # model = "<account-available-model>"
   ```

5. 用 `debugfs` 或复用 `scripts/axbuild/src/rootfs/inject.rs` 对 rootfs 副本注入 overlay。

   初期可以先写一次性脚本放在 `target/codex/notes/` 或本地 shell 历史里，不急着提交。等流程稳定后，再正式把 prebuilt asset pipeline 合入 `scripts/axbuild`。

6. StarryOS guest 里跑：

   ```sh
   export HOME=/root
   export USER=root
   export SHELL=/bin/sh
   export TERM=xterm-256color
   export PATH=/usr/local/bin:/usr/bin:/bin
   export CODEX_HOME=/root/.codex

   codex --version
   codex --help
   codex exec --help
   codex login status
   rg --version
   ```

### 进入下一阶段条件

- `codex --version` 成功返回。
- `codex --help` 成功打印命令列表。
- `codex exec --help` 成功返回。
- `codex login status` 不 panic；可以返回未登录。
- QEMU 日志中没有必须立即处理的 fatal page fault、panic、unimplemented syscall。

允许出现但需要记录的非 fatal 现象：

- `rseq` 注册返回 `ENOSYS`。
- Codex 查找浏览器、keyring 或 TTY 时失败，但 help/status 命令仍能退出。
- 某些 ioctl 返回 `ENOTTY`，只要程序正确 fallback。

### 失败优先级

1. 如果 ELF 装载失败：先看 static PIE、program header、地址空间映射。
2. 如果一启动就 `ENOSYS`：先补 syscall stub 或确认 libc fallback。
3. 如果访问 `/proc/self/exe`、`/proc/self/fd` 失败：修 procfs symlink/readlink。
4. 如果 help 输出一半卡住：看 stdout/pipe/tty/poll。
5. 如果创建 `$CODEX_HOME` 失败：看 mkdir/openat/stat/permissions。

### 验证要求

本阶段若只注入资产不改代码，不需要 clippy。若改 syscall：

```bash
cargo fmt
cargo xtask clippy --package starry-kernel
cargo xtask starry test qemu --target x86_64-unknown-none -c smoke
```

## 阶段 C：auth.json、TLS、HTTPS 和最小在线 Codex

### 目标

让 Codex 在 StarryOS guest 内完成一次真实 OpenAI API 请求，并打印一个可匹配的成功字符串。

### 认证策略

首选使用用户提供的 `target/auth.json` 作为本地测试用 ChatGPT 登录缓存；如需程序化或 CI 风格验证，再使用 API key，不做 guest 内浏览器登录。

原因：

- 官方认证文档说明 CLI 支持 ChatGPT 登录和 API key 登录。
- 官方文档建议 programmatic CLI workflow 使用 API key。
- 浏览器登录依赖 callback server/browser/headless flow，会额外引入无关变量。
- API key 可以通过环境变量或 `codex login --with-api-key` 注入，适合 QEMU 手工实验。

不要把 `auth.json` 或 API key 写进仓库、测试配置或提交记录。

### 具体任务

1. 确认 guest 里 CA 和 DNS。

   ```sh
   cat /etc/resolv.conf
   ls /etc/ssl/certs/ca-certificates.crt
   ```

   如果 CA 不存在：

   ```sh
   apk update
   apk add ca-certificates
   update-ca-certificates
   ```

2. 先不用 Codex，验证 HTTPS：

   ```sh
   apk add curl
   curl -Iv https://api.openai.com
   ```

   这里的成功标准是 DNS、TCP、TLS 和证书链能走通，并能收到 HTTP 响应头；HTTP 状态码可能是 401、404 或其他非 2xx，不应仅凭状态码判失败。

3. 设置 Codex 认证，首选使用本地提供的 `target/auth.json`。

   方式 A：ChatGPT 登录缓存，也是当前实验的首选方式。

   - host 侧已有 `/home/threetu33/os_biglabB_task2/tgoskits/target/auth.json`。
   - 注入 rootfs 后应放到 guest 的 `/root/.codex/auth.json`，或运行时设置 `CODEX_HOME` 指向包含 `auth.json` 的目录。
   - 当前 host 侧已验证：`CODEX_HOME=/home/threetu33/os_biglabB_task2/tgoskits/target codex login status -c 'cli_auth_credentials_store="file"'` 返回 ChatGPT 已登录。
   - 它不是 `OPENAI_API_KEY`，而是 CLI 的网页登录缓存；后续可能过期，若 host 侧 `login status` 失败，应先重新登录或改用 API key，再排查 guest。
   - 这个文件是明文登录缓存，按密码处理：只用于本地测试，不提交、不贴日志、不放进公开 test case。

   guest 中验证：

   ```sh
   export CODEX_HOME=/root/.codex
   codex login status -c 'cli_auth_credentials_store="file"'
   ```

   方式 B：API key 备选，只用于程序化或 CI 风格验证。

   ```sh
   export OPENAI_API_KEY='...'
   export CODEX_HOME=/root/.codex
   # 或写入 file auth cache:
   printf '%s' "$OPENAI_API_KEY" | codex login --with-api-key -c 'cli_auth_credentials_store="file"'
   ```

4. 最小在线命令。

   ```sh
   codex exec \
     --sandbox danger-full-access \
     --skip-git-repo-check \
     -C /root \
     'Reply with exactly: STARRY_CODEX_ONLINE_OK'
   ```

   如果当前 CLI 版本在 `danger-full-access` 下仍要求交互确认，使用等价绕过命令：

   ```sh
   codex exec \
     --skip-git-repo-check \
     --dangerously-bypass-approvals-and-sandbox \
     -C /root \
     'Reply with exactly: STARRY_CODEX_ONLINE_OK'
   ```

5. 如果当前 Codex 版本必须选择模型，显式加当前账号可用的模型，例如：

   ```sh
   -m <account-available-model>
   ```

### 进入下一阶段条件

- guest 里 `curl -Iv https://api.openai.com` 能完成 TLS 握手并收到响应头。
- Codex 能识别 `/root/.codex/auth.json` 或 API key file auth。
- `codex exec` 返回包含 `STARRY_CODEX_ONLINE_OK` 的最终消息。
- 失败时日志里没有未解释的 kernel panic。

### 失败优先级

1. DNS 失败：检查 `target/rootfs` 中 `/etc/resolv.conf` 是否被设置为 QEMU slirp `10.0.2.3`。
2. TLS 失败：检查 CA bundle；必要时设置 `CODEX_CA_CERTIFICATE` 或 `SSL_CERT_FILE`。
3. socket 失败：看 `connect`、`getsockopt(SO_ERROR)`、nonblocking TCP、epoll。
4. WebSocket/SSE stream 卡住：看 `poll/epoll`、nonblocking read/write、`recvmsg`。
5. Codex 认证失败：先在 host 上确认 `target/auth.json` 可用；guest 内只排查文件路径、`CODEX_HOME`、权限、时间和网络。

## 阶段 D：让 Codex 操作一个小 workspace

### 目标

证明 Codex 不只是能联网回复，还能在 StarryOS guest 里完成 coding agent 的基础闭环：先读取和修改小工作目录，再后置验证简单 shell 命令。不要一开始就跑 `git`、`cargo`、`pytest` 或大仓库扫描。

### 小 workspace 设计

初始 workspace 不要直接上 `tgoskits` 全仓。先用小仓库：

```text
/root/workspace/hello/
  .git/
  AGENTS.md
  README.md
  src/main.rs 或 hello.sh
```

guest 准备：

```sh
apk add git
mkdir -p /root/workspace/hello
cd /root/workspace/hello
git init
git config user.name 'StarryOS Codex Smoke'
git config user.email 'starry-codex-smoke@example.invalid'
cat > README.md <<'EOF'
# hello
EOF
git add README.md
git commit -m 'init'
```

如果 git 暂时不可用，可以先不建 `.git`，所有 Codex 命令加 `--skip-git-repo-check`，先验证普通目录读写；进入阶段 E 前再补上 git、`git status` 和 `git diff`。

### Codex 命令

1. 只读探索：

   ```sh
   codex exec \
     --dangerously-bypass-approvals-and-sandbox \
     -C /root/workspace/hello \
     'List the files in this repository and summarize them.'
   ```

2. 文件修改：

   ```sh
   codex exec \
     --dangerously-bypass-approvals-and-sandbox \
     -C /root/workspace/hello \
     'Read README.md, then append one line to README.md saying StarryOS can run a small Codex workspace smoke.'
   ```

3. 搜索，放在基础读写之后：

   ```sh
   codex exec \
     --dangerously-bypass-approvals-and-sandbox \
     -C /root/workspace/hello \
     'Use rg to find the word hello, then report the matching files.'
   ```

4. 最后才验证 Codex 调 shell 命令：

   ```sh
   codex exec \
     --dangerously-bypass-approvals-and-sandbox \
     -C /root/workspace/hello \
     'Run `sh -lc "echo hello && ls -la && cat README.md"` and summarize the result.'
   ```

### 进入下一阶段条件

- Codex 能读取 README 这类小文件并给出正确摘要。
- Codex 能创建或修改一个普通文件，修改后的文件在 guest 内能读回。
- 在上述两项稳定后，Codex 能在 guest 内执行至少一个简单 shell command。
- `rg` 可用。
- 如果安装 git，则 `git status`、`git diff` 可用。
- 过程中未出现未解释的死锁或 panic。

### 可能暴露的问题

| 现象 | 可能原因 | 处理 |
| --- | --- | --- |
| Codex spawn 命令失败 | `clone`/`vfork`/`execve`/pipe/fd 语义问题 | 最小 C/Rust 子进程测试复现 |
| git 卡住 | file lock、fcntl、poll 或 entropy | 看 syscall 日志和 `/dev/urandom` |
| sqlite 报错 | mmap/shared lock/fsync/fcntl | 先禁用持久 state 或补 sqlite 需要的 fcntl |
| shell 输出丢失 | pipe、poll、wait4、SIGCHLD | 做 pipe + child 输出小测试 |
| Codex 认为不是 git repo | git 不存在或 `.git` stat/readlink 兼容问题 | 先安装 git，再修 statx/fstatat |

## 阶段 E：扩大到 TGOSKits 源码

### 目标

让 Codex 在 StarryOS guest 内以 `/root/tgoskits` 为工作目录运行，至少完成只读理解和小范围文件修改。

### 工作区放入 guest 的三种方案

#### 方案 E1：rootfs overlay 注入全仓或子集

优点：

- 最简单，不需要新设备或共享文件系统。
- 适合第一轮验证。

缺点：

- `tgoskits` 全仓很大，注入慢，rootfs 容量可能不足。
- 每次 host 修改都需要重新注入或复制。

建议用法：

- 先注入一个子集：
  - `AGENTS.md`
  - `README*.md`
  - `os/StarryOS/kernel/src/syscall/`
  - `test-suit/starryos/GUIDE.md`
  - `scripts/axbuild/src/starry/`
- 只读 prompt：

  ```sh
  codex exec \
    --dangerously-bypass-approvals-and-sandbox \
    -C /root/tgoskits \
    'Explain how StarryOS syscall dispatch is organized. Mention file paths.'
  ```

#### 方案 E2：guest 内用 git clone

优点：

- 更接近真实 Linux 环境。
- 不需要 host 侧 debugfs 注入大目录。

缺点：

- 依赖 guest 里 git、TLS、DNS、GitHub 访问。
- clone 全仓可能慢，rootfs 容量仍然要扩大。

建议先 shallow clone：

```sh
apk add git ca-certificates
git clone --depth 1 https://github.com/rcore-os/tgoskits.git /root/tgoskits
```

#### 方案 E3：后续做 host/guest 共享目录

优点：

- 开发体验最好。

缺点：

- 当前 QEMU 配置没有现成共享目录。
- 如果走 9p/virtio-fs，需要 StarryOS 对应驱动和 VFS 接入，不适合第一阶段。

建议：

- 不作为“先跑起来”的条件。
- 等 Codex 本体跑通后，再单独评估是否值得实现。

### 进入下一阶段条件

- Codex 能在 `/root/tgoskits` 或子集目录下启动。
- 能用 `rg` 查找代码。
- 能引用真实路径总结架构。
- 能做一个不需要编译的大改动，例如改文档或生成计划文件。
- `git diff` 可用，或者至少能读取修改后的文件。

### 不建议第一轮做的事

- 在 guest 内完整编译 TGOSKits。
- 在 guest 内跑 `cargo xtask`。
- 让 Codex 自动大范围修改 StarryOS 内核。
- 同时追 TUI、sandbox、full repo build。

## 阶段 F：按日志补齐 Codex 触发的 syscall/文件系统缺口

### 目标

把 Codex 在阶段 B 到 E 暴露的真实缺口转化为小 PR，每个 PR 一个明确行为和回归测试。

### 优先级 F0：只按真实 trace 补 syscall

补 syscall 的顺序以 Linux `strace` 和 StarryOS 运行日志为准，不按“Codex 可能会用到”提前铺开。建议分层：

| 优先级 | 类别 | 典型 syscall |
| --- | --- | --- |
| P0 | 进程启动与内存 | `execve`、`mmap`、`mprotect`、`munmap`、`brk`、`arch_prctl`、`set_tid_address` |
| P1 | 基础文件和配置 | `openat`、`read`、`write`、`close`、`lseek`、`newfstatat`/`fstatat`、`getdents64`、`readlinkat`、`fcntl` |
| P2 | 时间和随机数 | `clock_gettime`、`gettimeofday`、`getrandom`、`nanosleep` |
| P3 | 线程同步和信号 | `clone`、`futex`、`rt_sigaction`、`rt_sigprocmask`、`sigaltstack` |
| P4 | 网络和 HTTPS | `socket`、`connect`、`setsockopt`、`getsockopt`、`sendmsg`、`recvmsg`、`shutdown` |
| P5 | 异步 I/O | `poll`、`ppoll`、`epoll_create1`、`epoll_ctl`、`epoll_pwait` |
| P6 | shell 子命令 | `pipe2`、`dup2`/`dup3`、`wait4`、`kill`、`exit_group`、`chdir`、`getcwd` |

即使暂不追求 Codex 并发，也不能假设 `clone`、`futex`、`epoll` 完全不会出现；现代 Rust async runtime、HTTP client、TLS 和日志路径可能在单次请求中使用这些能力。可以不优化并发性能，但必须保证单会话语义正确。

### 优先级 F1：inotify 最小实现

当前 `syscall/mod.rs` 对 `inotify_init1` 返回 unsupported，而 Codex 依赖树包含 `notify`。它不是开局必做项；只有当 `codex --help`、`codex exec` 或 workspace smoke 的真实日志触发 inotify 失败时，再实现最小版本。

先做最小可用：

- 新增 `FileLike`：`InotifyFile`。
- 实现 syscall：
  - `inotify_init1(flags)`
  - `inotify_add_watch(fd, pathname, mask)`
  - `inotify_rm_watch(fd, wd)`
- 语义：
  - `init1` 返回可 poll 的 fd。
  - 支持 `IN_NONBLOCK`、`IN_CLOEXEC`。
  - `add_watch` 分配递增 watch descriptor。
  - 初期可以不产生真实事件；`read` 在 nonblocking 下返回 `EAGAIN`。
  - `poll` 默认不报告 readable。
- 测试：
  - C case：init/add/rm/close。
  - nonblocking read 返回 `EAGAIN`。
  - flags 校验。

进入下一阶段条件：

```bash
cargo fmt
cargo xtask clippy --package starry-kernel
cargo xtask starry test qemu --target x86_64-unknown-none -c <inotify-case>
```

### 优先级 F2：Codex 子进程闭环缺口

如果 Codex 执行 shell command 出错，按最小复现拆：

- `pipe2 + clone/vfork + execve + wait4`
- stdout/stderr pipe capture
- stdin pipe close on exec
- CLOEXEC
- `SIGCHLD`
- `kill`/`tgkill`
- `close_range`
- `dup3`

每个缺口都加一个小 C case 或 grouped case，不直接用 Codex 当唯一测试。

### 优先级 F3：sqlite/state 相关缺口

Codex `codex-state` 使用 sqlite。可能触发：

- `fcntl` 文件锁
- `pread64` / `pwrite64`
- `ftruncate`
- `fsync` / `fdatasync`
- `mmap(MAP_SHARED)`
- `unlink` / `rename`
- `statx`

StarryOS 当前 `fcntl` lock 多数是 stub，`flock` 也是 stub，单进程 Codex 可能够用。若 sqlite 报锁错误或数据库损坏，再按 sqlite 行为补。

建议先做一个 sqlite smoke：

- guest 安装 `sqlite` 或放入一个静态 sqlite 测试程序。
- 创建 db、建表、插入、查询、关闭、重开。

### 优先级 F4：Codex sandbox 相关缺口

首阶段用：

```sh
--dangerously-bypass-approvals-and-sandbox
```

或能用时：

```sh
--sandbox danger-full-access
```

后续如果要支持 Codex 的 Linux sandbox，再评估：

- landlock syscalls
- `prctl(PR_SET_NO_NEW_PRIVS)`
- seccomp 行为
- bpf
- namespace flags
- mount namespace / bind mount

这不应阻塞“跑起来”。

### 优先级 F5：TUI 终端缺口

当 `codex exec` 已经稳定后，再尝试：

```sh
codex --no-alt-screen --skip-git-repo-check
```

重点观察：

- raw mode 是否成功。
- `TIOCGWINSZ` 是否返回合理行列。
- crossterm terminal probe 是否卡住。
- Ctrl-C / SIGINT 是否正确到达。
- 输入回显、退格、方向键是否正常。
- alternate screen 是否可选禁用。

如果失败，按 ioctl/termios 逐项补，不要和网络/API 问题混在一个 PR。

### 阶段 F 实施记录（2026-05-10）

阶段 F 已完成。实际实施时没有提前铺开 `inotify`、sqlite、sandbox 或 TUI，而是按阶段 B 到 E 的真实 Codex 日志修复了四类阻塞项：

- `prctl(PR_CAPBSET_READ)`：补最小 capability bounding set 查询，消除 `unsupported option 23`。
- `getcwd`：修正原始 syscall 返回值和空指针错误码，使普通 `git init/config/status/diff` 不再需要 `--git-dir` / `--work-tree` 绕过。
- TCP keepalive：接受 `TCP_KEEPIDLE`、`TCP_KEEPINTVL`、`TCP_KEEPCNT`、`TCP_USER_TIMEOUT`，消除 curl/hyper 的 `ENOPROTOOPT` / `errno 92` 兼容问题。
- unreaped zombie process：让 `getpgid`、`getsid`、`kill(pid, 0)`、`kill(pid, SIGKILL)` 在 `waitpid` 前仍能看到 zombie child，修复 Codex 写文件后清理 child process 时的 `No such process (os error 3)`。

本阶段同时新增 x86_64 QEMU bugfix case：

- `bug-prctl-capbset-read`
- `bug-getcwd-syscall-return`
- `bug-tcp-keepalive-options`
- `bug-zombie-process-queries`

已通过的验证：

```bash
cargo fmt
git diff --check
PKG_CONFIG_PATH=target/local-pkgconfig:target/host-libs/pkgconfig cargo xtask clippy --package starry-kernel
PATH=target/codex/qemu-build-x86_64-user:target/codex/qemu-build-x86_64:$PATH \
  PKG_CONFIG_PATH=target/local-pkgconfig:target/host-libs/pkgconfig \
  cargo xtask starry test qemu --arch x86_64 -c bugfix
PATH=target/codex/qemu-build-x86_64-user:target/codex/qemu-build-x86_64:$PATH \
  PKG_CONFIG_PATH=target/local-pkgconfig:target/host-libs/pkgconfig \
  cargo xtask starry qemu --target x86_64-unknown-none \
    --qemu-config target/codex/qemu/qemu-x86_64-codex-stage-f-compat.toml \
    --rootfs target/codex/rootfs/rootfs-x86_64-codex.img
PATH=target/codex/qemu-build-x86_64-user:target/codex/qemu-build-x86_64:$PATH \
  PKG_CONFIG_PATH=target/local-pkgconfig:target/host-libs/pkgconfig \
  cargo xtask starry qemu --target x86_64-unknown-none \
    --qemu-config target/codex/qemu/qemu-x86_64-codex-stage-f-write.toml \
    --rootfs target/codex/rootfs/rootfs-x86_64-codex.img
```

关键成功标记：

```text
STARRY_GROUPED_TESTS_PASSED
STARRY_CODEX_STAGE_F_COMPAT_PASSED
STARRY_CODEX_STAGE_F_WRITE_PASSED
```

详细记录见 `target/codex/notes/stage_f_report.md`。排查过程中额外发现一个 tmpfs/pseudofs atomic context panic，但它不是阶段 B 到 E 的 Codex 主线阻塞项；该问题已在阶段 I 后续清理中修复。

## 阶段 G：正式测试套件接入

### 目标

把不含 secret 的 Codex smoke 固化为 `test-suit/starryos` case，使后续改 syscall 不会破坏 Codex 基础启动。

### 推荐 case 分层

1. `codex-help`

   - 不需要 API key。
   - 注入 native `codex` 和 `rg`。
   - 运行：

     ```sh
     codex --version
     codex --help
     codex exec --help
     rg --version
     echo CODEX_HELP_TEST_PASSED
     ```

2. `codex-login-status`

   - 不需要 API key。
   - 配置 `CODEX_HOME=/tmp/codex-home`。
   - 运行 `codex login status`，接受“未登录”。

3. `codex-local-workspace`

   - 不联网时只验证 guest workspace 环境：`rg`、git、小文件读写、shell、`git diff`。
   - 这个 case 不应声称已经验证 Codex 模型读写 workspace，因为模型请求一定需要认证和网络。
   - 若要验证 Codex 真正读写 workspace，应单独放到 `codex-online-workspace` 或手工 online smoke，并默认不进 CI。

4. `codex-online`

   - 默认不进 CI。
   - 需要手工设置 secret。
   - 只在本地或专门环境运行。

### 当前测试框架缺口

`test-suit/starryos` 现在有这些 pipeline：

- `plain`
- `c`
- `sh`
- `python`
- `grouped`

但没有“host 预编译二进制资产注入”pipeline。Codex 二进制超过普通脚本，建议新增一个轻量 asset pipeline：

```text
test-suit/starryos/normal/qemu-smp1/codex-help/
  qemu-x86_64.toml
  assets/
    usr/local/bin/codex
    usr/local/bin/rg
    root/.codex/config.toml
```

或不把大二进制提交到仓库，改成：

```text
assets.toml
```

描述从 host 缓存路径或 URL 准备资产。推荐不要把 100MB 级 release binary 放进 git。

### 可选实现方案

#### 方案 G1：仅本地脚本，不进 CI

- 最快。
- 在 `target/codex` 里保存脚本和 rootfs。
- 适合开发早期。

#### 方案 G2：新增 `prebuilt-assets` pipeline

- 在 `scripts/axbuild/src/test/case` 或 Starry 专用测试准备逻辑中识别 `assets/`。
- 将 `assets/` 原样注入 case rootfs。
- 适合小二进制或脚本，但不适合提交大型 Codex binary。

#### 方案 G3：新增 `download-assets` pipeline

- case 配置声明 URL、sha256、目标路径。
- host 下载到 `target/codex/cache`。
- 注入 rootfs。
- 适合 Codex release binary。

推荐：开发早期 G1，稳定后 G3。

### 进入下一阶段条件

- `codex-help` 能用一条 `cargo xtask starry test qemu ... -c codex-help` 复现。
- case 不需要 secret。
- Codex binary 来源、版本、sha256 可追踪。
- 失败 regex 能捕获 panic、unimplemented syscall、Codex fatal error。

后续正式修改 `test-suit/starryos` 时，应使用项目本地 `starry-test-suit` skill。

### 阶段 G 实施记录（2026-05-10）

阶段 G 已完成。实际选择的是 **G3 的轻量落地形态**：不把大型 Codex/ripgrep 二进制提交进 git，而是在正式 test-suit case 中用 `assets.toml` 记录 host 侧缓存路径、guest 注入路径和 SHA-256；测试运行器新增 `prebuilt-assets` pipeline 负责校验和注入。

新增正式 case：

```text
test-suit/starryos/normal/qemu-smp1/codex-help/
  assets.toml
  qemu-x86_64.toml
```

该 case 不需要 API key，不联网，不读取 `target/auth.json`。它覆盖：

- `codex --version`
- `codex --help`
- `codex exec --help`
- `rg --version`
- 空 `CODEX_HOME` 下的 `codex login status`，期望 `Not logged in`
- guest 内本地 workspace 的 `git init/config/commit/status/diff`
- `rg -n --with-filename` 搜索工作区文件

已通过的验证：

```bash
cargo fmt
git diff --check
PKG_CONFIG_PATH=target/local-pkgconfig:target/host-libs/pkgconfig cargo xtask clippy --package axbuild
PKG_CONFIG_PATH=target/local-pkgconfig:target/host-libs/pkgconfig cargo test -p axbuild test::case
PATH=target/codex/qemu-build-x86_64-user:target/codex/qemu-build-x86_64:$PATH \
  PKG_CONFIG_PATH=target/local-pkgconfig:target/host-libs/pkgconfig \
  cargo xtask starry test qemu --arch x86_64 -c codex-help
```

关键成功标记：

```text
STARRY_CODEX_STAGE_G_HELP_OK
STARRY_CODEX_STAGE_G_LOGIN_STATUS_OK
STARRY_CODEX_STAGE_G_LOCAL_WORKSPACE_OK
STARRY_CODEX_STAGE_G_CODEX_HELP_PASSED
```

详细记录见 `target/codex/notes/stage_g_report.md`。排查中再次确认：当 guest shell 命令失败时，仍可能触发阶段 F 记录过的 tmpfs/pseudofs atomic context panic；这不是 `codex-help` 通过路径的问题，已在阶段 I 后续清理中修复。

## 阶段 H：Codex 在真实 TGOSKits 子任务上试跑

### 目标

让 guest 内 Codex 完成一个很小、可审查、低风险的 TGOSKits 任务。

### 建议任务

1. 只读任务：

   ```text
   Summarize how StarryOS qemu test cases are discovered. Mention exact file paths.
   ```

2. 文档任务：

   ```text
   Add a short note to a dedicated scratch markdown file explaining the current Codex smoke setup.
   ```

   如果需要用 `git diff` 验收，scratch 文件必须放在被注入的可追踪工作区中；`target/` 通常被 git 忽略，只适合作为本地实验目录，不适合验证仓库 diff。

3. 代码搜索任务：

   ```text
   Find where inotify syscalls are dispatched and report whether they are implemented.
   ```

不要一开始让 Codex 改 syscall。先证明它能在 guest 里理解仓库和产生可控输出。

### 验收条件

- Codex 输出能引用真实文件路径。
- 如果要求修改文件，`git diff` 或文件内容能显示正确改动。
- 没有破坏 rootfs 或 shell 环境。
- 若 Codex 执行命令，命令和输出可在日志中复现。

### 阶段 H 实施记录（2026-05-10）

阶段 H 已完成。本阶段没有修改已跟踪源码，而是在 `target/codex` 下构造了一个 TGOSKits 源码子集，让 guest 内 Codex 完成可审查的真实仓库任务：先阅读 StarryOS QEMU test-suit 发现逻辑，再创建一个 scratch markdown 文件说明当前 Codex CLI smoke 设置。

实际采用的是阶段 E1 的子集注入方式：

```text
target/codex/workspaces/stage_h_tgoskits_subset/
target/codex/workspaces/stage_h_tgoskits_subset.tar.gz
target/codex/notes/inject_stage_h_subset.debugfs
target/codex/qemu/qemu-x86_64-codex-stage-h-tgoskits-task.toml
```

源码子集包含 `AGENTS.md`、`README*.md`、项目本地 `starry-test-suit` skill、`scripts/axbuild/src/starry/`、`scripts/axbuild/src/test/`、`test-suit/starryos/GUIDE.md` 和阶段 G 新增的 `codex-help` case。

只读任务要求 Codex 只运行固定脚本 `/tmp/stage-h-read-context.sh`，再总结 StarryOS QEMU case 的发现流程。验收检查它的最终回答包含：

```text
STARRY_STAGE_H_READ_OK
scripts/axbuild/src/starry/test.rs
scripts/axbuild/src/test/qemu.rs
scripts/axbuild/src/test/case.rs
test-suit/starryos/GUIDE.md
test_commands
```

写文件任务要求 Codex 直接使用 file-edit/apply_patch 工具创建 `STARRY_STAGE_H_CODEX_SMOKE.md`，并且不运行 shell 命令。guest 侧随后用 `grep`、`git status --short` 和 `git diff --cached` 验证文件内容和唯一改动。

源码子集解压后会执行 `chown -R root:root /root/tgoskits`。这是因为 host 侧生成的 tar 可能保留 host 用户 uid/gid；如果 guest 内 root 直接运行 git，Git 会触发 `detected dubious ownership` 的 safe.directory 保护。统一 owner 后，Stage H 使用普通 `git config`、`git status` 和 `git diff`，不再需要显式 `--git-dir` / `--work-tree` 绕过。

已通过的验证：

```bash
PATH=target/codex/qemu-build-x86_64-user:target/codex/qemu-build-x86_64:$PATH \
  PKG_CONFIG_PATH=target/local-pkgconfig:target/host-libs/pkgconfig \
  cargo xtask starry qemu \
    --target x86_64-unknown-none \
    --qemu-config target/codex/qemu/qemu-x86_64-codex-stage-h-tgoskits-task.toml \
    --rootfs target/codex/rootfs/rootfs-x86_64-codex.img
```

关键成功标记：

```text
CODEX_STAGE_H_ENV_READY
STARRY_STAGE_H_SOURCE_TREE_READY
STARRY_STAGE_H_GIT_BASELINE_READY
STARRY_STAGE_H_READ_CONTEXT_OK
STARRY_STAGE_H_READ_TASK_OK
STARRY_STAGE_H_WRITE_TASK_OK
STARRY_CODEX_STAGE_H_TGOSKITS_TASK_PASSED
```

最终日志见：

```text
target/codex/logs/starry-codex-stage-h-tgoskits-task-rerun3.log
target/codex/notes/stage_h_report.md
```

排查中遇到并处理了四个问题：tar 初版解压路径不对、host tar owner 触发 Git dubious ownership、开放式 Codex 读仓 prompt 会跑成超长探索、裸 `No such process` fail regex 误判 shell TTY process-group 提示。当前配置通过解压后 `chown`、固定上下文脚本、受控写文件 prompt 和收窄 fail regex 解决。Codex 日志中仍可见非阻塞的模型列表刷新 timeout 和一次 `apply_patch(auto_approved=true) exited 1`，但 guest 侧文件与 git diff 验收全部通过。

## 阶段 I：整理成可提交的工程化改动

### 阶段 I 第一轮实施记录（2026-05-10）

阶段 I 已完成一轮针对演示和 failure path 的工程化清理：

- 保留阶段 H 的复杂 prompt，不用更短 prompt 代替真实任务；同时增强 `target/codex/qemu/qemu-x86_64-codex-stage-h-tgoskits-task.toml` 的串口输出，明确打印 read/write 两个任务的 prompt、Codex 最终回答、生成文件内容、git status 和 git diff。
- 调查 `apply_patch(auto_approved=true) exited 1`：Codex runtime 源码显示失败分支仍会取出已提交 delta；结合 Stage H 日志中 guest 侧文件、`grep`、`git status`、`git diff` 均通过，当前按 Codex tool/runtime 层非阻塞状态噪声处理，不作为 StarryOS 阻塞项。
- 修复阶段 F/G 记录过的 tmpfs/pseudofs failure-path panic：`MemoryNode::drop` 不再清理目录 entries；tmpfs inode slab 和 metadata 改为 `SpinNoIrq`；`unlink` / `rename` 在正常 syscall 路径提前清理被移除目录 entries。
- 新增 x86_64 bugfix 回归 `test-suit/starryos/normal/qemu-smp1/bugfix/bug-tmpfs-cwd-drop-safe/`，覆盖“子进程 cwd 位于 tmpfs 目录，父进程先 rmdir，子进程稍后退出”的旧 failure path。

验证结果：

```text
cargo fmt
cargo xtask clippy --package starry-kernel
cargo xtask starry test qemu --arch x86_64 -c bugfix
```

其中 `starry-kernel` 7 组 clippy 全部通过，`bugfix` QEMU 组通过，新增用例输出：

```text
STARRY_GROUPED_TEST_PASSED: /usr/bin/bug-tmpfs-cwd-drop-safe
STARRY_GROUPED_TESTS_PASSED
ok: bugfix
```

详细记录见 `target/codex/notes/stage_i_report.md`。

### 可能的 PR 拆分

1. `test(starryos): add Codex smoke rootfs flow`

   - 准备 Codex 专用 rootfs/overlay 流程。
   - 添加 `codex --version`、`codex --help` 的本地 smoke 脚本。
   - 不提交 `auth.json`、API key 或大型二进制；二进制用版本和 sha256 描述来源。

2. `fix(starry-kernel): close Codex startup syscall gaps`

   - 只修 `codex --version` / `codex --help` 真实阻塞的 syscall 或 ioctl。
   - 每个 syscall 配最小用户态测例，不做 Codex 特判。

3. `test(starryos): add HTTPS client smoke coverage`

   - 固化 DNS、TCP、TLS、CA、系统时间、随机数链路。
   - 目标是 `https_test https://example.com` 或等价 `apk-curl` 增强能过，不直接依赖 Codex。

4. `test(starryos): document local Codex online smoke`

   - 添加本地脚本说明如何使用 `target/auth.json` 或 API key 跑在线 `codex exec`。
   - 不进默认 CI，不包含 secret。

5. `test(starryos): add small Codex workspace smoke`

   - 准备 `/workspace/demo` 或 `/root/workspace/hello`。
   - 验证 Codex 读取 README、修改 README。

6. `test(starryos): add simple shell execution smoke for Codex`

   - 在 rootfs 中明确依赖 `/bin/sh` 或 busybox。
   - 先验证 `sh -lc "echo hello"`、`ls`、`cat README.md`，再让 Codex 调用。

后续如果真实日志显示需要，再单独拆：

- `fix(starry-kernel): add minimal inotify fd support`
- `fix(starry-kernel): improve tty ioctl coverage for Codex TUI`
- `docs(starryos): document running Codex CLI under StarryOS`

### 每个代码 PR 的最低验证

通用：

```bash
cargo fmt
```

改 StarryOS kernel：

```bash
cargo xtask clippy --package starry-kernel
cargo xtask starry test qemu --target x86_64-unknown-none -c smoke
```

改 axbuild/test runner：

```bash
cargo xtask clippy --package axbuild
cargo xtask starry test qemu --target x86_64-unknown-none -l
```

改测试 case：

```bash
cargo xtask starry test qemu --target x86_64-unknown-none -c <case-name>
```

如果某 crate clippy 已经通过但未在 `scripts/test/clippy_crates.csv` 中，应按项目要求补进去。

## 5. 最小命令清单

### 5.1 StarryOS baseline

```bash
cargo xtask starry rootfs --arch x86_64
cargo xtask starry test qemu --target x86_64-unknown-none -c smoke
cargo xtask starry test qemu --target x86_64-unknown-none -c busybox
cargo xtask starry test qemu --target x86_64-unknown-none -c apk-curl
```

### 5.2 Codex guest 环境变量

```sh
export HOME=/root
export USER=root
export SHELL=/bin/sh
export TERM=xterm-256color
export PATH=/usr/local/bin:/usr/bin:/bin
export CODEX_HOME=/root/.codex
export RUST_LOG=error
```

### 5.3 Codex offline smoke

```sh
codex --version
codex --help
codex exec --help
codex login status
rg --version
```

### 5.4 Codex online smoke

```sh
export CODEX_HOME=/root/.codex

codex exec \
  --sandbox danger-full-access \
  --skip-git-repo-check \
  -C /root \
  'Reply with exactly: STARRY_CODEX_ONLINE_OK'
```

如果改用 API key 而不是 `auth.json`：

```sh
export OPENAI_API_KEY='...'
export CODEX_HOME=/root/.codex
```

### 5.5 Codex local workspace smoke

```sh
mkdir -p /root/workspace/hello
cd /root/workspace/hello
cat > README.md <<'EOF'
# hello
EOF

codex exec \
  --skip-git-repo-check \
  --dangerously-bypass-approvals-and-sandbox \
  -C /root/workspace/hello \
  'Create NOTES.md with one sentence about StarryOS, then report the file content.'
```

## 6. 风险登记表

| 风险 | 影响 | 规避 |
| --- | --- | --- |
| Codex latest 版本变化快 | 复现实验困难 | 固定 release version、sha256、二进制路径 |
| QEMU 默认内存不足 | Codex OOM 或 TLS/sqlite 崩溃 | Codex 专用 QEMU config 用 2G/4G |
| rootfs 注入大文件慢 | 开发反馈慢 | 先手工 target 脚本，稳定后做缓存 pipeline |
| `auth.json` 或 API key 泄漏 | 安全事故 | 不进 git、不写 test config、不贴日志；`target/auth.json` 只作本地测试输入 |
| inotify 缺失 | Codex 启动或文件监听失败 | 先最小 dummy fd，真实 watcher 后置 |
| sandbox 缺失 | Codex 默认模式失败 | 首阶段绕过 Codex sandbox |
| TUI 卡住 | 误判 Codex 不可用 | 非交互模式先行 |
| sqlite 锁语义不完整 | state/log 失败 | 单会话先跑，必要时补 fcntl/flock |
| full tgoskits 太大 | rootfs 空间和时间爆炸 | 子集验证，再 full repo |
| guest 内构建 TGOSKits 太重 | 混入工具链和性能问题 | 首阶段只做源码阅读和小文件修改 |

## 7. 建议下一步

马上开始实现时，建议按这个顺序：

1. 新建 `target/codex/` 实验目录和 Codex 专用 rootfs 副本。
2. 下载或复制 `codex`/`rg` static binary，注入 rootfs。
3. 用 x86_64 QEMU + 2G 内存跑 `codex --version`。
4. 根据第一轮日志列出真实 syscall 缺口。
5. 只修阻塞 `--help`/`exec --help` 的缺口。
6. 再用 `target/auth.json` 做 online smoke；API key 只作为备选。

第一阶段成功的定义不要过高：

```text
StarryOS guest 中的 /usr/local/bin/codex 能启动；
codex exec 能通过 HTTPS 拿到一次模型回复；
Codex 能在一个小 workspace 中读写文件。
```

达到这三个条件后，再考虑 test-suit 正式化、inotify、TUI、真实 tgoskits 全仓和 Codex sandbox。
